/*
 * empathy-chat-manager.c - Source for EmpathyChatManager
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <telepathy-glib/telepathy-glib.h>

#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-utils.h>

#include "empathy-chat-window.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-chat-manager.h"

enum {
  CLOSED_CHATS_CHANGED,
  DISPLAYED_CHATS_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE(EmpathyChatManager, empathy_chat_manager, G_TYPE_OBJECT)

/* private structure */
typedef struct _EmpathyChatManagerPriv EmpathyChatManagerPriv;

struct _EmpathyChatManagerPriv
{
  EmpathyChatroomManager *chatroom_mgr;
  /* Queue of (ChatData *) representing the closed chats */
  GQueue *closed_queue;

  guint num_displayed_chat;

  /* account path -> GHashTable<contact -> non-NULL message> */
  GHashTable *messages;

  TpBaseClient *handler;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_CHAT_MANAGER, \
    EmpathyChatManagerPriv))

static EmpathyChatManager *chat_manager_singleton = NULL;

typedef struct
{
  TpAccount *account;
  gchar *id;
  gboolean room;
} ChatData;

static ChatData *
chat_data_new (EmpathyChat *chat)
{
  ChatData *data = NULL;

  data = g_slice_new0 (ChatData);

  data->account = g_object_ref (empathy_chat_get_account (chat));
  data->id = g_strdup (empathy_chat_get_id (chat));
  data->room = empathy_chat_is_room (chat);

  return data;
}

static void
chat_data_free (ChatData *data)
{
  if (data->account != NULL)
    {
      g_object_unref (data->account);
      data->account = NULL;
    }

  if (data->id != NULL)
    {
      g_free (data->id);
      data->id = NULL;
    }

  g_slice_free (ChatData, data);
}

static void
chat_destroyed_cb (gpointer data,
    GObject *object)
{
  EmpathyChatManager *self = data;
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  priv->num_displayed_chat--;

  DEBUG ("Chat destroyed; we are now displaying %u chats",
      priv->num_displayed_chat);

  g_signal_emit (self, signals[DISPLAYED_CHATS_CHANGED], 0,
      priv->num_displayed_chat);
}

static void
process_tp_chat (EmpathyChatManager *self,
    EmpathyTpChat *tp_chat,
    TpAccount *account,
    gint64 user_action_time)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  EmpathyChat *chat = NULL;
  const gchar *id;

  id = empathy_tp_chat_get_id (tp_chat);
  if (!tp_str_empty (id))
    {
      chat = empathy_chat_window_find_chat (account, id,
          empathy_tp_chat_is_sms_channel (tp_chat));
    }

  if (chat != NULL)
    {
      empathy_chat_set_tp_chat (chat, tp_chat);
    }
  else
    {
      GHashTable *chats = NULL;

      chat = empathy_chat_new (tp_chat);
      /* empathy_chat_new returns a floating reference as EmpathyChat is
       * a GtkWidget. This reference will be taken by a container
       * (a GtkNotebook) when we'll call empathy_chat_window_present_chat */

      priv->num_displayed_chat++;

      DEBUG ("Chat displayed; we are now displaying %u chat",
          priv->num_displayed_chat);

      g_signal_emit (self, signals[DISPLAYED_CHATS_CHANGED], 0,
          priv->num_displayed_chat);

      /* Set the saved message in the channel if we have one. */
      chats = g_hash_table_lookup (priv->messages,
          tp_proxy_get_object_path (account));

      if (chats != NULL)
        {
          const gchar *msg = g_hash_table_lookup (chats, id);

          if (msg != NULL)
            empathy_chat_set_text (chat, msg);
        }

      g_object_weak_ref ((GObject *) chat, chat_destroyed_cb, self);
    }
  empathy_chat_window_present_chat (chat, user_action_time);

  if (empathy_tp_chat_is_invited (tp_chat, NULL))
    {
      /* We have been invited to the room. Add ourself as member as this
       * channel has been approved. */
      empathy_tp_chat_join (tp_chat);
    }

  g_object_unref (tp_chat);
}

typedef struct
{
  EmpathyChatManager *self;
  EmpathyTpChat *tp_chat;
  TpAccount *account;
  gint64 user_action_time;
  gulong sig_id;
} chat_ready_ctx;

static chat_ready_ctx *
chat_ready_ctx_new (EmpathyChatManager *self,
    EmpathyTpChat *tp_chat,
    TpAccount *account,
    gint64 user_action_time)
{
  chat_ready_ctx *ctx = g_slice_new0 (chat_ready_ctx);

  ctx->self = g_object_ref (self);
  ctx->tp_chat = g_object_ref (tp_chat);
  ctx->account = g_object_ref (account);
  ctx->user_action_time = user_action_time;
  return ctx;
}

static void
chat_ready_ctx_free (chat_ready_ctx *ctx)
{
  g_object_unref (ctx->self);
  g_object_unref (ctx->tp_chat);
  g_object_unref (ctx->account);

  if (ctx->sig_id != 0)
    g_signal_handler_disconnect (ctx->tp_chat, ctx->sig_id);

  g_slice_free (chat_ready_ctx, ctx);
}

static void
tp_chat_ready_cb (GObject *object,
  GParamSpec *spec,
  gpointer user_data)
{
  EmpathyTpChat *tp_chat = EMPATHY_TP_CHAT (object);
  chat_ready_ctx *ctx = user_data;

  if (!empathy_tp_chat_is_ready (tp_chat))
    return;

  process_tp_chat (ctx->self, tp_chat, ctx->account, ctx->user_action_time);

  chat_ready_ctx_free (ctx);
}

static void
handle_channels (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  EmpathyChatManager *self = (EmpathyChatManager *) user_data;
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      EmpathyTpChat *tp_chat;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      if (!TP_IS_TEXT_CHANNEL (channel))
        {
          DEBUG ("Channel %s doesn't implement Messages; can't handle it",
              tp_proxy_get_object_path (channel));
          continue;
        }

      DEBUG ("Now handling channel %s", tp_proxy_get_object_path (channel));

      tp_chat = empathy_tp_chat_new (account, channel);

      if (empathy_tp_chat_is_ready (tp_chat))
        {
          process_tp_chat (self, tp_chat, account, user_action_time);
        }
      else
        {
          chat_ready_ctx *ctx = chat_ready_ctx_new (self, tp_chat, account,
              user_action_time);

          ctx->sig_id = g_signal_connect (tp_chat, "notify::ready",
              G_CALLBACK (tp_chat_ready_cb), ctx);
        }
    }

  tp_handle_channels_context_accept (context);
}

static void
empathy_chat_manager_init (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  TpDBusDaemon *dbus;
  GError *error = NULL;

  priv->closed_queue = g_queue_new ();
  priv->messages = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) g_hash_table_unref);

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    {
      g_critical ("Failed to get D-Bus daemon: %s", error->message);
      g_error_free (error);
      return;
    }

  priv->chatroom_mgr = empathy_chatroom_manager_dup_singleton (NULL);

  /* Text channels handler */
  priv->handler = tp_simple_handler_new (dbus, FALSE, FALSE,
      EMPATHY_CHAT_BUS_NAME_SUFFIX, FALSE, handle_channels, self, NULL);

  /* EmpathyTpChat relies on these features being prepared */
  tp_base_client_add_connection_features_varargs (priv->handler,
    TP_CONNECTION_FEATURE_CAPABILITIES, 0);
  tp_base_client_add_channel_features_varargs (priv->handler,
      TP_CHANNEL_FEATURE_CHAT_STATES, 0);

  g_object_unref (dbus);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_ROOM,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_NONE,
        NULL));

  if (!tp_base_client_register (priv->handler, &error))
    {
      g_critical ("Failed to register text handler: %s", error->message);
      g_error_free (error);
    }
}

static void
empathy_chat_manager_finalize (GObject *object)
{
  EmpathyChatManager *self = EMPATHY_CHAT_MANAGER (object);
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  if (priv->closed_queue != NULL)
    {
      g_queue_foreach (priv->closed_queue, (GFunc) chat_data_free, NULL);
      g_queue_free (priv->closed_queue);
      priv->closed_queue = NULL;
    }

  tp_clear_pointer (&priv->messages, g_hash_table_unref);

  tp_clear_object (&priv->handler);
  tp_clear_object (&priv->chatroom_mgr);

  G_OBJECT_CLASS (empathy_chat_manager_parent_class)->finalize (object);
}

static GObject *
empathy_chat_manager_constructor (GType type,
    guint n_construct_params,
    GObjectConstructParam *construct_params)
{
  GObject *retval;

  if (!chat_manager_singleton)
    {
      retval = G_OBJECT_CLASS (empathy_chat_manager_parent_class)->constructor
        (type, n_construct_params, construct_params);

      chat_manager_singleton = EMPATHY_CHAT_MANAGER (retval);
      g_object_add_weak_pointer (retval, (gpointer) &chat_manager_singleton);
    }
  else
    {
      retval = g_object_ref (chat_manager_singleton);
    }

  return retval;
}

static void
empathy_chat_manager_class_init (
  EmpathyChatManagerClass *empathy_chat_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_chat_manager_class);

  object_class->finalize = empathy_chat_manager_finalize;
  object_class->constructor = empathy_chat_manager_constructor;

  signals[CLOSED_CHATS_CHANGED] =
    g_signal_new ("closed-chats-changed",
        G_TYPE_FROM_CLASS (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE,
        1, G_TYPE_UINT, NULL);

  signals[DISPLAYED_CHATS_CHANGED] =
    g_signal_new ("displayed-chats-changed",
        G_TYPE_FROM_CLASS (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_VOID__UINT,
        G_TYPE_NONE,
        1, G_TYPE_UINT, NULL);

  g_type_class_add_private (empathy_chat_manager_class,
    sizeof (EmpathyChatManagerPriv));
}

EmpathyChatManager *
empathy_chat_manager_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_CHAT_MANAGER, NULL);
}

void
empathy_chat_manager_closed_chat (EmpathyChatManager *self,
    EmpathyChat *chat)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  ChatData *data;
  GHashTable *chats;
  gchar *message;

  data = chat_data_new (chat);

  DEBUG ("Adding %s to closed queue: %s",
      data->room ? "room" : "contact", data->id);

  g_queue_push_tail (priv->closed_queue, data);

  g_signal_emit (self, signals[CLOSED_CHATS_CHANGED], 0,
      g_queue_get_length (priv->closed_queue));

  /* If there was a message saved from last time it was closed
   * (perhaps by accident?) save it to our hash table so it can be
   * used again when the same chat pops up. Hot. */
  message = empathy_chat_dup_text (chat);

  chats = g_hash_table_lookup (priv->messages,
      tp_proxy_get_object_path (data->account));

  /* Don't create a new hash table if we don't already have one and we
   * don't actually have a message to save. */
  if (chats == NULL && tp_str_empty (message))
    {
      g_free (message);
      return;
    }
  else if (chats == NULL && !tp_str_empty (message))
    {
      chats = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, g_free);

      g_hash_table_insert (priv->messages,
          g_strdup (tp_proxy_get_object_path (data->account)),
          chats);
    }

  if (tp_str_empty (message))
    {
      g_hash_table_remove (chats, data->id);
      /* might be '\0' */
      g_free (message);
    }
  else
    {
      /* takes ownership of message */
      g_hash_table_insert (chats, g_strdup (data->id), message);
    }
}

void
empathy_chat_manager_undo_closed_chat (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);
  ChatData *data;

  data = g_queue_pop_tail (priv->closed_queue);

  if (data == NULL)
    return;

  DEBUG ("Removing %s from closed queue and starting a chat with: %s",
      data->room ? "room" : "contact", data->id);

  if (data->room)
    empathy_join_muc (data->account, data->id,
        TP_USER_ACTION_TIME_NOT_USER_ACTION);
  else
    empathy_chat_with_contact_id (data->account, data->id,
        TP_USER_ACTION_TIME_NOT_USER_ACTION);

  g_signal_emit (self, signals[CLOSED_CHATS_CHANGED], 0,
      g_queue_get_length (priv->closed_queue));

  chat_data_free (data);
}

guint
empathy_chat_manager_get_num_closed_chats (EmpathyChatManager *self)
{
  EmpathyChatManagerPriv *priv = GET_PRIV (self);

  return g_queue_get_length (priv->closed_queue);
}

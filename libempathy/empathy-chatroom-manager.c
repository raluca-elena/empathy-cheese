/*
 * Copyright (C) 2004-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Martyn Russell <martyn@imendio.com>
 */

#include "config.h"

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libxml/parser.h>
#include <libxml/tree.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/simple-observer.h>
#include <telepathy-glib/util.h>

#include "empathy-tp-chat.h"
#include "empathy-chatroom-manager.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include "empathy-debug.h"

#define CHATROOMS_XML_FILENAME "chatrooms.xml"
#define CHATROOMS_DTD_FILENAME "empathy-chatroom-manager.dtd"
#define SAVE_TIMER 4

static EmpathyChatroomManager *chatroom_manager_singleton = NULL;

static void observe_channels_cb (TpSimpleObserver *observer,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GList *requests,
    TpObserveChannelsContext *context,
    gpointer user_data);

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyChatroomManager)
typedef struct
{
  GList *chatrooms;
  gchar *file;
  TpAccountManager *account_manager;

  /* source id of the autosave timer */
  gint save_timer_id;
  gboolean ready;

  TpBaseClient *observer;
} EmpathyChatroomManagerPriv;

enum {
  CHATROOM_ADDED,
  CHATROOM_REMOVED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

/* properties */
enum
{
  PROP_FILE = 1,
  PROP_READY,
  LAST_PROPERTY
};

G_DEFINE_TYPE (EmpathyChatroomManager, empathy_chatroom_manager, G_TYPE_OBJECT);

/*
 * API to save/load and parse the chatrooms file.
 */

static gboolean
chatroom_manager_file_save (EmpathyChatroomManager *manager)
{
  EmpathyChatroomManagerPriv *priv;
  xmlDocPtr doc;
  xmlNodePtr root;
  GList *l;

  priv = GET_PRIV (manager);

  doc = xmlNewDoc ((const xmlChar *) "1.0");
  root = xmlNewNode (NULL, (const xmlChar *) "chatrooms");
  xmlDocSetRootElement (doc, root);

  for (l = priv->chatrooms; l; l = l->next)
    {
      EmpathyChatroom *chatroom;
      xmlNodePtr       node;
      const gchar     *account_id;

      chatroom = l->data;

      if (!empathy_chatroom_is_favorite (chatroom))
        continue;

      account_id = tp_proxy_get_object_path (empathy_chatroom_get_account (
            chatroom));

      node = xmlNewChild (root, NULL, (const xmlChar *) "chatroom", NULL);
      xmlNewTextChild (node, NULL, (const xmlChar *) "name",
        (const xmlChar *) empathy_chatroom_get_name (chatroom));
      xmlNewTextChild (node, NULL, (const xmlChar *) "room",
        (const xmlChar *) empathy_chatroom_get_room (chatroom));
      xmlNewTextChild (node, NULL, (const xmlChar *) "account",
        (const xmlChar *) account_id);
      xmlNewTextChild (node, NULL, (const xmlChar *) "auto_connect",
        empathy_chatroom_get_auto_connect (chatroom) ?
        (const xmlChar *) "yes" : (const xmlChar *) "no");
      xmlNewTextChild (node, NULL, (const xmlChar *) "always_urgent",
        empathy_chatroom_is_always_urgent (chatroom) ?
        (const xmlChar *) "yes" : (const xmlChar *) "no");
    }

  /* Make sure the XML is indented properly */
  xmlIndentTreeOutput = 1;

  DEBUG ("Saving file:'%s'", priv->file);
  xmlSaveFormatFileEnc (priv->file, doc, "utf-8", 1);
  xmlFreeDoc (doc);

  xmlMemoryDump ();

  return TRUE;
}

static gboolean
save_timeout (EmpathyChatroomManager *self)
{
  EmpathyChatroomManagerPriv *priv = GET_PRIV (self);

  priv->save_timer_id = 0;
  chatroom_manager_file_save (self);

  return FALSE;
}

static void
reset_save_timeout (EmpathyChatroomManager *self)
{
  EmpathyChatroomManagerPriv *priv = GET_PRIV (self);

  if (priv->save_timer_id > 0)
    g_source_remove (priv->save_timer_id);

  priv->save_timer_id = g_timeout_add_seconds (SAVE_TIMER,
      (GSourceFunc) save_timeout, self);
}

static void
chatroom_changed_cb (EmpathyChatroom *chatroom,
    GParamSpec *spec,
    EmpathyChatroomManager *self)
{
  reset_save_timeout (self);
}

static void
add_chatroom (EmpathyChatroomManager *self,
    EmpathyChatroom *chatroom)
{
  EmpathyChatroomManagerPriv *priv = GET_PRIV (self);

  priv->chatrooms = g_list_prepend (priv->chatrooms, g_object_ref (chatroom));

  g_signal_connect (chatroom, "notify",
      G_CALLBACK (chatroom_changed_cb), self);
}

static void
chatroom_manager_parse_chatroom (EmpathyChatroomManager *manager,
    xmlNodePtr node)
{
  EmpathyChatroomManagerPriv *priv;
  EmpathyChatroom *chatroom;
  TpAccount *account;
  xmlNodePtr child;
  gchar *str;
  gchar *name;
  gchar *room;
  gchar *account_id;
  gboolean auto_connect;
  gboolean always_urgent;

  priv = GET_PRIV (manager);

  /* default values. */
  name = NULL;
  room = NULL;
  auto_connect = TRUE;
  always_urgent = FALSE;
  account_id = NULL;

  for (child = node->children; child; child = child->next)
    {
      gchar *tag;

      if (xmlNodeIsText (child))
        continue;

      tag = (gchar *) child->name;
      str = (gchar *) xmlNodeGetContent (child);

      if (strcmp (tag, "name") == 0)
        {
          name = g_strdup (str);
        }
      else if (strcmp (tag, "room") == 0)
        {
          room = g_strdup (str);
        }
      else if (strcmp (tag, "auto_connect") == 0)
        {
          if (strcmp (str, "yes") == 0)
            auto_connect = TRUE;
          else
            auto_connect = FALSE;
        }
      else if (!tp_strdiff (tag, "always_urgent"))
        {
          if (strcmp (str, "yes") == 0)
            always_urgent = TRUE;
          else
            always_urgent = FALSE;
        }
      else if (strcmp (tag, "account") == 0)
        {
          account_id = g_strdup (str);
        }

      xmlFree (str);
    }

  account = tp_account_manager_ensure_account (priv->account_manager,
    account_id);
  if (account == NULL)
    {
      g_free (name);
      g_free (room);
      g_free (account_id);
      return;
    }

  chatroom = empathy_chatroom_new_full (account, room, name, auto_connect);
  empathy_chatroom_set_favorite (chatroom, TRUE);
  empathy_chatroom_set_always_urgent (chatroom, always_urgent);
  add_chatroom (manager, chatroom);
  g_signal_emit (manager, signals[CHATROOM_ADDED], 0, chatroom);

  g_free (name);
  g_free (room);
  g_free (account_id);
}

static gboolean
chatroom_manager_file_parse (EmpathyChatroomManager *manager,
    const gchar *filename)
{
  EmpathyChatroomManagerPriv *priv;
  xmlParserCtxtPtr ctxt;
  xmlDocPtr doc;
  xmlNodePtr chatrooms;
  xmlNodePtr node;

  priv = GET_PRIV (manager);

  DEBUG ("Attempting to parse file:'%s'...", filename);

  ctxt = xmlNewParserCtxt ();

  /* Parse and validate the file. */
  doc = xmlCtxtReadFile (ctxt, filename, NULL, 0);
  if (doc == NULL)
    {
      g_warning ("Failed to parse file:'%s'", filename);
      xmlFreeParserCtxt (ctxt);
      return FALSE;
    }

  if (!empathy_xml_validate (doc, CHATROOMS_DTD_FILENAME))
    {
      g_warning ("Failed to validate file:'%s'", filename);
      xmlFreeDoc (doc);
      xmlFreeParserCtxt (ctxt);
      return FALSE;
    }

  /* The root node, chatrooms. */
  chatrooms = xmlDocGetRootElement (doc);

  for (node = chatrooms->children; node; node = node->next)
    {
      if (strcmp ((gchar *) node->name, "chatroom") == 0)
        chatroom_manager_parse_chatroom (manager, node);
    }

  DEBUG ("Parsed %d chatrooms", g_list_length (priv->chatrooms));

  xmlFreeDoc (doc);
  xmlFreeParserCtxt (ctxt);

  return TRUE;
}

static gboolean
chatroom_manager_get_all (EmpathyChatroomManager *manager)
{
  EmpathyChatroomManagerPriv *priv;

  priv = GET_PRIV (manager);

  /* read file in */
  if (g_file_test (priv->file, G_FILE_TEST_EXISTS) &&
      !chatroom_manager_file_parse (manager, priv->file))
    return FALSE;

  priv->ready = TRUE;
  g_object_notify (G_OBJECT (manager), "ready");

  return TRUE;
}

static void
empathy_chatroom_manager_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyChatroomManager *self = EMPATHY_CHATROOM_MANAGER (object);
  EmpathyChatroomManagerPriv *priv = GET_PRIV (self);

  switch (property_id)
    {
      case PROP_FILE:
        g_value_set_string (value, priv->file);
        break;
      case PROP_READY:
        g_value_set_boolean (value, priv->ready);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
empathy_chatroom_manager_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyChatroomManager *self = EMPATHY_CHATROOM_MANAGER (object);
  EmpathyChatroomManagerPriv *priv = GET_PRIV (self);

  switch (property_id)
    {
      case PROP_FILE:
        g_free (priv->file);
        priv->file = g_value_dup_string (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
chatroom_manager_dispose (GObject *object)
{
  EmpathyChatroomManagerPriv *priv;

  priv = GET_PRIV (object);

  tp_clear_object (&priv->observer);

  (G_OBJECT_CLASS (empathy_chatroom_manager_parent_class)->dispose) (object);
}

static void
chatroom_manager_finalize (GObject *object)
{
  EmpathyChatroomManager *self = EMPATHY_CHATROOM_MANAGER (object);
  EmpathyChatroomManagerPriv *priv;
  GList *l;

  priv = GET_PRIV (object);

  g_object_unref (priv->account_manager);

  if (priv->save_timer_id > 0)
    {
      /* have to save before destroy the object */
      g_source_remove (priv->save_timer_id);
      priv->save_timer_id = 0;
      chatroom_manager_file_save (self);
    }

  for (l = priv->chatrooms; l != NULL; l = g_list_next (l))
    {
      EmpathyChatroom *chatroom = l->data;

      g_signal_handlers_disconnect_by_func (chatroom, chatroom_changed_cb,
          self);

      g_object_unref (chatroom);
    }

  g_list_free (priv->chatrooms);
  g_free (priv->file);

  (G_OBJECT_CLASS (empathy_chatroom_manager_parent_class)->finalize) (object);
}

static void
account_manager_ready_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyChatroomManager *self = EMPATHY_CHATROOM_MANAGER (user_data);
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      goto out;
    }

  chatroom_manager_get_all (self);

out:
  g_object_unref (self);
}

static GObject *
empathy_chatroom_manager_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *obj;
  EmpathyChatroomManager *self;
  EmpathyChatroomManagerPriv *priv;
  GError *error = NULL;
  TpDBusDaemon *dbus;

  if (chatroom_manager_singleton != NULL)
    return g_object_ref (chatroom_manager_singleton);

  /* Parent constructor chain */
  obj = G_OBJECT_CLASS (empathy_chatroom_manager_parent_class)->
        constructor (type, n_props, props);

  self = EMPATHY_CHATROOM_MANAGER (obj);
  priv = GET_PRIV (self);

  priv->ready = FALSE;

  chatroom_manager_singleton = self;
  g_object_add_weak_pointer (obj, (gpointer) &chatroom_manager_singleton);

  priv->account_manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (priv->account_manager, NULL,
      account_manager_ready_cb, g_object_ref (self));

  if (priv->file == NULL)
    {
      /* Set the default file path */
      gchar *dir;

      dir = g_build_filename (g_get_user_config_dir (), PACKAGE_NAME, NULL);
      if (!g_file_test (dir, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR))
        g_mkdir_with_parents (dir, S_IRUSR | S_IWUSR | S_IXUSR);

      priv->file = g_build_filename (dir, CHATROOMS_XML_FILENAME, NULL);
      g_free (dir);
    }

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    {
      g_warning ("Failed to get TpDBusDaemon: %s", error->message);

      g_error_free (error);
      return obj;
    }

  /* Setup a room observer */
  priv->observer = tp_simple_observer_new (dbus, TRUE,
      "Empathy.ChatroomManager", TRUE, observe_channels_cb, self, NULL);

  g_object_unref (dbus);

  tp_base_client_take_observer_filter (priv->observer, tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_ROOM,
      NULL));

  tp_base_client_add_connection_features_varargs (priv->observer,
      TP_CONNECTION_FEATURE_CAPABILITIES, NULL);

  if (!tp_base_client_register (priv->observer, &error))
    {
      g_critical ("Failed to register Observer: %s", error->message);

      g_error_free (error);
    }

  return obj;
}

static void
empathy_chatroom_manager_class_init (EmpathyChatroomManagerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  object_class->constructor = empathy_chatroom_manager_constructor;
  object_class->get_property = empathy_chatroom_manager_get_property;
  object_class->set_property = empathy_chatroom_manager_set_property;
  object_class->dispose = chatroom_manager_dispose;
  object_class->finalize = chatroom_manager_finalize;

  param_spec = g_param_spec_string (
      "file",
      "path of the favorite file",
      "The path of the XML file containing user's favorites",
      NULL,
      G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_FILE, param_spec);

  param_spec = g_param_spec_boolean (
      "ready",
      "whether the manager is ready yet",
      "whether the manager is ready yet",
      FALSE,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_READY, param_spec);

  signals[CHATROOM_ADDED] = g_signal_new ("chatroom-added",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_CHATROOM);

  signals[CHATROOM_REMOVED] = g_signal_new ("chatroom-removed",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_CHATROOM);

  g_type_class_add_private (object_class, sizeof (EmpathyChatroomManagerPriv));
}

static void
empathy_chatroom_manager_init (EmpathyChatroomManager *manager)
{
  EmpathyChatroomManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
      EMPATHY_TYPE_CHATROOM_MANAGER, EmpathyChatroomManagerPriv);

  manager->priv = priv;
}

EmpathyChatroomManager *
empathy_chatroom_manager_dup_singleton (const gchar *file)
{
  return EMPATHY_CHATROOM_MANAGER (g_object_new (EMPATHY_TYPE_CHATROOM_MANAGER,
      "file", file, NULL));
}

gboolean
empathy_chatroom_manager_add (EmpathyChatroomManager *manager,
    EmpathyChatroom *chatroom)
{
  EmpathyChatroomManagerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_CHATROOM_MANAGER (manager), FALSE);
  g_return_val_if_fail (EMPATHY_IS_CHATROOM (chatroom), FALSE);

  priv = GET_PRIV (manager);

  /* don't add more than once */
  if (!empathy_chatroom_manager_find (manager,
      empathy_chatroom_get_account (chatroom),
      empathy_chatroom_get_room (chatroom)))
    {
      add_chatroom (manager, chatroom);

      if (empathy_chatroom_is_favorite (chatroom))
        reset_save_timeout (manager);

      g_signal_emit (manager, signals[CHATROOM_ADDED], 0, chatroom);
      return TRUE;
    }

  return FALSE;
}

static void
chatroom_manager_remove_link (EmpathyChatroomManager *manager,
    GList *l)
{
  EmpathyChatroomManagerPriv *priv;
  EmpathyChatroom *chatroom;

  priv = GET_PRIV (manager);

  chatroom = l->data;

  if (empathy_chatroom_is_favorite (chatroom))
    reset_save_timeout (manager);

  priv->chatrooms = g_list_delete_link (priv->chatrooms, l);

  g_signal_emit (manager, signals[CHATROOM_REMOVED], 0, chatroom);
  g_signal_handlers_disconnect_by_func (chatroom, chatroom_changed_cb, manager);

  g_object_unref (chatroom);
}

void
empathy_chatroom_manager_remove (EmpathyChatroomManager *manager,
    EmpathyChatroom        *chatroom)
{
  EmpathyChatroomManagerPriv *priv;
  GList *l;

  g_return_if_fail (EMPATHY_IS_CHATROOM_MANAGER (manager));
  g_return_if_fail (EMPATHY_IS_CHATROOM (chatroom));

  priv = GET_PRIV (manager);

  for (l = priv->chatrooms; l; l = l->next)
    {
      EmpathyChatroom *this_chatroom;

      this_chatroom = l->data;

      if (this_chatroom == chatroom ||
          empathy_chatroom_equal (chatroom, this_chatroom))
        {
          chatroom_manager_remove_link (manager, l);
          break;
        }
    }
}

EmpathyChatroom *
empathy_chatroom_manager_find (EmpathyChatroomManager *manager,
    TpAccount *account,
    const gchar *room)
{
  EmpathyChatroomManagerPriv *priv;
  GList *l;

  g_return_val_if_fail (EMPATHY_IS_CHATROOM_MANAGER (manager), NULL);
  g_return_val_if_fail (room != NULL, NULL);

  priv = GET_PRIV (manager);

  for (l = priv->chatrooms; l; l = l->next)
    {
      EmpathyChatroom *chatroom;
      TpAccount *this_account;
      const gchar    *this_room;

      chatroom = l->data;
      this_account = empathy_chatroom_get_account (chatroom);
      this_room = empathy_chatroom_get_room (chatroom);

      if (this_account && this_room && account == this_account
          && strcmp (this_room, room) == 0)
        return chatroom;
    }

  return NULL;
}

EmpathyChatroom *
empathy_chatroom_manager_ensure_chatroom (EmpathyChatroomManager *manager,
    TpAccount *account,
    const gchar *room,
    const gchar *name)
{
  EmpathyChatroom *chatroom;

  chatroom = empathy_chatroom_manager_find (manager, account, room);

  if (chatroom)
    {
      return g_object_ref (chatroom);
    }
  else
    {
      chatroom = empathy_chatroom_new_full (account,
        room,
        name,
        FALSE);
      empathy_chatroom_manager_add (manager, chatroom);
      return chatroom;
    }
}

GList *
empathy_chatroom_manager_get_chatrooms (EmpathyChatroomManager *manager,
    TpAccount *account)
{
  EmpathyChatroomManagerPriv *priv;
  GList *chatrooms, *l;

  g_return_val_if_fail (EMPATHY_IS_CHATROOM_MANAGER (manager), NULL);

  priv = GET_PRIV (manager);

  if (!account)
    return g_list_copy (priv->chatrooms);

  chatrooms = NULL;
  for (l = priv->chatrooms; l; l = l->next)
    {
      EmpathyChatroom *chatroom;

      chatroom = l->data;

      if (account == empathy_chatroom_get_account (chatroom))
        chatrooms = g_list_append (chatrooms, chatroom);
    }

  return chatrooms;
}

static void
chatroom_manager_chat_destroyed_cb (EmpathyTpChat *chat,
  gpointer manager)
{
  EmpathyChatroomManagerPriv *priv = GET_PRIV (manager);
  GList *l;

  for (l = priv->chatrooms; l; l = l->next)
    {
      EmpathyChatroom *chatroom = l->data;

      if (empathy_chatroom_get_tp_chat (chatroom) != chat)
        continue;

      empathy_chatroom_set_tp_chat (chatroom, NULL);

      if (!empathy_chatroom_is_favorite (chatroom))
        {
          /* Remove the chatroom from the list, unless it's in the list of
           * favourites..
           * FIXME this policy should probably not be in libempathy */
          chatroom_manager_remove_link (manager, l);
        }

      break;
    }
}

static void
observe_channels_cb (TpSimpleObserver *observer,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GList *requests,
    TpObserveChannelsContext *context,
    gpointer user_data)
{
  EmpathyChatroomManager *self = user_data;
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      EmpathyTpChat *tp_chat;
      const gchar *roomname;
      EmpathyChatroom *chatroom;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      tp_chat = empathy_tp_chat_new (account, channel);
      roomname = empathy_tp_chat_get_id (tp_chat);
      chatroom = empathy_chatroom_manager_find (self, account, roomname);

      if (chatroom == NULL)
        {
          chatroom = empathy_chatroom_new_full (account, roomname, roomname,
            FALSE);
          empathy_chatroom_manager_add (self, chatroom);
          g_object_unref (chatroom);
        }

      empathy_chatroom_set_tp_chat (chatroom, tp_chat);
      g_object_unref (tp_chat);

      /* A TpChat is always destroyed as it only gets unreffed after the channel
       * has been invalidated in the dispatcher..  */
      g_signal_connect (tp_chat, "destroy",
        G_CALLBACK (chatroom_manager_chat_destroyed_cb),
        self);
    }

  tp_observe_channels_context_accept (context);
}

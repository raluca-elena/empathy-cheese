/*
 * empathy-call-factory.c - Source for EmpathyCallFactory
 * Copyright (C) 2008 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
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


#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/account-channel-request.h>
#include <telepathy-glib/simple-handler.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include <telepathy-yell/telepathy-yell.h>

#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-utils.h>

#include "empathy-call-factory.h"
#include "empathy-call-handler.h"
#include "src-marshal.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE(EmpathyCallFactory, empathy_call_factory, G_TYPE_OBJECT)

static void handle_channels_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data);

/* signal enum */
enum
{
    NEW_CALL_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct {
  TpBaseClient *handler;
  gboolean dispose_has_run;
} EmpathyCallFactoryPriv;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyCallFactory)

static GObject *call_factory = NULL;

static void
empathy_call_factory_init (EmpathyCallFactory *obj)
{
  EmpathyCallFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_CALL_FACTORY, EmpathyCallFactoryPriv);
  TpAccountManager *am;

  obj->priv = priv;

  am = tp_account_manager_dup ();

  priv->handler = tp_simple_handler_new_with_am (am, FALSE, FALSE,
      EMPATHY_CALL_BUS_NAME_SUFFIX, FALSE, handle_channels_cb, obj, NULL);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TPY_IFACE_CHANNEL_TYPE_CALL,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TPY_IFACE_CHANNEL_TYPE_CALL,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, G_TYPE_BOOLEAN, TRUE,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TPY_IFACE_CHANNEL_TYPE_CALL,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, G_TYPE_BOOLEAN, TRUE,
        NULL));

  tp_base_client_add_handler_capabilities_varargs (priv->handler,
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/ice-udp",
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/gtalk-p2p",
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/video/h264",
    NULL);

  g_object_unref (am);
}

static GObject *
empathy_call_factory_constructor (GType type, guint n_construct_params,
  GObjectConstructParam *construct_params)
{
  g_return_val_if_fail (call_factory == NULL, NULL);

  call_factory = G_OBJECT_CLASS (empathy_call_factory_parent_class)->constructor
          (type, n_construct_params, construct_params);
  g_object_add_weak_pointer (call_factory, (gpointer)&call_factory);

  return call_factory;
}

static void
empathy_call_factory_finalize (GObject *object)
{
  /* free any data held directly by the object here */

  if (G_OBJECT_CLASS (empathy_call_factory_parent_class)->finalize)
    G_OBJECT_CLASS (empathy_call_factory_parent_class)->finalize (object);
}

static void
empathy_call_factory_dispose (GObject *object)
{
  EmpathyCallFactoryPriv *priv = GET_PRIV (object);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->handler);

  if (G_OBJECT_CLASS (empathy_call_factory_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_call_factory_parent_class)->dispose (object);
}

static void
empathy_call_factory_class_init (
  EmpathyCallFactoryClass *empathy_call_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_call_factory_class);

  g_type_class_add_private (empathy_call_factory_class,
    sizeof (EmpathyCallFactoryPriv));

  object_class->constructor = empathy_call_factory_constructor;
  object_class->dispose = empathy_call_factory_dispose;
  object_class->finalize = empathy_call_factory_finalize;

  signals[NEW_CALL_HANDLER] =
    g_signal_new ("new-call-handler",
      G_TYPE_FROM_CLASS (empathy_call_factory_class),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      _src_marshal_VOID__OBJECT_BOOLEAN,
      G_TYPE_NONE,
      2, EMPATHY_TYPE_CALL_HANDLER, G_TYPE_BOOLEAN);
}

EmpathyCallFactory *
empathy_call_factory_initialise (void)
{
  g_return_val_if_fail (call_factory == NULL, NULL);

  return EMPATHY_CALL_FACTORY (g_object_new (EMPATHY_TYPE_CALL_FACTORY, NULL));
}

EmpathyCallFactory *
empathy_call_factory_get (void)
{
  g_return_val_if_fail (call_factory != NULL, NULL);

  return EMPATHY_CALL_FACTORY (call_factory);
}

static void
call_channel_got_contact (TpConnection *connection,
  EmpathyContact *contact,
  const GError *error,
  gpointer user_data,
  GObject *weak_object)
{
  EmpathyCallFactory *factory = EMPATHY_CALL_FACTORY (weak_object);
  EmpathyCallHandler *handler;
  TpyCallChannel *call = TPY_CALL_CHANNEL (user_data);

  if (contact == NULL)
    {
      /* FIXME use hangup with an appropriate error */
      tp_channel_close_async (TP_CHANNEL (call), NULL, NULL);
      return;
    }

  handler = empathy_call_handler_new_for_channel (call, contact);

  g_signal_emit (factory, signals[NEW_CALL_HANDLER], 0,
    handler, FALSE);

  g_object_unref (handler);
}

static void
call_channel_ready (EmpathyCallFactory *factory,
  TpyCallChannel *call)
{
  TpChannel *channel = TP_CHANNEL (call);
  const gchar *id;

  id = tp_channel_get_identifier (channel);

  /* The ready callback has a reference, so pass that on */
  empathy_tp_contact_factory_get_from_id (
    tp_channel_borrow_connection (channel),
    id,
    call_channel_got_contact,
    channel,
    g_object_unref,
    (GObject *) factory);
}

static void
call_channel_ready_cb (TpyCallChannel *call,
  GParamSpec *spec,
  EmpathyCallFactory *factory)
{
  gboolean ready;

  g_object_get (call, "ready", &ready, NULL);
  if (!ready)
    return;

  call_channel_ready (factory, call);
}


static void
handle_channels_cb (TpSimpleHandler *handler,
    TpAccount *account,
    TpConnection *connection,
    GList *channels,
    GList *requests_satisfied,
    gint64 user_action_time,
    TpHandleChannelsContext *context,
    gpointer user_data)
{
  EmpathyCallFactory *self = user_data;
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      TpyCallChannel *call;
      gboolean ready;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      if (tp_channel_get_channel_type_id (channel) !=
          TPY_IFACE_QUARK_CHANNEL_TYPE_CALL)
        continue;

      if (!TPY_IS_CALL_CHANNEL (channel))
        continue;

      call = TPY_CALL_CHANNEL (channel);

      /* Take a ref to keep while hopping through the async callbacks */
      g_object_ref (call);
      g_object_get (call, "ready", &ready, NULL);

      if (!ready)
        tp_g_signal_connect_object (call, "notify::ready",
          G_CALLBACK (call_channel_ready_cb), self, 0);
      else
        call_channel_ready (self, call);
    }

  tp_handle_channels_context_accept (context);
}

gboolean
empathy_call_factory_register (EmpathyCallFactory *self,
    GError **error)
{
  EmpathyCallFactoryPriv *priv = GET_PRIV (self);

  return tp_base_client_register (priv->handler, error);
}

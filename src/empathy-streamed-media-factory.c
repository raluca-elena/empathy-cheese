/*
 * empathy-streamed-media-factory.c - Source for EmpathyStreamedMediaFactory
 * Copyright (C) 2008-2011 Collabora Ltd.
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

#include <libempathy/empathy-utils.h>

#include "empathy-streamed-media-factory.h"
#include "empathy-streamed-media-handler.h"
#include "src-marshal.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE(EmpathyStreamedMediaFactory, empathy_streamed_media_factory, G_TYPE_OBJECT)

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
    NEW_STREAMED_MEDIA_HANDLER,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

/* private structure */
typedef struct {
  TpBaseClient *handler;
  gboolean dispose_has_run;
} EmpathyStreamedMediaFactoryPriv;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyStreamedMediaFactory)

static GObject *call_factory = NULL;

static void
empathy_streamed_media_factory_init (EmpathyStreamedMediaFactory *obj)
{
  EmpathyStreamedMediaFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_STREAMED_MEDIA_FACTORY, EmpathyStreamedMediaFactoryPriv);
  TpDBusDaemon *dbus;
  GError *error = NULL;

  obj->priv = priv;

  dbus = tp_dbus_daemon_dup (&error);
  if (dbus == NULL)
    {
      g_warning ("Failed to get TpDBusDaemon: %s", error->message);
      g_error_free (error);
      return;
    }

  priv->handler = tp_simple_handler_new (dbus, FALSE, FALSE,
      "Empathy.AudioVideo", FALSE, handle_channels_cb, obj, NULL);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, G_TYPE_BOOLEAN, TRUE,
        NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, TP_HANDLE_TYPE_CONTACT,
        TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO, G_TYPE_BOOLEAN, TRUE,
        NULL));

  tp_base_client_add_handler_capabilities_varargs (priv->handler,
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/ice-udp",
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/gtalk-p2p",
    "org.freedesktop.Telepathy.Channel.Interface.MediaSignalling/video/h264",
    NULL);

  g_object_unref (dbus);
}

static GObject *
empathy_streamed_media_factory_constructor (GType type, guint n_construct_params,
  GObjectConstructParam *construct_params)
{
  g_return_val_if_fail (call_factory == NULL, NULL);

  call_factory = G_OBJECT_CLASS (empathy_streamed_media_factory_parent_class)->constructor
          (type, n_construct_params, construct_params);
  g_object_add_weak_pointer (call_factory, (gpointer)&call_factory);

  return call_factory;
}

static void
empathy_streamed_media_factory_finalize (GObject *object)
{
  /* free any data held directly by the object here */

  if (G_OBJECT_CLASS (empathy_streamed_media_factory_parent_class)->finalize)
    G_OBJECT_CLASS (empathy_streamed_media_factory_parent_class)->finalize (object);
}

static void
empathy_streamed_media_factory_dispose (GObject *object)
{
  EmpathyStreamedMediaFactoryPriv *priv = GET_PRIV (object);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  tp_clear_object (&priv->handler);

  if (G_OBJECT_CLASS (empathy_streamed_media_factory_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_streamed_media_factory_parent_class)->dispose (object);
}

static void
empathy_streamed_media_factory_class_init (
  EmpathyStreamedMediaFactoryClass *empathy_streamed_media_factory_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_streamed_media_factory_class);

  g_type_class_add_private (empathy_streamed_media_factory_class,
    sizeof (EmpathyStreamedMediaFactoryPriv));

  object_class->constructor = empathy_streamed_media_factory_constructor;
  object_class->dispose = empathy_streamed_media_factory_dispose;
  object_class->finalize = empathy_streamed_media_factory_finalize;

  signals[NEW_STREAMED_MEDIA_HANDLER] =
    g_signal_new ("new-streamed-media-handler",
      G_TYPE_FROM_CLASS (empathy_streamed_media_factory_class),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      _src_marshal_VOID__OBJECT_BOOLEAN,
      G_TYPE_NONE,
      2, EMPATHY_TYPE_STREAMED_MEDIA_HANDLER, G_TYPE_BOOLEAN);
}

EmpathyStreamedMediaFactory *
empathy_streamed_media_factory_initialise (void)
{
  g_return_val_if_fail (call_factory == NULL, NULL);

  return EMPATHY_STREAMED_MEDIA_FACTORY (g_object_new (EMPATHY_TYPE_STREAMED_MEDIA_FACTORY, NULL));
}

EmpathyStreamedMediaFactory *
empathy_streamed_media_factory_get (void)
{
  g_return_val_if_fail (call_factory != NULL, NULL);

  return EMPATHY_STREAMED_MEDIA_FACTORY (call_factory);
}

static void
create_streamed_media_handler (EmpathyStreamedMediaFactory *factory,
  EmpathyTpStreamedMedia *call)
{
  EmpathyStreamedMediaHandler *handler;

  g_return_if_fail (factory != NULL);

  handler = empathy_streamed_media_handler_new_for_channel (call);

  g_signal_emit (factory, signals[NEW_STREAMED_MEDIA_HANDLER], 0,
    handler, FALSE);

  g_object_unref (handler);
}

static void
call_status_changed_cb (EmpathyTpStreamedMedia *call,
    GParamSpec *spec,
    EmpathyStreamedMediaFactory *self)
{
  if (empathy_tp_streamed_media_get_status (call) <= EMPATHY_TP_STREAMED_MEDIA_STATUS_READYING)
    return;

  create_streamed_media_handler (self, call);

  g_signal_handlers_disconnect_by_func (call, call_status_changed_cb, self);
  g_object_unref (call);
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
  EmpathyStreamedMediaFactory *self = user_data;
  GList *l;

  for (l = channels; l != NULL; l = g_list_next (l))
    {
      TpChannel *channel = l->data;
      EmpathyTpStreamedMedia *call;

      if (tp_proxy_get_invalidated (channel) != NULL)
        continue;

      if (tp_channel_get_channel_type_id (channel) !=
          TP_IFACE_QUARK_CHANNEL_TYPE_STREAMED_MEDIA)
        continue;

      call = empathy_tp_streamed_media_new (account, channel);

      if (empathy_tp_streamed_media_get_status (call) <= EMPATHY_TP_STREAMED_MEDIA_STATUS_READYING)
        {
          /* We have to wait that the TpStreamedMedia is ready as the
           * call-handler rely on it. */
          tp_g_signal_connect_object (call, "notify::status",
              G_CALLBACK (call_status_changed_cb), self, 0);
          continue;
        }

      create_streamed_media_handler (self, call);
      g_object_unref (call);
    }

  tp_handle_channels_context_accept (context);
}

gboolean
empathy_streamed_media_factory_register (EmpathyStreamedMediaFactory *self,
    GError **error)
{
  EmpathyStreamedMediaFactoryPriv *priv = GET_PRIV (self);

  return tp_base_client_register (priv->handler, error);
}

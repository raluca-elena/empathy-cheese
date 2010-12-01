/*
 * empathy-auth-factory.c - Source for EmpathyAuthFactory
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#include "empathy-auth-factory.h"

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/simple-handler.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include "empathy-debug.h"
#include "empathy-server-tls-handler.h"
#include "empathy-utils.h"

#include "extensions/extensions.h"

G_DEFINE_TYPE (EmpathyAuthFactory, empathy_auth_factory, G_TYPE_OBJECT);

typedef struct {
  TpBaseClient *handler;

  gboolean dispose_run;
} EmpathyAuthFactoryPriv;

enum {
  NEW_SERVER_TLS_HANDLER,
  NEW_SERVER_SASL_HANDLER,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = { 0, };

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAuthFactory)

static EmpathyAuthFactory *auth_factory_singleton = NULL;

typedef struct {
  TpHandleChannelsContext *context;
  EmpathyAuthFactory *self;
} HandlerContextData;

static void
handler_context_data_free (HandlerContextData *data)
{
  tp_clear_object (&data->self);
  tp_clear_object (&data->context);

  g_slice_free (HandlerContextData, data);
}

static HandlerContextData *
handler_context_data_new (EmpathyAuthFactory *self,
    TpHandleChannelsContext *context)
{
  HandlerContextData *data;

  data = g_slice_new0 (HandlerContextData);
  data->self = g_object_ref (self);
  data->context = g_object_ref (context);

  return data;
}

static void
server_tls_handler_ready_cb (GObject *source,
    GAsyncResult *res,
    gpointer user_data)
{
  EmpathyServerTLSHandler *handler;
  GError *error = NULL;
  EmpathyAuthFactoryPriv *priv;
  HandlerContextData *data = user_data;

  priv = GET_PRIV (data->self);
  handler = empathy_server_tls_handler_new_finish (res, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to create a server TLS handler; error %s",
          error->message);
      tp_handle_channels_context_fail (data->context, error);

      g_error_free (error);
    }
  else
    {
      tp_handle_channels_context_accept (data->context);
      g_signal_emit (data->self, signals[NEW_SERVER_TLS_HANDLER], 0,
          handler);

      g_object_unref (handler);
    }

  handler_context_data_free (data);
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
  TpChannel *channel;
  const GError *dbus_error;
  GError *error = NULL;
  EmpathyAuthFactory *self = user_data;
  HandlerContextData *data;

  DEBUG ("Handle TLS carrier channels.");

  /* there can't be more than one ServerTLSConnection channels
   * at the same time, for the same connection/account.
   */
  if (g_list_length (channels) != 1)
    {
      g_set_error_literal (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Can't handle more than one ServerTLSConnection channel "
          "for the same connection.");

      goto error;
    }

  channel = channels->data;

  if (tp_channel_get_channel_type_id (channel) !=
      EMP_IFACE_QUARK_CHANNEL_TYPE_SERVER_TLS_CONNECTION)
    {
      g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Can only handle ServerTLSConnection channels, this was a %s "
          "channel", tp_channel_get_channel_type (channel));

      goto error;
    }

  dbus_error = tp_proxy_get_invalidated (channel);

  if (dbus_error != NULL)
    {
      error = g_error_copy (dbus_error);
      goto error;
    }

  /* create a handler */
  data = handler_context_data_new (self, context);
  tp_handle_channels_context_delay (context);
  empathy_server_tls_handler_new_async (channel, server_tls_handler_ready_cb,
      data);

  return;

 error:
  tp_handle_channels_context_fail (context, error);
  g_clear_error (&error);
}

static GObject *
empathy_auth_factory_constructor (GType type,
    guint n_params,
    GObjectConstructParam *params)
{
  GObject *retval;

  if (auth_factory_singleton != NULL)
    {
      retval = g_object_ref (auth_factory_singleton);
    }
  else
    {
      retval = G_OBJECT_CLASS (empathy_auth_factory_parent_class)->constructor
        (type, n_params, params);

      auth_factory_singleton = EMPATHY_AUTH_FACTORY (retval);
      g_object_add_weak_pointer (retval, (gpointer *) &auth_factory_singleton);
    }

  return retval;
}

static void
empathy_auth_factory_init (EmpathyAuthFactory *self)
{
  EmpathyAuthFactoryPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_AUTH_FACTORY, EmpathyAuthFactoryPriv);
  TpDBusDaemon *bus;
  GError *error = NULL;

  self->priv = priv;

  bus = tp_dbus_daemon_dup (&error);
  if (error != NULL)
    {
      g_critical ("Failed to get TpDBusDaemon: %s", error->message);
      g_error_free (error);
      return;
    }

  priv->handler = tp_simple_handler_new (bus, FALSE, FALSE, "Empathy.Auth",
      FALSE, handle_channels_cb, self, NULL);

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
          /* ChannelType */
          TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          EMP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION,
          /* AuthenticationMethod */
          TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_NONE, NULL));

  tp_base_client_take_handler_filter (priv->handler, tp_asv_new (
          /* ChannelType */
          TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_SERVER_AUTHENTICATION,
          /* AuthenticationMethod */
          TP_PROP_CHANNEL_TYPE_SERVER_AUTHENTICATION_AUTHENTICATION_METHOD,
          G_TYPE_STRING, TP_IFACE_CHANNEL_INTERFACE_SASL_AUTHENTICATION,
          NULL));

 g_object_unref (bus);
}

static void
empathy_auth_factory_dispose (GObject *object)
{
  EmpathyAuthFactoryPriv *priv = GET_PRIV (object);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  tp_clear_object (&priv->handler);

  G_OBJECT_CLASS (empathy_auth_factory_parent_class)->dispose (object);
}

static void
empathy_auth_factory_class_init (EmpathyAuthFactoryClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->constructor = empathy_auth_factory_constructor;
  oclass->dispose = empathy_auth_factory_dispose;

  g_type_class_add_private (klass, sizeof (EmpathyAuthFactoryPriv));

  signals[NEW_SERVER_TLS_HANDLER] =
    g_signal_new ("new-server-tls-handler",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_SERVER_TLS_HANDLER);

  signals[NEW_SERVER_SASL_HANDLER] =
    g_signal_new ("new-server-sasl-handler",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, EMPATHY_TYPE_SERVER_SASL_HANDLER);
}

EmpathyAuthFactory *
empathy_auth_factory_dup_singleton (void)
{
  return g_object_new (EMPATHY_TYPE_AUTH_FACTORY, NULL);
}

gboolean
empathy_auth_factory_register (EmpathyAuthFactory *self,
    GError **error)
{
  EmpathyAuthFactoryPriv *priv = GET_PRIV (self);

  return tp_base_client_register (priv->handler, error);
}

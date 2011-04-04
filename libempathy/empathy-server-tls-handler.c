/*
 * empathy-server-tls-handler.c - Source for EmpathyServerTLSHandler
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

#include "empathy-server-tls-handler.h"

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include "empathy-debug.h"
#include "empathy-tls-certificate.h"
#include "empathy-utils.h"

#include "extensions/extensions.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

enum {
  PROP_CHANNEL = 1,
  PROP_TLS_CERTIFICATE,
  PROP_HOSTNAME,
  PROP_REFERENCE_IDENTITIES,
  LAST_PROPERTY,
};

typedef struct {
  TpChannel *channel;

  EmpathyTLSCertificate *certificate;
  gchar *hostname;
  gchar **reference_identities;

  GSimpleAsyncResult *async_init_res;
} EmpathyServerTLSHandlerPriv;

G_DEFINE_TYPE_WITH_CODE (EmpathyServerTLSHandler, empathy_server_tls_handler,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init));

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyServerTLSHandler);

static void
tls_certificate_prepared_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyTLSCertificate *certificate = EMPATHY_TLS_CERTIFICATE (source);
  EmpathyServerTLSHandler *self = user_data;
  GError *error = NULL;
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (self);

  empathy_tls_certificate_prepare_finish (certificate, result, &error);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (priv->async_init_res, error);
      g_error_free (error);
    }

  g_simple_async_result_complete_in_idle (priv->async_init_res);
  tp_clear_object (&priv->async_init_res);
}

static gboolean
tls_handler_init_finish (GAsyncInitable *initable,
    GAsyncResult *res,
    GError **error)
{
  gboolean retval = TRUE;

  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
          error))
    retval = FALSE;

  return retval;
}

static void
tls_handler_init_async (GAsyncInitable *initable,
    gint io_priority,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GHashTable *properties;
  const gchar *cert_object_path;
  const gchar *hostname;
  const gchar * const *identities;
  const gchar *bus_name;
  TpDBusDaemon *dbus;
  GError *error = NULL;
  /*
   * Used when channel doesn't implement ReferenceIdentities. A GStrv
   * with [0] the hostname, and [1] a NULL terminator.
   */
  gchar *default_identities[2];
  EmpathyServerTLSHandler *self = EMPATHY_SERVER_TLS_HANDLER (initable);
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (self);

  g_assert (priv->channel != NULL);

  priv->async_init_res = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, empathy_server_tls_handler_new_async);
  properties = tp_channel_borrow_immutable_properties (priv->channel);

  hostname = tp_asv_get_string (properties,
     TP_PROP_CHANNEL_TYPE_SERVER_TLS_CONNECTION_HOSTNAME);
  priv->hostname = g_strdup (hostname);

  DEBUG ("Received hostname: %s", hostname);

  identities = tp_asv_get_strv (properties,
      TP_PROP_CHANNEL_TYPE_SERVER_TLS_CONNECTION_REFERENCE_IDENTITIES);

  /*
   * If the channel doesn't implement the ReferenceIdentities parameter
   * then fallback to the hostname.
   */
  if (identities == NULL)
    {
      default_identities[0] = (gchar *) hostname;
      default_identities[1] = NULL;
      identities = (const gchar **) default_identities;
    }
  else
    {
#ifdef ENABLE_DEBUG
      gchar *output = g_strjoinv (", ", (gchar **) identities);
      DEBUG ("Received reference identities: %s", output);
      g_free (output);
#endif /* ENABLE_DEBUG */
  }

  priv->reference_identities = g_strdupv ((gchar **) identities);

  cert_object_path = tp_asv_get_object_path (properties,
      EMP_IFACE_CHANNEL_TYPE_SERVER_TLS_CONNECTION ".ServerCertificate");
  bus_name = tp_proxy_get_bus_name (TP_PROXY (priv->channel));
  dbus = tp_proxy_get_dbus_daemon (TP_PROXY (priv->channel));

  DEBUG ("Creating an EmpathyTLSCertificate for path %s, bus name %s",
      cert_object_path, bus_name);

  priv->certificate = empathy_tls_certificate_new (dbus, bus_name,
      cert_object_path, &error);

  if (error != NULL)
    {
      DEBUG ("Unable to create the EmpathyTLSCertificate: error %s",
          error->message);

      g_simple_async_result_set_from_error (priv->async_init_res, error);
      g_simple_async_result_complete_in_idle (priv->async_init_res);

      g_error_free (error);
      tp_clear_object (&priv->async_init_res);

      return;
    }

  empathy_tls_certificate_prepare_async (priv->certificate,
      tls_certificate_prepared_cb, self);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = tls_handler_init_async;
  iface->init_finish = tls_handler_init_finish;
}

static void
empathy_server_tls_handler_finalize (GObject *object)
{
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (object);

  DEBUG ("%p", object);

  tp_clear_object (&priv->channel);
  tp_clear_object (&priv->certificate);
  g_strfreev (priv->reference_identities);
  g_free (priv->hostname);

  G_OBJECT_CLASS (empathy_server_tls_handler_parent_class)->finalize (object);
}

static void
empathy_server_tls_handler_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_TLS_CERTIFICATE:
      g_value_set_object (value, priv->certificate);
      break;
    case PROP_HOSTNAME:
      g_value_set_string (value, priv->hostname);
      break;
    case PROP_REFERENCE_IDENTITIES:
      g_value_set_boxed (value, priv->reference_identities);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_server_tls_handler_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_CHANNEL:
      priv->channel = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_server_tls_handler_class_init (EmpathyServerTLSHandlerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  oclass->get_property = empathy_server_tls_handler_get_property;
  oclass->set_property = empathy_server_tls_handler_set_property;
  oclass->finalize = empathy_server_tls_handler_finalize;

  g_type_class_add_private (klass, sizeof (EmpathyServerTLSHandlerPriv));

  pspec = g_param_spec_object ("channel", "The TpChannel",
      "The TpChannel this handler is supposed to handle.",
      TP_TYPE_CHANNEL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CHANNEL, pspec);

  pspec = g_param_spec_object ("certificate", "The EmpathyTLSCertificate",
      "The EmpathyTLSCertificate carried by the channel.",
      EMPATHY_TYPE_TLS_CERTIFICATE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_TLS_CERTIFICATE, pspec);

  pspec = g_param_spec_string ("hostname", "The hostname",
      "The hostname the user is expecting to connect to.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_HOSTNAME, pspec);

  pspec = g_param_spec_boxed ("reference-identities", "Reference Identities",
      "The server certificate should certify one of these identities",
      G_TYPE_STRV, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REFERENCE_IDENTITIES, pspec);

}

static void
empathy_server_tls_handler_init (EmpathyServerTLSHandler *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_SERVER_TLS_HANDLER, EmpathyServerTLSHandlerPriv);
}

void
empathy_server_tls_handler_new_async (TpChannel *channel,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_assert (TP_IS_CHANNEL (channel));
  g_assert (channel != NULL);

  g_async_initable_new_async (EMPATHY_TYPE_SERVER_TLS_HANDLER,
      G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "channel", channel, NULL);
}

EmpathyServerTLSHandler *
empathy_server_tls_handler_new_finish (GAsyncResult *result,
    GError **error)
{
  GObject *object, *source_object;

  source_object = g_async_result_get_source_object (result);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
      result, error);
  g_object_unref (source_object);

  if (object != NULL)
    return EMPATHY_SERVER_TLS_HANDLER (object);
  else
    return NULL;
}

EmpathyTLSCertificate *
empathy_server_tls_handler_get_certificate (EmpathyServerTLSHandler *self)
{
  EmpathyServerTLSHandlerPriv *priv = GET_PRIV (self);

  g_assert (priv->certificate != NULL);

  return priv->certificate;
}

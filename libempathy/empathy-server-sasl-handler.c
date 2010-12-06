/*
 * empathy-server-sasl-handler.c - Source for EmpathyServerSASLHandler
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

#include "empathy-server-sasl-handler.h"

#include <telepathy-glib/util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include "empathy-debug.h"
#include "empathy-utils.h"
#include "empathy-keyring.h"

enum {
  PROP_CHANNEL = 1,
  PROP_ACCOUNT,
  LAST_PROPERTY,
};

/* signal enum */
enum {
  INVALIDATED,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = {0};

typedef struct {
  TpChannel *channel;
  TpAccount *account;

  GSimpleAsyncResult *result;

  gchar *password;

  GSimpleAsyncResult *async_init_res;
} EmpathyServerSASLHandlerPriv;

static void async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (EmpathyServerSASLHandler, empathy_server_sasl_handler,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init));

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyServerSASLHandler);

static const gchar *sasl_statuses[] = {
  "not started",
  "in progress",
  "server succeeded",
  "client accepted",
  "succeeded",
  "server failed",
  "client failed",
};

static void
sasl_status_changed_cb (TpChannel *channel,
    TpSASLStatus status,
    const gchar *error,
    GHashTable *details,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (weak_object);

  DEBUG ("SASL status changed to '%s'", sasl_statuses[status]);

  if (status == TP_SASL_STATUS_SERVER_SUCCEEDED)
    {
      tp_cli_channel_interface_sasl_authentication_call_accept_sasl (
          priv->channel, -1, NULL, NULL, NULL, NULL);

      tp_cli_channel_call_close (priv->channel, -1,
          NULL, NULL, NULL, NULL);
    }
}

static gboolean
empathy_server_sasl_handler_give_password (gpointer data)
{
  EmpathyServerSASLHandler *self = data;
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (self);

  empathy_server_sasl_handler_provide_password (self,
      priv->password, FALSE);

  return FALSE;
}

static void
empathy_server_sasl_handler_get_password_async_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyServerSASLHandlerPriv *priv;
  const gchar *password;
  GError *error = NULL;

  priv = GET_PRIV (user_data);

  password = empathy_keyring_get_password_finish (TP_ACCOUNT (source),
      result, &error);

  if (password != NULL)
    {
      priv->password = g_strdup (password);

      /* Do this in an idle so the async result will get there
       * first. */
      g_idle_add (empathy_server_sasl_handler_give_password, user_data);
    }

  g_simple_async_result_complete (priv->async_init_res);
  tp_clear_object (&priv->async_init_res);
}

static void
empathy_server_sasl_handler_init_async (GAsyncInitable *initable,
    gint io_priority,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  EmpathyServerSASLHandler *self = EMPATHY_SERVER_SASL_HANDLER (initable);
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (self);

  g_assert (priv->account != NULL);

  priv->async_init_res = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, empathy_server_sasl_handler_new_async);

  empathy_keyring_get_password_async (priv->account,
      empathy_server_sasl_handler_get_password_async_cb, self);
}

static gboolean
empathy_server_sasl_handler_init_finish (GAsyncInitable *initable,
    GAsyncResult *res,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (res),
          error))
    return FALSE;

  return TRUE;
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = empathy_server_sasl_handler_init_async;
  iface->init_finish = empathy_server_sasl_handler_init_finish;
}

static void
channel_invalidated_cb (TpProxy *proxy,
    guint domain,
    gint code,
    gchar *message,
    EmpathyServerSASLHandler *self)
{
  g_signal_emit (self, signals[INVALIDATED], 0);
}

static void
empathy_server_sasl_handler_constructed (GObject *object)
{
  EmpathyServerSASLHandlerPriv *priv;
  GError *error = NULL;

  priv = GET_PRIV (object);

  tp_cli_channel_interface_sasl_authentication_connect_to_sasl_status_changed (priv->channel,
      sasl_status_changed_cb, NULL, NULL, object, &error);

  if (error != NULL)
    {
      DEBUG ("Failed to connect to SASLStatusChanged: %s", error->message);
      g_clear_error (&error);
    }

  tp_g_signal_connect_object (priv->channel, "invalidated",
      G_CALLBACK (channel_invalidated_cb), object, 0);
}

static void
empathy_server_sasl_handler_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_ACCOUNT:
      g_value_set_object (value, priv->account);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_server_sasl_handler_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_CHANNEL:
      priv->channel = g_value_dup_object (value);
      break;
    case PROP_ACCOUNT:
      priv->account = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_server_sasl_handler_dispose (GObject *object)
{
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (object);

  DEBUG ("%p", object);

  tp_clear_object (&priv->channel);
  tp_clear_object (&priv->account);

  G_OBJECT_CLASS (empathy_server_sasl_handler_parent_class)->dispose (object);
}

static void
empathy_server_sasl_handler_finalize (GObject *object)
{
  EmpathyServerSASLHandlerPriv *priv = GET_PRIV (object);

  DEBUG ("%p", object);

  tp_clear_pointer (&priv->password, g_free);

  G_OBJECT_CLASS (empathy_server_sasl_handler_parent_class)->finalize (object);
}

static void
empathy_server_sasl_handler_class_init (EmpathyServerSASLHandlerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  oclass->constructed = empathy_server_sasl_handler_constructed;
  oclass->get_property = empathy_server_sasl_handler_get_property;
  oclass->set_property = empathy_server_sasl_handler_set_property;
  oclass->dispose = empathy_server_sasl_handler_dispose;
  oclass->dispose = empathy_server_sasl_handler_finalize;

  g_type_class_add_private (klass, sizeof (EmpathyServerSASLHandlerPriv));

  pspec = g_param_spec_object ("channel", "The TpChannel",
      "The TpChannel this handler is supposed to handle.",
      TP_TYPE_CHANNEL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CHANNEL, pspec);

  pspec = g_param_spec_object ("account", "The TpAccount",
      "The TpAccount this channel belongs to.",
      TP_TYPE_ACCOUNT,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_ACCOUNT, pspec);

  signals[INVALIDATED] = g_signal_new ("invalidated",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE, 0);
}

static void
empathy_server_sasl_handler_init (EmpathyServerSASLHandler *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_SERVER_SASL_HANDLER, EmpathyServerSASLHandlerPriv);
}

EmpathyServerSASLHandler *
empathy_server_sasl_handler_new_finish (GAsyncResult *result,
    GError **error)
{
  GObject *object, *source_object;

  source_object = g_async_result_get_source_object (result);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
      result, error);
  g_object_unref (source_object);

  if (object != NULL)
    return EMPATHY_SERVER_SASL_HANDLER (object);
  else
    return NULL;
}

void
empathy_server_sasl_handler_new_async (TpAccount *account,
    TpChannel *channel,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (TP_IS_CHANNEL (channel));
  g_return_if_fail (callback != NULL);

  g_async_initable_new_async (EMPATHY_TYPE_SERVER_SASL_HANDLER,
      G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "account", account,
      "channel", channel,
      NULL);
}

static void
start_mechanism_with_data_cb (TpChannel *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  if (error != NULL)
    {
      DEBUG ("Failed to start mechanism: %s", error->message);
      return;
    }

  DEBUG ("Started mechanism successfully");
}

void
empathy_server_sasl_handler_provide_password (
    EmpathyServerSASLHandler *handler,
    const gchar *password,
    gboolean remember)
{
  EmpathyServerSASLHandlerPriv *priv;
  GArray *array;

  g_return_if_fail (EMPATHY_IS_SERVER_SASL_HANDLER (handler));

  priv = GET_PRIV (handler);

  array = g_array_sized_new (TRUE, FALSE,
      sizeof (gchar), strlen (password));

  g_array_append_vals (array, password, strlen (password));

  DEBUG ("Calling StartMechanismWithData with our password");

  tp_cli_channel_interface_sasl_authentication_call_start_mechanism_with_data (
      priv->channel, -1, "X-TELEPATHY-PASSWORD", array, start_mechanism_with_data_cb,
      NULL, NULL, G_OBJECT (handler));

  g_array_unref (array);

  DEBUG ("%sremembering the password", remember ? "" : "not ");

  if (remember)
    {
      /* TODO */
    }
}

void
empathy_server_sasl_handler_cancel (EmpathyServerSASLHandler *handler)
{
  EmpathyServerSASLHandlerPriv *priv;

  g_return_if_fail (EMPATHY_IS_SERVER_SASL_HANDLER (handler));

  priv = GET_PRIV (handler);

  DEBUG ("Cancelling SASL mechanism...");

  tp_cli_channel_interface_sasl_authentication_call_abort_sasl (
      priv->channel, -1, TP_SASL_ABORT_REASON_USER_ABORT,
      "User cancelled the authentication",
      NULL, NULL, NULL, NULL);
}

TpAccount *
empathy_server_sasl_handler_get_account (EmpathyServerSASLHandler *handler)
{
  EmpathyServerSASLHandlerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_SERVER_SASL_HANDLER (handler), NULL);

  priv = GET_PRIV (handler);

  return priv->account;
}

gboolean
empathy_server_sasl_handler_has_password (EmpathyServerSASLHandler *handler)
{
  EmpathyServerSASLHandlerPriv *priv;

  g_return_val_if_fail (EMPATHY_IS_SERVER_SASL_HANDLER (handler), FALSE);

  priv = GET_PRIV (handler);

  return (priv->password != NULL);
}

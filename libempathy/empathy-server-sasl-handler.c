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
} EmpathyServerSASLHandlerPriv;

G_DEFINE_TYPE (EmpathyServerSASLHandler, empathy_server_sasl_handler,
    G_TYPE_OBJECT);

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
empathy_server_sasl_handler_class_init (EmpathyServerSASLHandlerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *pspec;

  oclass->constructed = empathy_server_sasl_handler_constructed;
  oclass->get_property = empathy_server_sasl_handler_get_property;
  oclass->set_property = empathy_server_sasl_handler_set_property;
  oclass->dispose = empathy_server_sasl_handler_dispose;

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
empathy_server_sasl_handler_new (TpAccount *account,
    TpChannel *channel)
{
  g_return_val_if_fail (TP_IS_CHANNEL (channel), NULL);

  return g_object_new (EMPATHY_TYPE_SERVER_SASL_HANDLER,
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

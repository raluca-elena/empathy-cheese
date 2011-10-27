/*
 * empathy-auth-goa.c - Source for Goa SASL authentication
 * Copyright (C) 2011 Collabora Ltd.
 * @author Xavier Claessens <xavier.claessens@collabora.co.uk>
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

#include "config.h"

#define GOA_API_IS_SUBJECT_TO_CHANGE /* awesome! */
#include <goa/goa.h>

#include <libsoup/soup.h>
#include <string.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include "empathy-debug.h"
#include "empathy-utils.h"
#include "empathy-goa-auth-handler.h"

#define MECH_FACEBOOK "X-FACEBOOK-PLATFORM"
#define MECH_MSN "X-MESSENGER-OAUTH2"

static const gchar *supported_mechanisms[] = {
    MECH_FACEBOOK,
    MECH_MSN,
    NULL};

struct _EmpathyGoaAuthHandlerPriv
{
  GoaClient *client;
  gboolean client_preparing;

  /* List of AuthData waiting for client to be created */
  GList *auth_queue;
};

G_DEFINE_TYPE (EmpathyGoaAuthHandler, empathy_goa_auth_handler, G_TYPE_OBJECT);

static void
empathy_goa_auth_handler_init (EmpathyGoaAuthHandler *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_GOA_AUTH_HANDLER, EmpathyGoaAuthHandlerPriv);
}

static void
empathy_goa_auth_handler_dispose (GObject *object)
{
  EmpathyGoaAuthHandler *self = (EmpathyGoaAuthHandler *) object;

  /* AuthData keeps a ref on self */
  g_assert (self->priv->auth_queue == NULL);

  tp_clear_object (&self->priv->client);

  G_OBJECT_CLASS (empathy_goa_auth_handler_parent_class)->dispose (object);
}

static void
empathy_goa_auth_handler_class_init (EmpathyGoaAuthHandlerClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->dispose = empathy_goa_auth_handler_dispose;

  g_type_class_add_private (klass, sizeof (EmpathyGoaAuthHandlerPriv));
}

EmpathyGoaAuthHandler *
empathy_goa_auth_handler_new (void)
{
  return g_object_new (EMPATHY_TYPE_GOA_AUTH_HANDLER, NULL);
}

typedef struct
{
  EmpathyGoaAuthHandler *self;
  TpChannel *channel;
  TpAccount *account;

  GoaObject *goa_object;
  gchar *access_token;
} AuthData;

static void
auth_data_free (AuthData *data)
{
  tp_clear_object (&data->self);
  tp_clear_object (&data->channel);
  tp_clear_object (&data->account);
  tp_clear_object (&data->goa_object);
  g_free (data->access_token);
  g_slice_free (AuthData, data);
}

static void
fail_auth (AuthData *data)
{
  DEBUG ("Auth failed for account %s",
      tp_proxy_get_object_path (data->account));

  tp_channel_close_async (data->channel, NULL, NULL);
  auth_data_free (data);
}

static void
sasl_status_changed_cb (TpChannel *channel,
    guint status,
    const gchar *reason,
    GHashTable *details,
    gpointer user_data,
    GObject *self)
{
  switch (status)
    {
      case TP_SASL_STATUS_SERVER_SUCCEEDED:
        tp_cli_channel_interface_sasl_authentication_call_accept_sasl (channel,
            -1, NULL, NULL, NULL, NULL);
        break;

      case TP_SASL_STATUS_SUCCEEDED:
      case TP_SASL_STATUS_SERVER_FAILED:
      case TP_SASL_STATUS_CLIENT_FAILED:
        tp_cli_channel_call_close (channel, -1, NULL, NULL, NULL, NULL);
        break;

      default:
        break;
    }
}

static void
facebook_new_challenge_cb (TpChannel *channel,
    const GArray *challenge,
    gpointer user_data,
    GObject *weak_object)
{
  AuthData *data = user_data;
  GoaOAuth2Based *oauth2;
  const gchar *client_id;
  GHashTable *h;
  GHashTable *params;
  gchar *response;
  GArray *response_array;

  DEBUG ("new challenge for %s:\n%s",
      tp_proxy_get_object_path (data->account),
      challenge->data);

  h = soup_form_decode (challenge->data);

  oauth2 = goa_object_get_oauth2_based (data->goa_object);
  client_id = goa_oauth2_based_get_client_id (oauth2);

  /* See https://developers.facebook.com/docs/chat/#platauth */
  params = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (params, "method", g_hash_table_lookup (h, "method"));
  g_hash_table_insert (params, "nonce", g_hash_table_lookup (h, "nonce"));
  g_hash_table_insert (params, "access_token", data->access_token);
  g_hash_table_insert (params, "api_key", (gpointer) client_id);
  g_hash_table_insert (params, "call_id", "0");
  g_hash_table_insert (params, "v", "1.0");

  response = soup_form_encode_hash (params);
  DEBUG ("Response: %s", response);

  response_array = g_array_new (FALSE, FALSE, sizeof (gchar));
  g_array_append_vals (response_array, response, strlen (response));

  tp_cli_channel_interface_sasl_authentication_call_respond (data->channel, -1,
      response_array, NULL, NULL, NULL, NULL);

  g_hash_table_unref (h);
  g_hash_table_unref (params);
  g_object_unref (oauth2);
  g_free (response);
  g_array_unref (response_array);
}

static void
got_oauth2_access_token_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GoaOAuth2Based *oauth2 = (GoaOAuth2Based *) source;
  AuthData *data = user_data;
  gint expires_in;
  GError *error = NULL;

  if (!goa_oauth2_based_call_get_access_token_finish (oauth2,
      &data->access_token, &expires_in, result, &error))
    {
      DEBUG ("Failed to get access token: %s", error->message);
      fail_auth (data);
      g_clear_error (&error);
      return;
    }

  DEBUG ("Got access token for %s:\n%s",
      tp_proxy_get_object_path (data->account),
      data->access_token);

  tp_cli_channel_interface_sasl_authentication_connect_to_sasl_status_changed (
      data->channel, sasl_status_changed_cb, NULL, NULL, NULL, NULL);
  g_assert_no_error (error);

  if (empathy_sasl_channel_supports_mechanism (data->channel, MECH_FACEBOOK))
    {
      /* Give ownership of data to signal connection */
      tp_cli_channel_interface_sasl_authentication_connect_to_new_challenge (
          data->channel, facebook_new_challenge_cb,
          data, (GDestroyNotify) auth_data_free,
          NULL, NULL);

      DEBUG ("Start %s mechanism for account %s", MECH_FACEBOOK,
          tp_proxy_get_object_path (data->account));

      tp_cli_channel_interface_sasl_authentication_call_start_mechanism (
          data->channel, -1, MECH_FACEBOOK, NULL, NULL, NULL, NULL);
    }
  else if (empathy_sasl_channel_supports_mechanism (data->channel, MECH_MSN))
    {
      guchar *token_decoded;
      gsize token_decoded_len;
      GArray *token_decoded_array;

      /* Wocky will base64 encode, but token actually already is base64, so we
       * decode now and it will be re-encoded. */
      token_decoded = g_base64_decode (data->access_token, &token_decoded_len);
      token_decoded_array = g_array_new (FALSE, FALSE, sizeof (guchar));
      g_array_append_vals (token_decoded_array, token_decoded, token_decoded_len);

      DEBUG ("Start %s mechanism for account %s", MECH_MSN,
          tp_proxy_get_object_path (data->account));

      tp_cli_channel_interface_sasl_authentication_call_start_mechanism_with_data (
          data->channel, -1, MECH_MSN, token_decoded_array,
          NULL, NULL, NULL, NULL);

      g_array_unref (token_decoded_array);
      g_free (token_decoded);
      auth_data_free (data);
    }
  else
    {
      /* We already checked it supports one of supported_mechanisms, so this
       * can't happen */
      g_assert_not_reached ();
    }
}

static void
ensure_credentials_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  AuthData *data = user_data;
  GoaAccount *goa_account = (GoaAccount *) source;
  GoaOAuth2Based *oauth2;
  gint expires_in;
  GError *error = NULL;

  if (!goa_account_call_ensure_credentials_finish (goa_account, &expires_in,
      result, &error))
    {
      DEBUG ("Failed to EnsureCredentials: %s", error->message);
      fail_auth (data);
      g_clear_error (&error);
      return;
    }

  /* We support only oaut2 */
  oauth2 = goa_object_get_oauth2_based (data->goa_object);
  if (oauth2 == NULL)
    {
      DEBUG ("GoaObject does not implement oauth2");
      fail_auth (data);
      return;
    }

  DEBUG ("Goa daemon has credentials for %s, get the access token",
      tp_proxy_get_object_path (data->account));

  goa_oauth2_based_call_get_access_token (oauth2, NULL,
      got_oauth2_access_token_cb, data);

  g_object_unref (oauth2);
}

static void
start_auth (AuthData *data)
{
  EmpathyGoaAuthHandler *self = data->self;
  const GValue *id_value;
  const gchar *id;
  GList *goa_accounts, *l;
  gboolean found = FALSE;

  id_value = tp_account_get_storage_identifier (data->account);
  id = g_value_get_string (id_value);

  goa_accounts = goa_client_get_accounts (self->priv->client);
  for (l = goa_accounts; l != NULL && !found; l = l->next)
    {
      GoaObject *goa_object = l->data;
      GoaAccount *goa_account;

      goa_account = goa_object_get_account (goa_object);
      if (!tp_strdiff (goa_account_get_id (goa_account), id))
        {
          data->goa_object = g_object_ref (goa_object);

          DEBUG ("Found the GoaAccount for %s, ensure credentials",
              tp_proxy_get_object_path (data->account));

          goa_account_call_ensure_credentials (goa_account, NULL,
              ensure_credentials_cb, data);

          found = TRUE;
        }

      g_object_unref (goa_account);
    }
  g_list_free_full (goa_accounts, g_object_unref);

  if (!found)
    {
      DEBUG ("Cannot find GoaAccount");
      fail_auth (data);
    }
}

static void
client_new_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyGoaAuthHandler *self = user_data;
  GList *l;
  GError *error = NULL;

  self->priv->client_preparing = FALSE;
  self->priv->client = goa_client_new_finish (result, &error);
  if (self->priv->client == NULL)
    {
      DEBUG ("Error getting GoaClient: %s", error->message);
      g_clear_error (&error);
    }

  /* process queued data */
  for (l = self->priv->auth_queue; l != NULL; l = l->next)
    {
      AuthData *data = l->data;

      if (self->priv->client != NULL)
        start_auth (data);
      else
        fail_auth (data);
    }

  tp_clear_pointer (&self->priv->auth_queue, g_list_free);
}

void
empathy_goa_auth_handler_start (EmpathyGoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *account)
{
  AuthData *data;

  g_return_if_fail (TP_IS_CHANNEL (channel));
  g_return_if_fail (TP_IS_ACCOUNT (account));
  g_return_if_fail (empathy_goa_auth_handler_supports (self, channel, account));

  DEBUG ("Start Goa auth for account: %s",
      tp_proxy_get_object_path (account));

  data = g_slice_new0 (AuthData);
  data->self = g_object_ref (self);
  data->channel = g_object_ref (channel);
  data->account = g_object_ref (account);

  if (self->priv->client == NULL)
    {
      /* GOA client not ready yet, queue data */
      if (!self->priv->client_preparing)
        {
          goa_client_new (NULL, client_new_cb, self);
          self->priv->client_preparing = TRUE;
        }

      self->priv->auth_queue = g_list_prepend (self->priv->auth_queue, data);
    }
  else
    {
      start_auth (data);
    }
}

gboolean
empathy_goa_auth_handler_supports (EmpathyGoaAuthHandler *self,
    TpChannel *channel,
    TpAccount *account)
{
  const gchar *provider;
  const gchar * const *iter;

  g_return_val_if_fail (TP_IS_CHANNEL (channel), FALSE);
  g_return_val_if_fail (TP_IS_ACCOUNT (account), FALSE);

  provider = tp_account_get_storage_provider (account);
  if (tp_strdiff (provider, EMPATHY_GOA_PROVIDER))
    return FALSE;

  for (iter = supported_mechanisms; *iter != NULL; iter++)
    {
      if (empathy_sasl_channel_supports_mechanism (channel, *iter))
        return TRUE;
    }

  return FALSE;
}

/* * Copyright (C) 2007-2010 Collabora Ltd.
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
 *
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>

#include <telepathy-glib/telepathy-glib.h>

#include "empathy-request-util.h"
#include "empathy-utils.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_DISPATCHER
#include <libempathy/empathy-debug.h>

void
empathy_chat_with_contact (EmpathyContact *contact,
    gint64 timestamp)
{
  empathy_chat_with_contact_id (
      empathy_contact_get_account (contact), empathy_contact_get_id (contact),
      timestamp, NULL, NULL);
}

static void
ensure_text_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;

  if (!tp_account_channel_request_ensure_channel_finish (
        TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error))
    {
      DEBUG ("Failed to ensure text channel: %s", error->message);
      g_error_free (error);
    }
}

static void
create_text_channel (TpAccount *account,
    TpHandleType target_handle_type,
    const gchar *target_id,
    gboolean sms_channel,
    gint64 timestamp,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GHashTable *request;
  TpAccountChannelRequest *req;

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_TEXT,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT, target_handle_type,
      TP_PROP_CHANNEL_TARGET_ID, G_TYPE_STRING, target_id,
      NULL);

  if (sms_channel)
    tp_asv_set_boolean (request,
        TP_PROP_CHANNEL_INTERFACE_SMS_SMS_CHANNEL, TRUE);

  req = tp_account_channel_request_new (account, request, timestamp);
  tp_account_channel_request_set_delegate_to_preferred_handler (req, TRUE);

  tp_account_channel_request_ensure_channel_async (req, EMPATHY_CHAT_BUS_NAME,
      NULL, callback ? callback : ensure_text_channel_cb, user_data);

  g_hash_table_unref (request);
  g_object_unref (req);
}

/* @callback is optional, but if it's provided, it should call the right
 * _finish() func that we call in ensure_text_channel_cb() */
void
empathy_chat_with_contact_id (TpAccount *account,
    const gchar *contact_id,
    gint64 timestamp,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  create_text_channel (account, TP_HANDLE_TYPE_CONTACT,
      contact_id, FALSE, timestamp, callback, user_data);
}

void
empathy_join_muc (TpAccount *account,
    const gchar *room_name,
    gint64 timestamp)
{
  create_text_channel (account, TP_HANDLE_TYPE_ROOM,
      room_name, FALSE, timestamp, NULL, NULL);
}

/* @callback is optional, but if it's provided, it should call the right
 * _finish() func that we call in ensure_text_channel_cb() */
void
empathy_sms_contact_id (TpAccount *account,
    const gchar *contact_id,
    gint64 timestamp,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  create_text_channel (account, TP_HANDLE_TYPE_CONTACT,
      contact_id, TRUE, timestamp, callback, user_data);
}

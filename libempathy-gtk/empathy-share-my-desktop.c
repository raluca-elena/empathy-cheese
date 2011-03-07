/*
 * © 2009, Collabora Ltd
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
 * Authors: Arnaud Maillet <arnaud.maillet@collabora.co.uk>
 */

#include <gtk/gtk.h>

#include <dbus/dbus-glib.h>
#include <telepathy-glib/account-channel-request.h>
#include <telepathy-glib/util.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/interfaces.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SHARE_DESKTOP
#include <libempathy/empathy-debug.h>

#include "empathy-share-my-desktop.h"

static void
create_tube_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;

  if (!tp_account_channel_request_create_channel_finish (
        TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error))
    {
      DEBUG ("Failed to create tube channel: %s", error->message);
      g_error_free (error);
    }
}

void
empathy_share_my_desktop_share_with_contact (EmpathyContact *contact)
{
  TpAccountChannelRequest *req;
  GHashTable *request;
  TpContact *tp_contact;

  tp_contact = empathy_contact_get_tp_contact (contact);

  DEBUG ("Creation of ShareMyDesktop");

  if (!TP_IS_CONTACT (tp_contact))
    {
      DEBUG ("It's not a tp contact");
      return;
    }

  request = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
        TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
      TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
        TP_HANDLE_TYPE_CONTACT,
      TP_PROP_CHANNEL_TARGET_HANDLE, G_TYPE_UINT,
        tp_contact_get_handle (tp_contact),
      TP_PROP_CHANNEL_TYPE_STREAM_TUBE_SERVICE, G_TYPE_STRING, "rfb",
      NULL);

  req = tp_account_channel_request_new (empathy_contact_get_account (contact),
      request, TP_USER_ACTION_TIME_CURRENT_TIME);

  tp_account_channel_request_create_channel_async (req, NULL, NULL,
      create_tube_channel_cb, NULL);

  g_object_unref (req);
  g_hash_table_unref (request);
}

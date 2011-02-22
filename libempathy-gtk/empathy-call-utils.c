/*
 * Copyright (C) 2011 Collabora Ltd.
 *
 * The code contained in this file is free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#include "config.h"

#include <telepathy-glib/telepathy-glib.h>

#if HAVE_CALL
 #include <telepathy-yell/telepathy-yell.h>
#endif

#include "empathy-call-utils.h"

#include <libempathy/empathy-request-util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#if HAVE_CALL
GHashTable *
empathy_call_create_call_request (EmpathyContact *contact,
    gboolean initial_audio,
    gboolean initial_video)
{
  return tp_asv_new (
    TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
      TPY_IFACE_CHANNEL_TYPE_CALL,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
      TP_HANDLE_TYPE_CONTACT,
    TP_PROP_CHANNEL_TARGET_HANDLE, G_TYPE_UINT,
      empathy_contact_get_handle (contact),
    TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_AUDIO, G_TYPE_BOOLEAN,
      initial_audio,
    TPY_PROP_CHANNEL_TYPE_CALL_INITIAL_VIDEO, G_TYPE_BOOLEAN,
      initial_video,
    NULL);
}
#endif

GHashTable *
empathy_call_create_streamed_media_request (EmpathyContact *contact,
    gboolean initial_audio,
    gboolean initial_video)
{
  return tp_asv_new (
    TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
    TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
      TP_HANDLE_TYPE_CONTACT,
    TP_PROP_CHANNEL_TARGET_HANDLE, G_TYPE_UINT,
      empathy_contact_get_handle (contact),
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, G_TYPE_BOOLEAN,
      initial_audio,
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO, G_TYPE_BOOLEAN,
      initial_video,
    NULL);
}

static void
create_streamed_media_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;

  if (!tp_account_channel_request_create_channel_finish (
           TP_ACCOUNT_CHANNEL_REQUEST (source),
           result,
           &error))
    {
      DEBUG ("Failed to create StreamedMedia channel: %s", error->message);
      g_error_free (error);
    }
}

#if HAVE_CALL
static void
create_call_channel_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountChannelRequest *streamed_media_req = user_data;
  GError *error = NULL;

  if (tp_account_channel_request_create_channel_finish (
      TP_ACCOUNT_CHANNEL_REQUEST (source), result, &error))
    {
      g_object_unref (streamed_media_req);
      return;
    }

  DEBUG ("Failed to create Call channel: %s", error->message);

  if (error->code != TP_ERROR_NOT_IMPLEMENTED)
    return;

  DEBUG ("Let's try with an StreamedMedia channel");
  g_error_free (error);
  tp_account_channel_request_create_channel_async (streamed_media_req,
      EMPATHY_AV_BUS_NAME, NULL,
      create_streamed_media_channel_cb,
      NULL);
}
#endif

void
empathy_call_new_with_streams (EmpathyContact *contact,
    gboolean initial_audio,
    gboolean initial_video,
    gint64 timestamp)
{
#if HAVE_CALL
  GHashTable *call_request, *streamed_media_request;
  TpAccount *account;
  TpAccountChannelRequest *call_req, *streamed_media_req;

  call_request = empathy_call_create_call_request (contact,
      initial_audio,
      initial_video);

  streamed_media_request = empathy_call_create_streamed_media_request (
      contact, initial_audio, initial_video);

  account = empathy_contact_get_account (contact);

  call_req = tp_account_channel_request_new (account, call_request, timestamp);
  streamed_media_req = tp_account_channel_request_new (account,
      streamed_media_request,
      timestamp);

  tp_account_channel_request_create_channel_async (call_req,
      EMPATHY_CALL_BUS_NAME, NULL,
      create_call_channel_cb,
      streamed_media_req);

  g_hash_table_unref (call_request);
  g_hash_table_unref (streamed_media_request);
  g_object_unref (call_req);
#else
  GHashTable *request;
  TpAccountChannelRequest *req;

  request = empathy_call_create_streamed_media_request (contact,
      initial_audio,
      initial_video);

  req = tp_account_channel_request_new (account, request, timestamp);

  tp_account_channel_request_create_channel_async (req,
      EMPATHY_AV_BUS_NAME, NULL,
      create_streamed_media_channel_cb,
      NULL);

  g_hash_table_unref (request);
  g_object_unref (req);
#endif
}

/*
 * Copyright (C) 2007 Elliot Fairweather
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Elliot Fairweather <elliot.fairweather@collabora.co.uk>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include <string.h>

#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/util.h>

#include "empathy-tp-streamed-media.h"
#include "empathy-tp-contact-factory.h"
#include "empathy-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include "empathy-debug.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTpStreamedMedia)
typedef struct
{
  gboolean dispose_has_run;
  TpAccount *account;
  TpChannel *channel;
  EmpathyContact *contact;
  gboolean is_incoming;
  guint status;

  EmpathyTpStreamedMediaStream *audio;
  EmpathyTpStreamedMediaStream *video;
} EmpathyTpStreamedMediaPriv;

/* signal enum */
enum {
  AUDIO_STREAM_ERROR,
  VIDEO_STREAM_ERROR,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

enum
{
  PROP_0,
  PROP_ACCOUNT,
  PROP_CHANNEL,
  PROP_CONTACT,
  PROP_STATUS,
  PROP_AUDIO_STREAM,
  PROP_VIDEO_STREAM
};

G_DEFINE_TYPE (EmpathyTpStreamedMedia, empathy_tp_streamed_media, G_TYPE_OBJECT)

static void
tp_streamed_media_add_stream (EmpathyTpStreamedMedia *call,
                    guint stream_id,
                    guint contact_handle,
                    guint stream_type,
                    guint stream_state,
                    guint stream_direction)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  switch (stream_type)
    {
      case TP_MEDIA_STREAM_TYPE_AUDIO:
        DEBUG ("Audio stream - id: %d, state: %d, direction: %d",
            stream_id, stream_state, stream_direction);
        priv->audio->exists = TRUE;
        priv->audio->id = stream_id;
        priv->audio->state = stream_state;
        priv->audio->direction = stream_direction;
        g_object_notify (G_OBJECT (call), "audio-stream");
        break;
      case TP_MEDIA_STREAM_TYPE_VIDEO:
        DEBUG ("Video stream - id: %d, state: %d, direction: %d",
            stream_id, stream_state, stream_direction);
        priv->video->exists = TRUE;
        priv->video->id = stream_id;
        priv->video->state = stream_state;
        priv->video->direction = stream_direction;
        g_object_notify (G_OBJECT (call), "video-stream");
        break;
      default:
        DEBUG ("Unknown stream type: %d", stream_type);
    }
}

static void
tp_streamed_media_stream_added_cb (TpChannel *channel,
                         guint stream_id,
                         guint contact_handle,
                         guint stream_type,
                         gpointer user_data,
                         GObject *call)
{
  DEBUG ("Stream added - stream id: %d, contact handle: %d, stream type: %d",
      stream_id, contact_handle, stream_type);

  tp_streamed_media_add_stream (EMPATHY_TP_STREAMED_MEDIA (call), stream_id, contact_handle,
      stream_type, TP_MEDIA_STREAM_STATE_DISCONNECTED,
      TP_MEDIA_STREAM_DIRECTION_NONE);
}

static void
tp_streamed_media_stream_removed_cb (TpChannel *channel,
                           guint stream_id,
                           gpointer user_data,
                           GObject *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  DEBUG ("Stream removed - stream id: %d", stream_id);

  if (stream_id == priv->audio->id)
    {
      priv->audio->exists = FALSE;
      g_object_notify (call, "audio-stream");
    }
  else if (stream_id == priv->video->id)
    {
      priv->video->exists = FALSE;
      g_object_notify (call, "video-stream");
    }
}

static void
tp_streamed_media_stream_state_changed_cb (TpChannel *proxy,
                                 guint stream_id,
                                 guint stream_state,
                                 gpointer user_data,
                                 GObject *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  DEBUG ("Stream state changed - stream id: %d, state state: %d",
      stream_id, stream_state);

  if (stream_id == priv->audio->id)
    {
      priv->audio->state = stream_state;
      g_object_notify (call, "audio-stream");
    }
  else if (stream_id == priv->video->id)
    {
      priv->video->state = stream_state;
      g_object_notify (call, "video-stream");
    }
}

static void
tp_streamed_media_stream_direction_changed_cb (TpChannel *channel,
                                     guint stream_id,
                                     guint stream_direction,
                                     guint pending_flags,
                                     gpointer user_data,
                                     GObject *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  DEBUG ("Stream direction changed - stream: %d, direction: %d",
      stream_id, stream_direction);

  if (stream_id == priv->audio->id)
    {
      priv->audio->direction = stream_direction;
      g_object_notify (call, "audio-stream");
    }
  else if (stream_id == priv->video->id)
    {
      priv->video->direction = stream_direction;
      g_object_notify (call, "video-stream");
    }
}

static void
tp_streamed_media_request_streams_cb (TpChannel *channel,
                            const GPtrArray *streams,
                            const GError *error,
                            gpointer user_data,
                            GObject *call)
{
  guint i;

  if (error)
    {
      DEBUG ("Error requesting streams: %s", error->message);
      return;
    }

  for (i = 0; i < streams->len; i++)
    {
      GValueArray *values;
      guint stream_id;
      guint contact_handle;
      guint stream_type;
      guint stream_state;
      guint stream_direction;

      values = g_ptr_array_index (streams, i);
      stream_id = g_value_get_uint (g_value_array_get_nth (values, 0));
      contact_handle = g_value_get_uint (g_value_array_get_nth (values, 1));
      stream_type = g_value_get_uint (g_value_array_get_nth (values, 2));
      stream_state = g_value_get_uint (g_value_array_get_nth (values, 3));
      stream_direction = g_value_get_uint (g_value_array_get_nth (values, 4));

      tp_streamed_media_add_stream (EMPATHY_TP_STREAMED_MEDIA (call), stream_id, contact_handle,
          stream_type, stream_state, stream_direction);
  }
}

static void
tp_streamed_media_request_streams_for_capabilities (EmpathyTpStreamedMedia *call,
                                          EmpathyCapabilities capabilities)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);
  GArray *stream_types;
  guint handle;
  guint stream_type;

  if (capabilities == EMPATHY_CAPABILITIES_UNKNOWN)
      capabilities = EMPATHY_CAPABILITIES_AUDIO | EMPATHY_CAPABILITIES_VIDEO;

  DEBUG ("Requesting new stream for capabilities %d",
      capabilities);

  stream_types = g_array_new (FALSE, FALSE, sizeof (guint));
  handle = empathy_contact_get_handle (priv->contact);

  if (capabilities & EMPATHY_CAPABILITIES_AUDIO)
    {
      stream_type = TP_MEDIA_STREAM_TYPE_AUDIO;
      g_array_append_val (stream_types, stream_type);
    }
  if (capabilities & EMPATHY_CAPABILITIES_VIDEO)
    {
      stream_type = TP_MEDIA_STREAM_TYPE_VIDEO;
      g_array_append_val (stream_types, stream_type);
    }

  tp_cli_channel_type_streamed_media_call_request_streams (priv->channel, -1,
      handle, stream_types, tp_streamed_media_request_streams_cb, NULL, NULL,
      G_OBJECT (call));

  g_array_unref (stream_types);
}

static void
tp_streamed_media_got_contact_cb (TpConnection            *connection,
                        EmpathyContact          *contact,
                        const GError            *error,
                        gpointer                 user_data,
                        GObject                 *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  if (error)
    {
      DEBUG ("Error: %s", error->message);
      return;
    }

  priv->contact = g_object_ref (contact);

  if (priv->status < EMPATHY_TP_STREAMED_MEDIA_STATUS_PENDING)
    {
      priv->status = EMPATHY_TP_STREAMED_MEDIA_STATUS_PENDING;
      g_object_notify (G_OBJECT (call), "status");
    }

  g_object_notify (G_OBJECT (call), "contact");
}

static void
tp_streamed_media_update_status (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);
  TpHandle self_handle;
  const TpIntSet *set;
  TpIntSetIter iter;

  g_object_ref (call);

  self_handle = tp_channel_group_get_self_handle (priv->channel);
  set = tp_channel_group_get_members (priv->channel);
  tp_intset_iter_init (&iter, set);
  while (tp_intset_iter_next (&iter))
    {
      if (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_PENDING &&
          ((priv->is_incoming && iter.element == self_handle) ||
           (!priv->is_incoming && iter.element != self_handle)))
        {
          priv->status = EMPATHY_TP_STREAMED_MEDIA_STATUS_ACCEPTED;
          g_object_notify (G_OBJECT (call), "status");
        }
    }

  g_object_unref (call);
}

static void
tp_streamed_media_channel_invalidated_cb (TpChannel     *channel,
                                GQuark         domain,
                                gint           code,
                                gchar         *message,
                                EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  DEBUG ("Channel invalidated: %s", message);
  priv->status = EMPATHY_TP_STREAMED_MEDIA_STATUS_CLOSED;
  g_object_notify (G_OBJECT (call), "status");
}

static void
tp_streamed_media_async_cb (TpProxy *proxy,
                  const GError *error,
                  gpointer user_data,
                  GObject *call)
{
  if (error)
      DEBUG ("Error %s: %s", (gchar *) user_data, error->message);
}

static void
tp_streamed_media_stream_error_cb (TpChannel *channel,
    guint stream_id,
    guint error_code,
    const gchar *msg,
    gpointer user_data,
    GObject *call)
{
  EmpathyTpStreamedMedia *self = EMPATHY_TP_STREAMED_MEDIA (call);
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);

  DEBUG ("Stream error on stream %u: %s (code: %u)", stream_id, msg,
      error_code);

  if (priv->audio->id == stream_id)
    {
      g_signal_emit (call, signals[AUDIO_STREAM_ERROR], 0, error_code, msg);
    }
  else if (priv->video->id == stream_id)
    {
      g_signal_emit (call, signals[VIDEO_STREAM_ERROR], 0, error_code, msg);
    }
  else
    {
      DEBUG ("Unknown stream id: %u", stream_id);
    }
}

static GObject *
tp_streamed_media_constructor (GType type,
                     guint n_construct_params,
                     GObjectConstructParam *construct_params)
{
  GObject *object;
  EmpathyTpStreamedMedia *call;
  EmpathyTpStreamedMediaPriv *priv;

  object = G_OBJECT_CLASS (empathy_tp_streamed_media_parent_class)->constructor (type,
      n_construct_params, construct_params);

  call = EMPATHY_TP_STREAMED_MEDIA (object);
  priv = GET_PRIV (call);

  /* Setup streamed media channel */
  g_signal_connect (priv->channel, "invalidated",
      G_CALLBACK (tp_streamed_media_channel_invalidated_cb), call);
  tp_cli_channel_type_streamed_media_connect_to_stream_added (priv->channel,
      tp_streamed_media_stream_added_cb, NULL, NULL, G_OBJECT (call), NULL);
  tp_cli_channel_type_streamed_media_connect_to_stream_removed (priv->channel,
      tp_streamed_media_stream_removed_cb, NULL, NULL, G_OBJECT (call), NULL);
  tp_cli_channel_type_streamed_media_connect_to_stream_state_changed (priv->channel,
      tp_streamed_media_stream_state_changed_cb, NULL, NULL, G_OBJECT (call), NULL);
  tp_cli_channel_type_streamed_media_connect_to_stream_direction_changed (priv->channel,
      tp_streamed_media_stream_direction_changed_cb, NULL, NULL, G_OBJECT (call), NULL);
  tp_cli_channel_type_streamed_media_connect_to_stream_error (priv->channel,
      tp_streamed_media_stream_error_cb, NULL, NULL, G_OBJECT (call), NULL);
  tp_cli_channel_type_streamed_media_call_list_streams (priv->channel, -1,
      tp_streamed_media_request_streams_cb, NULL, NULL, G_OBJECT (call));

  /* Is the call incoming? */
  priv->is_incoming = !tp_channel_get_requested (priv->channel);

  /* Get the remote contact */
  empathy_tp_contact_factory_get_from_handle (
      tp_channel_borrow_connection (priv->channel),
      tp_channel_get_handle (priv->channel, NULL), tp_streamed_media_got_contact_cb,
      NULL, NULL, object);

  /* Update status when members changes */
  tp_streamed_media_update_status (call);
  tp_g_signal_connect_object (priv->channel, "group-members-changed",
      G_CALLBACK (tp_streamed_media_update_status), call, G_CONNECT_SWAPPED);

  return object;
}
static void
tp_streamed_media_dispose (GObject *object)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (object);

  DEBUG ("Disposing: %p, %d", object, priv->dispose_has_run);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->channel != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->channel,
        tp_streamed_media_channel_invalidated_cb, object);

      g_object_unref (priv->channel);
      priv->channel = NULL;
    }

  if (priv->contact != NULL)
      g_object_unref (priv->contact);

  tp_clear_object (&priv->account);

  if (G_OBJECT_CLASS (empathy_tp_streamed_media_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_tp_streamed_media_parent_class)->dispose (object);
}

static void
tp_streamed_media_finalize (GObject *object)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (object);

  DEBUG ("Finalizing: %p", object);

  g_slice_free (EmpathyTpStreamedMediaStream, priv->audio);
  g_slice_free (EmpathyTpStreamedMediaStream, priv->video);

  (G_OBJECT_CLASS (empathy_tp_streamed_media_parent_class)->finalize) (object);
}

static void
tp_streamed_media_set_property (GObject *object,
                      guint prop_id,
                      const GValue *value,
                      GParamSpec *pspec)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_ACCOUNT:
      priv->account = g_value_dup_object (value);
      break;
    case PROP_CHANNEL:
      priv->channel = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
tp_streamed_media_get_property (GObject *object,
                      guint prop_id,
                      GValue *value,
                      GParamSpec *pspec)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
    case PROP_ACCOUNT:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_CHANNEL:
      g_value_set_object (value, priv->channel);
      break;
    case PROP_CONTACT:
      g_value_set_object (value, priv->contact);
      break;
    case PROP_STATUS:
      g_value_set_uint (value, priv->status);
      break;
    case PROP_AUDIO_STREAM:
      g_value_set_pointer (value, priv->audio);
      break;
    case PROP_VIDEO_STREAM:
      g_value_set_pointer (value, priv->video);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
empathy_tp_streamed_media_class_init (EmpathyTpStreamedMediaClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = tp_streamed_media_constructor;
  object_class->dispose = tp_streamed_media_dispose;
  object_class->finalize = tp_streamed_media_finalize;
  object_class->set_property = tp_streamed_media_set_property;
  object_class->get_property = tp_streamed_media_get_property;

  g_type_class_add_private (klass, sizeof (EmpathyTpStreamedMediaPriv));

  g_object_class_install_property (object_class, PROP_ACCOUNT,
      g_param_spec_object ("account", "TpAccount", "TpAccount",
      TP_TYPE_ACCOUNT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class, PROP_CHANNEL,
      g_param_spec_object ("channel", "channel", "channel",
      TP_TYPE_CHANNEL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_CONTACT,
      g_param_spec_object ("contact", "Call contact", "Call contact",
      EMPATHY_TYPE_CONTACT,
      G_PARAM_READABLE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_STATUS,
      g_param_spec_uint ("status", "Call status",
      "Call status", 0, 255, 0, G_PARAM_READABLE | G_PARAM_STATIC_NICK |
      G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_AUDIO_STREAM,
      g_param_spec_pointer ("audio-stream", "Audio stream data",
      "Audio stream data",
      G_PARAM_READABLE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  g_object_class_install_property (object_class, PROP_VIDEO_STREAM,
      g_param_spec_pointer ("video-stream", "Video stream data",
      "Video stream data",
      G_PARAM_READABLE | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB));

  signals[AUDIO_STREAM_ERROR] =
    g_signal_new ("audio-stream-error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE,
      2, G_TYPE_UINT, G_TYPE_STRING);

  signals[VIDEO_STREAM_ERROR] =
    g_signal_new ("video-stream-error",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE,
      2, G_TYPE_UINT, G_TYPE_STRING);
}

static void
empathy_tp_streamed_media_init (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (call,
    EMPATHY_TYPE_TP_STREAMED_MEDIA, EmpathyTpStreamedMediaPriv);

  call->priv = priv;
  priv->status = EMPATHY_TP_STREAMED_MEDIA_STATUS_READYING;
  priv->contact = NULL;
  priv->audio = g_slice_new0 (EmpathyTpStreamedMediaStream);
  priv->video = g_slice_new0 (EmpathyTpStreamedMediaStream);
  priv->audio->exists = FALSE;
  priv->video->exists = FALSE;
}

EmpathyTpStreamedMedia *
empathy_tp_streamed_media_new (TpAccount *account,
    TpChannel *channel)
{
  g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);
  g_return_val_if_fail (TP_IS_CHANNEL (channel), NULL);

  return g_object_new (EMPATHY_TYPE_TP_STREAMED_MEDIA,
      "account", account,
      "channel", channel,
      NULL);
}

void
empathy_tp_streamed_media_accept_incoming_call (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);
  TpHandle self_handle;
  GArray handles = {(gchar *) &self_handle, 1};

  g_return_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call));
  g_return_if_fail (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_PENDING);

  if (!priv->is_incoming)
    return;

  DEBUG ("Accepting incoming call");

  self_handle = tp_channel_group_get_self_handle (priv->channel);
  tp_cli_channel_interface_group_call_add_members (priv->channel, -1,
      &handles, NULL, NULL, NULL, NULL, NULL);
}

void
empathy_tp_streamed_media_close (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call));

  if (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_CLOSED)
      return;

  DEBUG ("Closing channel");

  tp_cli_channel_call_close (priv->channel, -1,
      NULL, NULL, NULL, NULL);

  priv->status = EMPATHY_TP_STREAMED_MEDIA_STATUS_CLOSED;
  g_object_notify (G_OBJECT (call), "status");
}

void
empathy_tp_streamed_media_request_video_stream_direction (EmpathyTpStreamedMedia *call,
                                                gboolean is_sending)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);
  guint new_direction;

  g_return_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call));
  g_return_if_fail (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_ACCEPTED);

  DEBUG ("Requesting video stream direction - is_sending: %d", is_sending);

  if (!priv->video->exists)
    {
      if (is_sending)
          tp_streamed_media_request_streams_for_capabilities (call,
              EMPATHY_CAPABILITIES_VIDEO);
      return;
    }

  if (is_sending)
      new_direction = priv->video->direction | TP_MEDIA_STREAM_DIRECTION_SEND;
  else
      new_direction = priv->video->direction & ~TP_MEDIA_STREAM_DIRECTION_SEND;

  tp_cli_channel_type_streamed_media_call_request_stream_direction (priv->channel,
      -1, priv->video->id, new_direction,
      (tp_cli_channel_type_streamed_media_callback_for_request_stream_direction)
      tp_streamed_media_async_cb, NULL, NULL, G_OBJECT (call));
}

void
empathy_tp_streamed_media_start_tone (EmpathyTpStreamedMedia *call, TpDTMFEvent event)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call));
  g_return_if_fail (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_ACCEPTED);

  if (!priv->audio->exists)
      return;

  tp_cli_channel_interface_dtmf_call_start_tone (priv->channel, -1,
      priv->audio->id, event,
      (tp_cli_channel_interface_dtmf_callback_for_start_tone) tp_streamed_media_async_cb,
      "starting tone", NULL, G_OBJECT (call));
}

void
empathy_tp_streamed_media_stop_tone (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call));
  g_return_if_fail (priv->status == EMPATHY_TP_STREAMED_MEDIA_STATUS_ACCEPTED);

  if (!priv->audio->exists)
      return;

  tp_cli_channel_interface_dtmf_call_stop_tone (priv->channel, -1,
      priv->audio->id,
      (tp_cli_channel_interface_dtmf_callback_for_stop_tone) tp_streamed_media_async_cb,
      "stoping tone", NULL, G_OBJECT (call));
}

gboolean
empathy_tp_streamed_media_has_dtmf (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_val_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call), FALSE);

  return tp_proxy_has_interface_by_id (priv->channel,
      TP_IFACE_QUARK_CHANNEL_INTERFACE_DTMF);
}

/**
 * empathy_tp_streamed_media_is_receiving_video:
 * @call: the call
 *
 * Indicates if the call is receiving video or not.
 *
 * Returns: %TRUE if the call is currently receiving video, %FALSE otherwise.
 */
gboolean
empathy_tp_streamed_media_is_receiving_video (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_val_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call), FALSE);

  if (!priv->video->exists)
    return FALSE;

  return priv->video->direction & TP_MEDIA_STREAM_DIRECTION_RECEIVE ?
      TRUE : FALSE;
}

/**
 * empathy_tp_streamed_media_is_sending_video:
 * @call: the call
 *
 * Indicates if the call is sending video or not.
 *
 * Returns: %TRUE if the call is currently sending video, %FALSE otherwise.
 */
gboolean
empathy_tp_streamed_media_is_sending_video (EmpathyTpStreamedMedia *call)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (call);

  g_return_val_if_fail (EMPATHY_IS_TP_STREAMED_MEDIA (call), FALSE);

  if (!priv->video->exists)
    return FALSE;

  return priv->video->direction & TP_MEDIA_STREAM_DIRECTION_SEND ?
      TRUE : FALSE;
}

const gchar *
empathy_tp_streamed_media_get_connection_manager (EmpathyTpStreamedMedia *self)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);

  return tp_account_get_connection_manager (priv->account);
}

gboolean
empathy_tp_streamed_media_has_initial_video (EmpathyTpStreamedMedia *self)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);
  GHashTable *props;
  gboolean initial_video;
  gboolean valid;

  if (priv->channel == NULL)
    return FALSE;

  g_object_get (priv->channel, "channel-properties", &props, NULL);

  initial_video = tp_asv_get_boolean (props,
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo", &valid);
  if (!valid)
    initial_video = FALSE;

  g_hash_table_unref (props);
  return initial_video;
}

static void
leave_remove_members_cb (TpChannel *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyTpStreamedMedia *self = user_data;

  if (error == NULL)
    return;

  DEBUG ("RemoveMembers failed (%s); closing the channel", error->message);
  empathy_tp_streamed_media_close (self);
}

void
empathy_tp_streamed_media_leave (EmpathyTpStreamedMedia *self)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);
  TpHandle self_handle;
  GArray array = { (gchar *) &self_handle, 1 };

  if (!tp_proxy_has_interface_by_id (priv->channel,
        TP_IFACE_QUARK_CHANNEL_INTERFACE_GROUP))
    {
      empathy_tp_streamed_media_close (self);
      return;
    }

  self_handle = tp_channel_group_get_self_handle (priv->channel);
  if (self_handle == 0)
    {
      /* we are not member of the channel */
      empathy_tp_streamed_media_close (self);
      return;
    }

  tp_cli_channel_interface_group_call_remove_members (priv->channel, -1, &array,
      "", leave_remove_members_cb, self, NULL, G_OBJECT (self));
}

EmpathyTpStreamedMediaStatus
empathy_tp_streamed_media_get_status (EmpathyTpStreamedMedia *self)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);

  return priv->status;
}

TpAccount *
empathy_tp_streamed_media_get_account (EmpathyTpStreamedMedia *self)
{
  EmpathyTpStreamedMediaPriv *priv = GET_PRIV (self);

  return priv->account;
}

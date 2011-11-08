/*
 * empathy-streamed-media-handler.c - Source for EmpathyStreamedMediaHandler
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
#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>

#include <telepathy-farsight/channel.h>
#include <telepathy-farsight/stream.h>

#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-call-utils.h>

#include "empathy-streamed-media-handler.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE(EmpathyStreamedMediaHandler, empathy_streamed_media_handler, G_TYPE_OBJECT)

/* signal enum */
enum {
  CONFERENCE_ADDED,
  SRC_PAD_ADDED,
  SINK_PAD_ADDED,
  REQUEST_RESOURCE,
  CLOSED,
  STREAM_CLOSED,
  CANDIDATES_CHANGED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

enum {
  PROP_TP_STREAMED_MEDIA = 1,
  PROP_GST_BUS,
  PROP_CONTACT,
  PROP_INITIAL_AUDIO,
  PROP_INITIAL_VIDEO,
  PROP_SEND_AUDIO_CODEC,
  PROP_SEND_VIDEO_CODEC,
  PROP_RECV_AUDIO_CODECS,
  PROP_RECV_VIDEO_CODECS,
  PROP_AUDIO_REMOTE_CANDIDATE,
  PROP_VIDEO_REMOTE_CANDIDATE,
  PROP_AUDIO_LOCAL_CANDIDATE,
  PROP_VIDEO_LOCAL_CANDIDATE,
};

/* private structure */

typedef struct {
  gboolean dispose_has_run;
  EmpathyTpStreamedMedia *call;
  EmpathyContact *contact;
  TfChannel *tfchannel;
  gboolean initial_audio;
  gboolean initial_video;

  FsCodec *send_audio_codec;
  FsCodec *send_video_codec;
  GList *recv_audio_codecs;
  GList *recv_video_codecs;
  FsCandidate *audio_remote_candidate;
  FsCandidate *video_remote_candidate;
  FsCandidate *audio_local_candidate;
  FsCandidate *video_local_candidate;
} EmpathyStreamedMediaHandlerPriv;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyStreamedMediaHandler)

static void
empathy_streamed_media_handler_dispose (GObject *object)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (object);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->contact != NULL)
    g_object_unref (priv->contact);

  priv->contact = NULL;

  if (priv->tfchannel != NULL)
    g_object_unref (priv->tfchannel);

  priv->tfchannel = NULL;

  if (priv->call != NULL)
    {
      empathy_tp_streamed_media_close (priv->call);
      g_object_unref (priv->call);
    }

  priv->call = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (empathy_streamed_media_handler_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_streamed_media_handler_parent_class)->dispose (object);
}

static void
empathy_streamed_media_handler_finalize (GObject *object)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (object);

  fs_codec_destroy (priv->send_audio_codec);
  fs_codec_destroy (priv->send_video_codec);
  fs_codec_list_destroy (priv->recv_audio_codecs);
  fs_codec_list_destroy (priv->recv_video_codecs);
  fs_candidate_destroy (priv->audio_remote_candidate);
  fs_candidate_destroy (priv->video_remote_candidate);
  fs_candidate_destroy (priv->audio_local_candidate);
  fs_candidate_destroy (priv->video_local_candidate);

  if (G_OBJECT_CLASS (empathy_streamed_media_handler_parent_class)->finalize)
    G_OBJECT_CLASS (empathy_streamed_media_handler_parent_class)->finalize (object);
}

static void
empathy_streamed_media_handler_init (EmpathyStreamedMediaHandler *obj)
{
  EmpathyStreamedMediaHandlerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_STREAMED_MEDIA_HANDLER, EmpathyStreamedMediaHandlerPriv);

  obj->priv = priv;
}

static void
empathy_streamed_media_handler_constructed (GObject *object)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (object);

  if (priv->contact == NULL)
    {
      g_object_get (priv->call, "contact", &(priv->contact), NULL);
    }
}

static void
empathy_streamed_media_handler_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CONTACT:
        priv->contact = g_value_dup_object (value);
        break;
      case PROP_TP_STREAMED_MEDIA:
        priv->call = g_value_dup_object (value);
        break;
      case PROP_INITIAL_AUDIO:
        priv->initial_audio = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_VIDEO:
        priv->initial_video = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_streamed_media_handler_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_TP_STREAMED_MEDIA:
        g_value_set_object (value, priv->call);
        break;
      case PROP_INITIAL_AUDIO:
        g_value_set_boolean (value, priv->initial_audio);
        break;
      case PROP_INITIAL_VIDEO:
        g_value_set_boolean (value, priv->initial_video);
        break;
      case PROP_SEND_AUDIO_CODEC:
        g_value_set_boxed (value, priv->send_audio_codec);
        break;
      case PROP_SEND_VIDEO_CODEC:
        g_value_set_boxed (value, priv->send_video_codec);
        break;
      case PROP_RECV_AUDIO_CODECS:
        g_value_set_boxed (value, priv->recv_audio_codecs);
        break;
      case PROP_RECV_VIDEO_CODECS:
        g_value_set_boxed (value, priv->recv_video_codecs);
        break;
      case PROP_AUDIO_REMOTE_CANDIDATE:
        g_value_set_boxed (value, priv->audio_remote_candidate);
        break;
      case PROP_VIDEO_REMOTE_CANDIDATE:
        g_value_set_boxed (value, priv->video_remote_candidate);
        break;
      case PROP_AUDIO_LOCAL_CANDIDATE:
        g_value_set_boxed (value, priv->audio_local_candidate);
        break;
      case PROP_VIDEO_LOCAL_CANDIDATE:
        g_value_set_boxed (value, priv->video_local_candidate);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
empathy_streamed_media_handler_class_init (EmpathyStreamedMediaHandlerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (EmpathyStreamedMediaHandlerPriv));

  object_class->constructed = empathy_streamed_media_handler_constructed;
  object_class->set_property = empathy_streamed_media_handler_set_property;
  object_class->get_property = empathy_streamed_media_handler_get_property;
  object_class->dispose = empathy_streamed_media_handler_dispose;
  object_class->finalize = empathy_streamed_media_handler_finalize;

  param_spec = g_param_spec_object ("contact",
    "contact", "The remote contact",
    EMPATHY_TYPE_CONTACT,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object ("tp-call",
    "tp-call", "The calls channel wrapper",
    EMPATHY_TYPE_TP_STREAMED_MEDIA,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TP_STREAMED_MEDIA, param_spec);

  param_spec = g_param_spec_boolean ("initial-audio",
    "initial-audio", "Whether the call should start with audio",
    TRUE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO,
      param_spec);

  param_spec = g_param_spec_boolean ("initial-video",
    "initial-video", "Whether the call should start with video",
    FALSE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_VIDEO,
    param_spec);

  param_spec = g_param_spec_boxed ("send-audio-codec",
    "send audio codec", "Codec used to encode the outgoing video stream",
    FS_TYPE_CODEC,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEND_AUDIO_CODEC,
    param_spec);

  param_spec = g_param_spec_boxed ("send-video-codec",
    "send video codec", "Codec used to encode the outgoing video stream",
    FS_TYPE_CODEC,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEND_VIDEO_CODEC,
    param_spec);

  param_spec = g_param_spec_boxed ("recv-audio-codecs",
    "recvs audio codec", "Codecs used to decode the incoming audio stream",
    FS_TYPE_CODEC_LIST,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RECV_AUDIO_CODECS,
    param_spec);

  param_spec = g_param_spec_boxed ("recv-video-codecs",
    "recvs video codec", "Codecs used to decode the incoming video stream",
    FS_TYPE_CODEC_LIST,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RECV_VIDEO_CODECS,
    param_spec);

  param_spec = g_param_spec_boxed ("audio-remote-candidate",
    "audio remote candidate",
    "Remote candidate used for the audio stream",
    FS_TYPE_CANDIDATE,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_AUDIO_REMOTE_CANDIDATE, param_spec);

  param_spec = g_param_spec_boxed ("video-remote-candidate",
    "video remote candidate",
    "Remote candidate used for the video stream",
    FS_TYPE_CANDIDATE,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_VIDEO_REMOTE_CANDIDATE, param_spec);

  param_spec = g_param_spec_boxed ("audio-local-candidate",
    "audio local candidate",
    "Local candidate used for the audio stream",
    FS_TYPE_CANDIDATE,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_AUDIO_REMOTE_CANDIDATE, param_spec);

  param_spec = g_param_spec_boxed ("video-local-candidate",
    "video local candidate",
    "Local candidate used for the video stream",
    FS_TYPE_CANDIDATE,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_VIDEO_REMOTE_CANDIDATE, param_spec);

  signals[CONFERENCE_ADDED] =
    g_signal_new ("conference-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE,
      1, FS_TYPE_CONFERENCE);

  signals[SRC_PAD_ADDED] =
    g_signal_new ("src-pad-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN,
      2, GST_TYPE_PAD, G_TYPE_UINT);

  signals[SINK_PAD_ADDED] =
    g_signal_new ("sink-pad-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN,
      2, GST_TYPE_PAD, G_TYPE_UINT);

  signals[REQUEST_RESOURCE] =
    g_signal_new ("request-resource", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      g_signal_accumulator_true_handled, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE,
      0);

  signals[STREAM_CLOSED] =
    g_signal_new ("stream-closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, TF_TYPE_STREAM);

  signals[CANDIDATES_CHANGED] =
    g_signal_new ("candidates-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * empathy_streamed_media_handler_new_for_contact:
 * @contact: an #EmpathyContact
 *
 * Creates a new #EmpathyStreamedMediaHandler with contact @contact.
 *
 * Return value: a new #EmpathyStreamedMediaHandler
 */
EmpathyStreamedMediaHandler *
empathy_streamed_media_handler_new_for_contact (EmpathyContact *contact)
{
  return EMPATHY_STREAMED_MEDIA_HANDLER (g_object_new (EMPATHY_TYPE_STREAMED_MEDIA_HANDLER,
    "contact", contact, NULL));
}

EmpathyStreamedMediaHandler *
empathy_streamed_media_handler_new_for_channel (EmpathyTpStreamedMedia *call)
{
  return EMPATHY_STREAMED_MEDIA_HANDLER (g_object_new (EMPATHY_TYPE_STREAMED_MEDIA_HANDLER,
    "tp-call", call,
    "initial-video", empathy_tp_streamed_media_is_receiving_video (call),
    NULL));
}

static void
update_sending_codec (EmpathyStreamedMediaHandler *self,
    FsCodec *codec,
    FsSession *session)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);
  FsMediaType type;

  if (codec == NULL || session == NULL)
    return;

  g_object_get (session, "media-type", &type, NULL);

  if (type == FS_MEDIA_TYPE_AUDIO)
    {
      priv->send_audio_codec = fs_codec_copy (codec);
      g_object_notify (G_OBJECT (self), "send-audio-codec");
    }
  else if (type == FS_MEDIA_TYPE_VIDEO)
    {
      priv->send_video_codec = fs_codec_copy (codec);
      g_object_notify (G_OBJECT (self), "send-video-codec");
    }
}

static void
update_receiving_codec (EmpathyStreamedMediaHandler *self,
    GList *codecs,
    FsStream *stream)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);
  FsSession *session;
  FsMediaType type;

  if (codecs == NULL || stream == NULL)
    return;

  g_object_get (stream, "session", &session, NULL);
  if (session == NULL)
    return;

  g_object_get (session, "media-type", &type, NULL);

  if (type == FS_MEDIA_TYPE_AUDIO)
    {
      priv->recv_audio_codecs = fs_codec_list_copy (codecs);
      g_object_notify (G_OBJECT (self), "recv-audio-codecs");
    }
  else if (type == FS_MEDIA_TYPE_VIDEO)
    {
      priv->recv_video_codecs = fs_codec_list_copy (codecs);
      g_object_notify (G_OBJECT (self), "recv-video-codecs");
    }

  g_object_unref (session);
}

static void
update_candidates (EmpathyStreamedMediaHandler *self,
    FsCandidate *remote_candidate,
    FsCandidate *local_candidate,
    FsStream *stream)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);
  FsSession *session;
  FsMediaType type;

  if (stream == NULL)
    return;

  g_object_get (stream, "session", &session, NULL);
  if (session == NULL)
    return;

  g_object_get (session, "media-type", &type, NULL);

  if (type == FS_MEDIA_TYPE_AUDIO)
    {
      if (remote_candidate != NULL)
        {
          fs_candidate_destroy (priv->audio_remote_candidate);
          priv->audio_remote_candidate = fs_candidate_copy (remote_candidate);
          g_object_notify (G_OBJECT (self), "audio-remote-candidate");
        }

      if (local_candidate != NULL)
        {
          fs_candidate_destroy (priv->audio_local_candidate);
          priv->audio_local_candidate = fs_candidate_copy (local_candidate);
          g_object_notify (G_OBJECT (self), "audio-local-candidate");
        }

      g_signal_emit (G_OBJECT (self), signals[CANDIDATES_CHANGED], 0,
          FS_MEDIA_TYPE_AUDIO);
    }
  else if (type == FS_MEDIA_TYPE_VIDEO)
    {
      if (remote_candidate != NULL)
        {
          fs_candidate_destroy (priv->video_remote_candidate);
          priv->video_remote_candidate = fs_candidate_copy (remote_candidate);
          g_object_notify (G_OBJECT (self), "video-remote-candidate");
        }

      if (local_candidate != NULL)
        {
          fs_candidate_destroy (priv->video_local_candidate);
          priv->video_local_candidate = fs_candidate_copy (local_candidate);
          g_object_notify (G_OBJECT (self), "video-local-candidate");
        }

      g_signal_emit (G_OBJECT (self), signals[CANDIDATES_CHANGED], 0,
          FS_MEDIA_TYPE_VIDEO);
    }

  g_object_unref (session);
}

void
empathy_streamed_media_handler_bus_message (EmpathyStreamedMediaHandler *handler,
  GstBus *bus, GstMessage *message)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (handler);
  const GstStructure *s = gst_message_get_structure (message);

  if (priv->tfchannel == NULL)
    return;

  if (s != NULL &&
      gst_structure_has_name (s, "farsight-send-codec-changed"))
    {
      const GValue *val;
      FsCodec *codec;
      FsSession *session;

      val = gst_structure_get_value (s, "codec");
      codec = g_value_get_boxed (val);

      val = gst_structure_get_value (s, "session");
      session = g_value_get_object (val);

      update_sending_codec (handler, codec, session);
    }
  else if (s != NULL &&
      gst_structure_has_name (s, "farsight-recv-codecs-changed"))
    {
      const GValue *val;
      GList *codecs;
      FsStream *stream;

      val = gst_structure_get_value (s, "codecs");
      codecs = g_value_get_boxed (val);

      val = gst_structure_get_value (s, "stream");
      stream = g_value_get_object (val);

      update_receiving_codec (handler, codecs, stream);
    }
  else if (s != NULL &&
      gst_structure_has_name (s, "farsight-new-active-candidate-pair"))
    {
      const GValue *val;
      FsCandidate *remote_candidate, *local_candidate;
      FsStream *stream;

      val = gst_structure_get_value (s, "remote-candidate");
      remote_candidate = g_value_get_boxed (val);

      val = gst_structure_get_value (s, "local-candidate");
      local_candidate = g_value_get_boxed (val);

      val = gst_structure_get_value (s, "stream");
      stream = g_value_get_object (val);

      update_candidates (handler, remote_candidate, local_candidate, stream);
    }

  tf_channel_bus_message (priv->tfchannel, message);
}

static void
empathy_streamed_media_handler_tf_channel_session_created_cb (TfChannel *tfchannel,
  FsConference *conference, FsParticipant *participant,
  EmpathyStreamedMediaHandler *self)
{
  g_signal_emit (G_OBJECT (self), signals[CONFERENCE_ADDED], 0,
    GST_ELEMENT (conference));
}

static gboolean
src_pad_added_error_idle (gpointer data)
{
  TfStream *stream = data;

  tf_stream_error (stream, TP_MEDIA_STREAM_ERROR_MEDIA_ERROR,
      "Could not link sink");
  g_object_unref (stream);

  return FALSE;
}

static void
empathy_streamed_media_handler_tf_stream_src_pad_added_cb (TfStream *stream,
  GstPad *pad, FsCodec *codec, EmpathyStreamedMediaHandler  *handler)
{
  guint media_type;
  gboolean retval;

  g_object_get (stream, "media-type", &media_type, NULL);

  g_signal_emit (G_OBJECT (handler), signals[SRC_PAD_ADDED], 0,
      pad, media_type, &retval);

  if (!retval)
    g_idle_add (src_pad_added_error_idle, g_object_ref (stream));
}


static gboolean
empathy_streamed_media_handler_tf_stream_request_resource_cb (TfStream *stream,
  guint direction, EmpathyTpStreamedMedia *call)
{
  gboolean ret;
  guint media_type;

  g_object_get (G_OBJECT (stream), "media-type", &media_type, NULL);

  g_signal_emit (G_OBJECT (call),
    signals[REQUEST_RESOURCE], 0, media_type, direction, &ret);

  return ret;
}

static void
empathy_streamed_media_handler_tf_stream_closed_cb (TfStream *stream,
  EmpathyStreamedMediaHandler *handler)
{
  g_signal_emit (handler, signals[STREAM_CLOSED], 0, stream);
}

static void
empathy_streamed_media_handler_tf_channel_stream_created_cb (TfChannel *tfchannel,
  TfStream *stream, EmpathyStreamedMediaHandler *handler)
{
  guint media_type;
  GstPad *spad;
  gboolean retval;
  FsStream *fs_stream;
  GList *codecs;
  FsSession *session;
  FsCodec *codec;

  g_signal_connect (stream, "src-pad-added",
      G_CALLBACK (empathy_streamed_media_handler_tf_stream_src_pad_added_cb), handler);
  g_signal_connect (stream, "request-resource",
      G_CALLBACK (empathy_streamed_media_handler_tf_stream_request_resource_cb),
        handler);
  g_signal_connect (stream, "closed",
      G_CALLBACK (empathy_streamed_media_handler_tf_stream_closed_cb), handler);

  g_object_get (stream, "media-type", &media_type,
    "sink-pad", &spad, NULL);

  g_signal_emit (G_OBJECT (handler), signals[SINK_PAD_ADDED], 0,
      spad, media_type, &retval);

 if (!retval)
      tf_stream_error (stream, TP_MEDIA_STREAM_ERROR_MEDIA_ERROR,
          "Could not link source");

 /* Get sending codec */
 g_object_get (stream, "farsight-session", &session, NULL);
 g_object_get (session, "current-send-codec", &codec, NULL);

 update_sending_codec (handler, codec, session);

 tp_clear_object (&session);
 tp_clear_object (&codec);

 /* Get receiving codec */
 g_object_get (stream, "farsight-stream", &fs_stream, NULL);
 g_object_get (fs_stream, "current-recv-codecs", &codecs, NULL);

 update_receiving_codec (handler, codecs, fs_stream);

 fs_codec_list_destroy (codecs);
 tp_clear_object (&fs_stream);

 gst_object_unref (spad);
}

static void
empathy_streamed_media_handler_tf_channel_closed_cb (TfChannel *tfchannel,
  EmpathyStreamedMediaHandler *handler)
{
  g_signal_emit (G_OBJECT (handler), signals[CLOSED], 0);
}

static void
empathy_streamed_media_handler_start_tpfs (EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);
  TpChannel *channel;

  g_object_get (priv->call, "channel", &channel, NULL);

  g_assert (channel != NULL);

  priv->tfchannel = tf_channel_new (channel);

  /* Set up the telepathy farsight channel */
  g_signal_connect (priv->tfchannel, "session-created",
      G_CALLBACK (empathy_streamed_media_handler_tf_channel_session_created_cb), self);
  g_signal_connect (priv->tfchannel, "stream-created",
      G_CALLBACK (empathy_streamed_media_handler_tf_channel_stream_created_cb), self);
  g_signal_connect (priv->tfchannel, "closed",
      G_CALLBACK (empathy_streamed_media_handler_tf_channel_closed_cb), self);

  g_object_unref (channel);
}

static void
empathy_streamed_media_handler_request_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyStreamedMediaHandler *self = EMPATHY_STREAMED_MEDIA_HANDLER (user_data);
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);
  TpChannel *channel;
  GError *error = NULL;
  TpAccountChannelRequest *req = TP_ACCOUNT_CHANNEL_REQUEST (source);
  TpAccount *account;

  channel = tp_account_channel_request_create_and_handle_channel_finish (req,
      result, NULL, &error);
  if (channel == NULL)
    {
      DEBUG ("Failed to create the channel: %s", error->message);
      g_error_free (error);
      return;
    }

  account = tp_account_channel_request_get_account (req);

  priv->call = empathy_tp_streamed_media_new (account, channel);

  g_object_notify (G_OBJECT (self), "tp-call");

  empathy_streamed_media_handler_start_tpfs (self);

  g_object_unref (channel);
}

void
empathy_streamed_media_handler_start_call (EmpathyStreamedMediaHandler *handler,
    gint64 timestamp)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (handler);
  TpAccountChannelRequest *req;
  TpAccount *account;
  GHashTable *request;

  if (priv->call != NULL)
    {
      empathy_streamed_media_handler_start_tpfs (handler);
      empathy_tp_streamed_media_accept_incoming_call (priv->call);
      return;
    }

  /* No TpStreamedMedia object (we are redialing). Request a new media channel that
   * will be used to create a new EmpathyTpStreamedMedia. */
  g_assert (priv->contact != NULL);

  account = empathy_contact_get_account (priv->contact);
  request = empathy_call_create_streamed_media_request (
      empathy_contact_get_id (priv->contact),
      priv->initial_audio, priv->initial_video);

  req = tp_account_channel_request_new (account, request, timestamp);

  tp_account_channel_request_create_and_handle_channel_async (req, NULL,
      empathy_streamed_media_handler_request_cb, handler);

  g_object_unref (req);
  g_hash_table_unref (request);
}

/**
 * empathy_streamed_media_handler_stop_call:
 * @handler: an #EmpathyStreamedMediaHandler
 *
 * Closes the #EmpathyStreamedMediaHandler's call and frees its resources.
 */
void
empathy_streamed_media_handler_stop_call (EmpathyStreamedMediaHandler *handler)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (handler);

  if (priv->call != NULL)
    {
      empathy_tp_streamed_media_leave (priv->call);
      g_object_unref (priv->call);
    }

  priv->call = NULL;
}

/**
 * empathy_streamed_media_handler_has_initial_video:
 * @handler: an #EmpathyStreamedMediaHandler
 *
 * Return %TRUE if the call managed by this #EmpathyStreamedMediaHandler was
 * created with video enabled
 *
 * Return value: %TRUE if the call was created as a video conversation.
 */
gboolean
empathy_streamed_media_handler_has_initial_video (EmpathyStreamedMediaHandler *handler)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (handler);

  return priv->initial_video;
}

FsCodec *
empathy_streamed_media_handler_get_send_audio_codec (EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->send_audio_codec;
}

FsCodec *
empathy_streamed_media_handler_get_send_video_codec (EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->send_video_codec;
}

GList *
empathy_streamed_media_handler_get_recv_audio_codecs (EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->recv_audio_codecs;
}

GList *
empathy_streamed_media_handler_get_recv_video_codecs (EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->recv_video_codecs;
}

FsCandidate *
empathy_streamed_media_handler_get_audio_remote_candidate (
    EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->audio_remote_candidate;
}

FsCandidate *
empathy_streamed_media_handler_get_audio_local_candidate (
    EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->audio_local_candidate;
}

FsCandidate *
empathy_streamed_media_handler_get_video_remote_candidate (
    EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->video_remote_candidate;
}

FsCandidate *
empathy_streamed_media_handler_get_video_local_candidate (
    EmpathyStreamedMediaHandler *self)
{
  EmpathyStreamedMediaHandlerPriv *priv = GET_PRIV (self);

  return priv->video_local_candidate;
}

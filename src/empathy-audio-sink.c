/*
 * empathy-gst-audio-sink.c - Source for EmpathyGstAudioSink
 * Copyright (C) 2008 Collabora Ltd.
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

#include <gst/audio/audio.h>
#include <gst/interfaces/streamvolume.h>

#include <telepathy-glib/telepathy-glib.h>

#include <libempathy-gtk/empathy-call-utils.h>

#include "empathy-audio-sink.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE(EmpathyGstAudioSink, empathy_audio_sink, GST_TYPE_BIN)

/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
    "sink%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ( GST_AUDIO_INT_PAD_TEMPLATE_CAPS " ; "
        GST_AUDIO_FLOAT_PAD_TEMPLATE_CAPS)
);

enum {
  PROP_VOLUME = 1,
};

struct _EmpathyGstAudioSinkPrivate
{
  GstElement *sink;
  gboolean echo_cancel;
};

#define EMPATHY_GST_AUDIO_SINK_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_GST_AUDIO_SINK, \
  EmpathyGstAudioSinkPrivate))

static void
empathy_audio_sink_init (EmpathyGstAudioSink *self)
{
  self->priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (self);
  self->priv->echo_cancel = TRUE;
}

static GstPad * empathy_audio_sink_request_new_pad (GstElement *self,
  GstPadTemplate *templ,
  const gchar* name);

static void empathy_audio_sink_release_pad (GstElement *self,
  GstPad *pad);

static void
empathy_audio_sink_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
      case PROP_VOLUME:
        empathy_audio_sink_set_volume (EMPATHY_GST_AUDIO_SINK (object),
          g_value_get_double (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_audio_sink_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  switch (property_id)
    {
      case PROP_VOLUME:
        g_value_set_double (value,
          empathy_audio_sink_get_volume (EMPATHY_GST_AUDIO_SINK (object)));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_audio_sink_class_init (EmpathyGstAudioSinkClass
  *empathy_audio_sink_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_audio_sink_class);
  GstElementClass *element_class =
    GST_ELEMENT_CLASS (empathy_audio_sink_class);
  GParamSpec *param_spec;

  gst_element_class_add_pad_template (element_class,
    gst_static_pad_template_get (&sink_template));

  g_type_class_add_private (empathy_audio_sink_class,
    sizeof (EmpathyGstAudioSinkPrivate));

  object_class->set_property = empathy_audio_sink_set_property;
  object_class->get_property = empathy_audio_sink_get_property;

  element_class->request_new_pad = empathy_audio_sink_request_new_pad;
  element_class->release_pad = empathy_audio_sink_release_pad;

  param_spec = g_param_spec_double ("volume", "Volume", "volume control",
    0.0, 5.0, 1.0,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_VOLUME, param_spec);
}

GstElement *
empathy_audio_sink_new (void)
{
  static gboolean registered = FALSE;

  if (!registered) {
    if (!gst_element_register (NULL, "empathyaudiosink",
            GST_RANK_NONE, EMPATHY_TYPE_GST_AUDIO_SINK))
      return NULL;
    registered = TRUE;
  }
  return gst_element_factory_make ("empathyaudiosink", NULL);
}

static gboolean
check_volume_support (EmpathyGstAudioSink *self)
{
  gchar *name;

  if (GST_IS_STREAM_VOLUME (self->priv->sink))
    return TRUE;

  name = gst_element_get_name (self->priv->sink);
  DEBUG ("Element %s doesn't support volume", name);

  g_free (name);
  return FALSE;
}

void
empathy_audio_sink_set_volume (EmpathyGstAudioSink *sink, gdouble volume)
{
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (sink);

  if (!check_volume_support (sink))
      return;

  g_object_set (priv->sink, "volume", volume, NULL);
}

gdouble
empathy_audio_sink_get_volume (EmpathyGstAudioSink *sink)
{
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (sink);
  gdouble volume;

  if (!check_volume_support (sink))
      return 1.0;

  g_object_get (priv->sink, "volume", &volume, NULL);
  return volume;
}

static GstElement *
create_sink (EmpathyGstAudioSink *self)
{
  GstElement *sink;
  const gchar *description;

  description = g_getenv ("EMPATHY_AUDIO_SINK");

  if (description != NULL)
    {
      GError *error = NULL;

      sink = gst_parse_bin_from_description (description, TRUE, &error);
      if (sink == NULL)
        {
          DEBUG ("Failed to create bin %s: %s", description, error->message);
          g_error_free (error);
        }

      return sink;
    }

  /* Use pulsesink as default */
  sink = gst_element_factory_make ("pulsesink", NULL);
  if (sink == NULL)
    return NULL;

  empathy_call_set_stream_properties (sink, self->priv->echo_cancel);

  return sink;
}

static GstPad *
empathy_audio_sink_request_new_pad (GstElement *element,
  GstPadTemplate *templ,
  const gchar* name)
{
  EmpathyGstAudioSink *self = EMPATHY_GST_AUDIO_SINK (element);
  GstElement *bin, *volume, *resample, *audioconvert0, *audioconvert1;
  GstPad *pad = NULL;
  GstPad *subpad, *filterpad;

  bin = gst_bin_new (NULL);

  audioconvert0 = gst_element_factory_make ("audioconvert", NULL);
  if (audioconvert0 == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), audioconvert0);

  resample = gst_element_factory_make ("audioresample", NULL);
  if (resample == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), resample);

  audioconvert1 = gst_element_factory_make ("audioconvert", NULL);
  if (audioconvert1 == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), audioconvert1);

  volume = gst_element_factory_make ("volume", NULL);
  if (volume == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), volume);

  self->priv->sink = create_sink (self);
  if (self->priv->sink == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), self->priv->sink);

  if (!gst_element_link_many (audioconvert0, resample, audioconvert1,
      volume, self->priv->sink, NULL))
    goto error;

  filterpad = gst_element_get_static_pad (audioconvert0, "sink");

  if (filterpad == NULL)
    goto error;

  subpad = gst_ghost_pad_new ("sink", filterpad);
  if (!gst_element_add_pad (GST_ELEMENT (bin), subpad))
    goto error;

  gst_bin_add (GST_BIN (self), bin);

  pad = gst_ghost_pad_new (name, subpad);
  g_assert (pad != NULL);

  if (!gst_element_sync_state_with_parent (bin))
    goto error;

  if (!gst_pad_set_active (pad, TRUE))
    goto error;

  if (!gst_element_add_pad (GST_ELEMENT (self), pad))
    goto error;

  return pad;

error:
  if (pad != NULL)
    {
      gst_object_unref (pad);
    }

  gst_object_unref (bin);
  g_warning ("Failed to create output subpipeline");
  return NULL;
}

static void
empathy_audio_sink_release_pad (GstElement *element,
  GstPad *pad)
{
  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
}

void
empathy_audio_sink_set_echo_cancel (EmpathyGstAudioSink *sink,
  gboolean echo_cancel)
{
  DEBUG ("Sink echo cancellation setting: %s", echo_cancel ? "on" : "off");
  sink->priv->echo_cancel = echo_cancel;
  if (sink->priv->sink != NULL)
    empathy_call_set_stream_properties (sink->priv->sink,
      sink->priv->echo_cancel);
}

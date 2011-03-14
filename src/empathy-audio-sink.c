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
#include <gst/farsight/fs-element-added-notifier.h>

#include "empathy-audio-sink.h"


G_DEFINE_TYPE(EmpathyGstAudioSink, empathy_audio_sink, GST_TYPE_BIN)

/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif
typedef struct {
  GstPad *pad;
  GstElement *bin;
  GstElement *volume;
  GstElement *sink;
} AudioBin;

static AudioBin *
audio_bin_new (GstPad *pad,
    GstElement *bin,
    GstElement *volume,
    GstElement *sink)
{
  AudioBin *result = g_slice_new0 (AudioBin);

  result->pad = pad;
  result->bin = bin;
  result->volume = gst_object_ref (volume);
  result->sink = sink;

  return result;
}

static void
audio_bin_free (AudioBin *bin)
{
  gst_object_unref (bin->volume);
  g_slice_free (AudioBin, bin);
}


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
  gboolean dispose_has_run;
  FsElementAddedNotifier *notifier;

  gdouble volume;

  /* Pad -> *owned* subbin hash */
  GHashTable *audio_bins;
};

#define EMPATHY_GST_AUDIO_SINK_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_GST_AUDIO_SINK, \
  EmpathyGstAudioSinkPrivate))

static void
empathy_audio_sink_element_added_cb (FsElementAddedNotifier *notifier,
  GstBin *bin, GstElement *element, EmpathyGstAudioSink *self)
{
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (self);

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (element), "volume"))
    {
      /* An element was added with a volume property, lets find its subbin and
       * update the volume in it */
      GHashTableIter iter;
      AudioBin *audio_bin = NULL;
      gpointer value;

      g_hash_table_iter_init (&iter, priv->audio_bins);

      while (g_hash_table_iter_next (&iter, NULL, &value))
        {
          AudioBin *b = value;

          if (gst_object_has_ancestor (GST_OBJECT (element),
              GST_OBJECT (b->bin)))
            {
              audio_bin = b;
              break;
            }
        }

      if (audio_bin == NULL)
        {
          g_warning ("Element added that doesn't belong to us ?");
          return;
        }

      /* Set the old volume to 1 and the new volume to the volume */
      g_object_set (audio_bin->volume, "volume", 1.0, NULL);
      gst_object_unref (audio_bin->volume);

      audio_bin->volume = gst_object_ref (element);
      g_object_set (audio_bin->volume, "volume", self->priv->volume, NULL);
    }
}

static void
empathy_audio_sink_init (EmpathyGstAudioSink *self)
{
  EmpathyGstAudioSinkPrivate *priv;

  priv = self->priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (self);

  priv->volume = 1.0;

  priv->audio_bins = g_hash_table_new_full (g_direct_hash, g_direct_equal,
    NULL, (GDestroyNotify) audio_bin_free);

  priv->notifier = fs_element_added_notifier_new ();
  g_signal_connect (priv->notifier, "element-added",
    G_CALLBACK (empathy_audio_sink_element_added_cb), self);
}

static void empathy_audio_sink_dispose (GObject *object);
static void empathy_audio_sink_finalize (GObject *object);

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

  object_class->dispose = empathy_audio_sink_dispose;
  object_class->finalize = empathy_audio_sink_finalize;

  object_class->set_property = empathy_audio_sink_set_property;
  object_class->get_property = empathy_audio_sink_get_property;

  element_class->request_new_pad = empathy_audio_sink_request_new_pad;
  element_class->release_pad = empathy_audio_sink_release_pad;

  param_spec = g_param_spec_double ("volume", "Volume", "volume control",
    0.0, 5.0, 1.0,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_VOLUME, param_spec);
}

void
empathy_audio_sink_dispose (GObject *object)
{
  EmpathyGstAudioSink *self = EMPATHY_GST_AUDIO_SINK (object);
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->notifier != NULL)
    g_object_unref (priv->notifier);
  priv->notifier = NULL;

  if (priv->audio_bins != NULL)
    g_hash_table_unref (priv->audio_bins);
  priv->audio_bins = NULL;

  if (G_OBJECT_CLASS (empathy_audio_sink_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_audio_sink_parent_class)->dispose (object);
}

void
empathy_audio_sink_finalize (GObject *object)
{
  //EmpathyGstAudioSink *self = EMPATHY_GST_AUDIO_SINK (object);
  //EmpathyGstAudioSinkPrivate *priv =
  //  EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (empathy_audio_sink_parent_class)->finalize (object);
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

void
empathy_audio_sink_set_volume (EmpathyGstAudioSink *sink, gdouble volume)
{
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (sink);
  GHashTableIter iter;
  gpointer value;

  priv->volume = volume;
  g_hash_table_iter_init (&iter, priv->audio_bins);

  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      AudioBin *b = value;
      g_object_set (b->volume, "volume", volume, NULL);
    }
}

gdouble
empathy_audio_sink_get_volume (EmpathyGstAudioSink *sink)
{
  EmpathyGstAudioSinkPrivate *priv = EMPATHY_GST_AUDIO_SINK_GET_PRIVATE (sink);
  return priv->volume;
}

static GstPad *
empathy_audio_sink_request_new_pad (GstElement *element,
  GstPadTemplate *templ,
  const gchar* name)
{
  EmpathyGstAudioSink *self = EMPATHY_GST_AUDIO_SINK (element);
  GstElement *bin, *sink, *volume, *resample, *audioconvert0, *audioconvert1;
  GstPad *pad = NULL;
  GstPad *subpad, *filterpad;
  AudioBin *audiobin;

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

  sink = gst_element_factory_make ("gconfaudiosink", NULL);
  if (sink == NULL)
    goto error;

  gst_bin_add (GST_BIN (bin), sink);
  fs_element_added_notifier_add (self->priv->notifier, GST_BIN (sink));

  if (!gst_element_link_many (audioconvert0, resample, audioconvert1,
      volume, sink, NULL))
    goto error;

  filterpad = gst_element_get_static_pad (audioconvert0, "sink");

  if (filterpad == NULL)
    goto error;

  subpad = gst_ghost_pad_new ("sink", filterpad);
  if (!gst_element_add_pad (GST_ELEMENT (bin), subpad))
    goto error;


  /* Ensure that state changes only happen _after_ the element has been added
   * to the hash table. But add it to the bin first so we can create our
   * ghostpad (if we create the ghostpad before adding it to the bin it will
   * get unlinked) */
  gst_element_set_locked_state (GST_ELEMENT (bin), TRUE);
  gst_bin_add (GST_BIN (self), bin);

  pad = gst_ghost_pad_new (name, subpad);
  g_assert (pad != NULL);

  audiobin = audio_bin_new (pad, bin, volume, sink);

  g_hash_table_insert (self->priv->audio_bins, pad, audiobin);

  gst_element_set_locked_state (GST_ELEMENT (bin), FALSE);

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
      g_hash_table_remove (self->priv->audio_bins, pad);
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
  EmpathyGstAudioSink *self = EMPATHY_GST_AUDIO_SINK (element);
  AudioBin *abin;

  abin = g_hash_table_lookup (self->priv->audio_bins, pad);
  g_hash_table_steal (self->priv->audio_bins, pad);

  if (abin == NULL)
    {
      g_warning ("Releasing a pad that doesn't belong to us ?");
      return;
    }

  gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);

  gst_element_set_locked_state (abin->bin, TRUE);
  gst_element_set_state (abin->bin, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (self), abin->bin);

  audio_bin_free (abin);
}

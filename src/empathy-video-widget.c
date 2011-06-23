/*
 * empathy-gst-gtk-widget.c - Source for EmpathyVideoWidget
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

#include <gst/base/gstbasesink.h>
#include <clutter-gtk/clutter-gtk.h>
#include <clutter-gst/clutter-gst.h>

#include "empathy-video-widget.h"

G_DEFINE_TYPE(EmpathyVideoWidget, empathy_video_widget,
  GTK_CLUTTER_TYPE_EMBED)


/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

enum {
  PROP_GST_ELEMENT = 1,
  PROP_GST_BUS,
  PROP_SYNC,
  PROP_ASYNC,
};

/* private structure */
typedef struct _EmpathyVideoWidgetPriv EmpathyVideoWidgetPriv;

struct _EmpathyVideoWidgetPriv
{
  gboolean dispose_has_run;
  GstBus *bus;
  GstElement *videosink, *sink;
  GstPad *sink_pad;
  gulong notify_allocation_id;
};

#define GET_PRIV(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
  EMPATHY_TYPE_VIDEO_WIDGET, EmpathyVideoWidgetPriv))

static void empathy_video_widget_dispose (GObject *object);

static void
on_stage_allocation_changed (ClutterActor *stage, GParamSpec *pspec, ClutterBox *box)
{
  gfloat w, h;
  clutter_actor_get_size (stage, &w, &h);
  clutter_actor_set_size (CLUTTER_ACTOR (box), w, h);
}

static ClutterActor*
create_clutter_texture(EmpathyVideoWidget *object)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);
  ClutterActor           *texture, *stage, *box;
  ClutterLayoutManager   *layout;

  stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (object));
  g_assert (stage != NULL);
  clutter_stage_set_color (CLUTTER_STAGE(stage), CLUTTER_COLOR_Black);


  layout = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
    CLUTTER_BIN_ALIGNMENT_CENTER);
  g_assert (layout != NULL);
  box = clutter_box_new (layout);
  g_assert (box != NULL);

  clutter_container_add_actor (CLUTTER_CONTAINER (stage), box);
  priv->notify_allocation_id = g_signal_connect (stage, "notify::allocation",
    G_CALLBACK(on_stage_allocation_changed), box);


  texture = clutter_texture_new ();
  g_assert (texture != NULL);

  clutter_texture_set_keep_aspect_ratio (CLUTTER_TEXTURE (texture), TRUE);
  g_object_ref (G_OBJECT (texture));
  clutter_box_pack (CLUTTER_BOX (box), texture, NULL, NULL);

  return texture;
}


static void
empathy_video_widget_init (EmpathyVideoWidget *object)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);
  GstElement *colorspace, *videoscale;
  ClutterActor *texture;
  GstPad *pad;

  texture = create_clutter_texture (object);

  priv->videosink = gst_bin_new (NULL);

  gst_object_ref (priv->videosink);
  gst_object_sink (priv->videosink);

  priv->sink_pad = gst_element_get_static_pad (priv->videosink, "sink");

  priv->sink = clutter_gst_video_sink_new (CLUTTER_TEXTURE (texture));
  g_object_unref (G_OBJECT (texture));
  g_assert (priv->sink != NULL);

  videoscale = gst_element_factory_make ("videoscale", NULL);
  g_assert (videoscale != NULL);

  g_object_set (videoscale, "qos", FALSE, NULL);

  colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);
  g_assert (colorspace != NULL);

  g_object_set (colorspace, "qos", FALSE, NULL);

  /* keep a reference so we can set it's "sync" or "async" properties */
  gst_object_ref (priv->sink);
  gst_bin_add_many (GST_BIN (priv->videosink), colorspace, videoscale,
    priv->sink, NULL);

  if (!gst_element_link (colorspace, videoscale))
    g_error ("Failed to link ffmpegcolorspace and videoscale");

  if (!gst_element_link (videoscale, priv->sink))
    g_error ("Failed to link videoscale and gconfvideosink");

  pad = gst_element_get_static_pad (colorspace, "sink");
  g_assert (pad != NULL);

  priv->sink_pad = gst_ghost_pad_new ("sink", pad);
  if (!gst_element_add_pad  (priv->videosink, priv->sink_pad))
    g_error ("Couldn't add sink ghostpad to the bin");

  gst_object_unref (pad);
}


static void
empathy_video_widget_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);
  gboolean boolval;

  switch (property_id)
    {
      case PROP_GST_BUS:
        priv->bus = g_value_dup_object (value);
        break;
      case PROP_SYNC:
        boolval = g_value_get_boolean (value);
        gst_base_sink_set_sync (GST_BASE_SINK (priv->sink), boolval);
        break;
      case PROP_ASYNC:
        boolval = g_value_get_boolean (value);
        gst_base_sink_set_async_enabled (GST_BASE_SINK (priv->sink), boolval);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_video_widget_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);
  gboolean boolval;

  switch (property_id)
    {
      case PROP_GST_ELEMENT:
        g_value_set_object (value, priv->videosink);
        break;
      case PROP_GST_BUS:
        g_value_set_object (value, priv->bus);
        break;
      case PROP_SYNC:
        boolval = gst_base_sink_get_sync (GST_BASE_SINK (priv->sink));
        g_value_set_boolean (value, boolval);
        break;
      case PROP_ASYNC:
        boolval = gst_base_sink_is_async_enabled (GST_BASE_SINK (priv->sink));
        g_value_set_boolean (value, boolval);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
empathy_video_widget_class_init (
  EmpathyVideoWidgetClass *empathy_video_widget_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_video_widget_class);
  GParamSpec *param_spec;

  g_type_class_add_private (empathy_video_widget_class,
    sizeof (EmpathyVideoWidgetPriv));

  object_class->dispose = empathy_video_widget_dispose;

  object_class->set_property = empathy_video_widget_set_property;
  object_class->get_property = empathy_video_widget_get_property;

  param_spec = g_param_spec_object ("gst-element",
    "gst-element", "The underlaying gstreamer element",
    GST_TYPE_ELEMENT,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_GST_ELEMENT, param_spec);

  param_spec = g_param_spec_object ("gst-bus",
    "gst-bus",
    "The toplevel bus from the pipeline in which this bin will be added",
    GST_TYPE_BUS,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_GST_BUS, param_spec);

  param_spec = g_param_spec_boolean ("sync",
    "sync",
    "Whether the underlying sink should be sync or not",
    TRUE,
    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SYNC, param_spec);

  param_spec = g_param_spec_boolean ("async",
    "async",
    "Whether the underlying sink should be async or not",
    TRUE,
    G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ASYNC, param_spec);
}

static void
empathy_video_widget_dispose (GObject *object)
{
  EmpathyVideoWidget *self = EMPATHY_VIDEO_WIDGET (object);
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->notify_allocation_id != 0) {
    ClutterActor *stage;
    stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (object));
    g_signal_handler_disconnect (stage, priv->notify_allocation_id);
    priv->notify_allocation_id = 0;
  }


  if (priv->bus != NULL)
    g_object_unref (priv->bus);

  priv->bus = NULL;

  if (priv->videosink != NULL)
    g_object_unref (priv->videosink);

  priv->videosink = NULL;

  if (priv->sink != NULL)
    g_object_unref (priv->sink);

  priv->sink = NULL;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (empathy_video_widget_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_video_widget_parent_class)->dispose (object);
}

GtkWidget *
empathy_video_widget_new_with_size (GstBus *bus, gint width, gint height)
{
  /* ignoring width, height: will use the full extent of the container */
  return empathy_video_widget_new (bus);
}

GtkWidget *
empathy_video_widget_new (GstBus *bus)
{
  g_return_val_if_fail (bus != NULL, NULL);

  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_VIDEO_WIDGET,
    "gst-bus", bus,
    NULL));
}

GstPad *
empathy_video_widget_get_sink (EmpathyVideoWidget *widget)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (widget);

  return priv->sink_pad;
}

GstElement *
empathy_video_widget_get_element (EmpathyVideoWidget *widget)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (widget);

  return priv->videosink;
}

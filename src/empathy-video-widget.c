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

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gdk/gdkx.h>
#include <gst/interfaces/xoverlay.h>
#include <gst/farsight/fs-element-added-notifier.h>

#include "empathy-video-widget.h"

G_DEFINE_TYPE(EmpathyVideoWidget, empathy_video_widget,
  GTK_TYPE_DRAWING_AREA)

static void empathy_video_widget_element_added_cb (
  FsElementAddedNotifier *notifier, GstBin *bin, GstElement *element,
  EmpathyVideoWidget *self);

static void empathy_video_widget_sync_message_cb (
  GstBus *bus, GstMessage *message, EmpathyVideoWidget *self);

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
  PROP_MIN_WIDTH,
  PROP_MIN_HEIGHT,
  PROP_SYNC,
  PROP_ASYNC,
  PROP_FLIP_VIDEO,
};

/* private structure */
typedef struct _EmpathyVideoWidgetPriv EmpathyVideoWidgetPriv;

struct _EmpathyVideoWidgetPriv
{
  gboolean dispose_has_run;
  GstBus *bus;
  GstElement *videosink;
  GstPad *sink_pad;
  GstElement *overlay;
  GstElement *flip;
  FsElementAddedNotifier *notifier;
  gint min_width;
  gint min_height;
  gboolean sync;
  gboolean async;
  gboolean flip_video;
  guintptr xid;

  GMutex *lock;
};

#define GET_PRIV(o)     (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
  EMPATHY_TYPE_VIDEO_WIDGET, EmpathyVideoWidgetPriv))

static void
empathy_video_widget_init (EmpathyVideoWidget *obj)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (obj);
  GdkRGBA black;

  priv->lock = g_mutex_new ();

  priv->notifier = fs_element_added_notifier_new ();
  g_signal_connect (priv->notifier, "element-added",
    G_CALLBACK (empathy_video_widget_element_added_cb),
    obj);

  if (gdk_rgba_parse (&black, "Black"))
    gtk_widget_override_background_color (GTK_WIDGET (obj), 0, &black);

  gtk_widget_set_double_buffered (GTK_WIDGET (obj), FALSE);
}

static void
empathy_video_widget_realized (GtkWidget *widget, gpointer user_data)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (widget);

  /* requesting the XID forces the GdkWindow to be native in GTK+ 2.18
   * onwards, requesting the native window in a thread causes a BadWindowID,
   * so we need to request it now. We could call gdk_window_ensure_native(),
   * but that would mean we require GTK+ 2.18, so instead we call this */
  priv->xid = GDK_WINDOW_XID (gtk_widget_get_window (GTK_WIDGET (widget)));
}

static void
empathy_video_widget_constructed (GObject *object)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);
  GstElement *colorspace, *videoscale, *sink;
  GstPad *pad;

  g_signal_connect (object, "realize",
      G_CALLBACK (empathy_video_widget_realized), NULL);

  priv->videosink = gst_bin_new (NULL);

  gst_object_ref (priv->videosink);
  gst_object_sink (priv->videosink);

  priv->sink_pad = gst_element_get_static_pad (priv->videosink, "sink");

  sink = gst_element_factory_make ("gconfvideosink", NULL);
  g_assert (sink != NULL);

  videoscale = gst_element_factory_make ("videoscale", NULL);
  g_assert (videoscale != NULL);

  g_object_set (videoscale, "qos", FALSE, NULL);

  colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);
  g_assert (colorspace != NULL);

  g_object_set (colorspace, "qos", FALSE, NULL);

  priv->flip = gst_element_factory_make ("videoflip", NULL);
  g_assert (priv->flip != NULL);

  gst_bin_add_many (GST_BIN (priv->videosink), colorspace, videoscale,
    priv->flip, sink, NULL);

  if (!gst_element_link (colorspace, videoscale))
    g_error ("Failed to link ffmpegcolorspace and videoscale");

  if (!gst_element_link (videoscale, priv->flip))
    g_error ("Failed to link videoscale and videoflip");

  if (!gst_element_link (priv->flip, sink))
    g_error ("Failed to link videoflip and gconfvideosink");

  pad = gst_element_get_static_pad (colorspace, "sink");
  g_assert (pad != NULL);

  priv->sink_pad = gst_ghost_pad_new ("sink", pad);
  if (!gst_element_add_pad  (priv->videosink, priv->sink_pad))
    g_error ("Couldn't add sink ghostpad to the bin");

  gst_object_unref (pad);

  fs_element_added_notifier_add (priv->notifier, GST_BIN (priv->videosink));
  gst_bus_enable_sync_message_emission (priv->bus);

  g_signal_connect (priv->bus, "sync-message",
    G_CALLBACK (empathy_video_widget_sync_message_cb), object);

  gtk_widget_set_size_request (GTK_WIDGET (object), priv->min_width,
    priv->min_height);
}

static void empathy_video_widget_dispose (GObject *object);
static void empathy_video_widget_finalize (GObject *object);


static gboolean empathy_video_widget_draw (GtkWidget *widget,
  cairo_t *cr);

static void
empathy_video_widget_element_set_sink_properties (EmpathyVideoWidget *self);

static void
empathy_video_widget_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_GST_BUS:
        priv->bus = g_value_dup_object (value);
        break;
      case PROP_MIN_WIDTH:
        priv->min_width = g_value_get_int (value);
        break;
      case PROP_MIN_HEIGHT:
        priv->min_height = g_value_get_int (value);
        break;
      case PROP_SYNC:
        priv->sync = g_value_get_boolean (value);
        empathy_video_widget_element_set_sink_properties (
          EMPATHY_VIDEO_WIDGET (object));
        break;
      case PROP_ASYNC:
        priv->async = g_value_get_boolean (value);
        empathy_video_widget_element_set_sink_properties (
          EMPATHY_VIDEO_WIDGET (object));
        break;
      case PROP_FLIP_VIDEO:
        priv->flip_video = g_value_get_boolean (value);
        gst_util_set_object_arg (G_OBJECT (priv->flip), "method",
            priv->flip_video ? "horizontal-flip" : "none");
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

  switch (property_id)
    {
      case PROP_GST_ELEMENT:
        g_value_set_object (value, priv->videosink);
        break;
      case PROP_GST_BUS:
        g_value_set_object (value, priv->bus);
        break;
      case PROP_MIN_WIDTH:
        g_value_set_int (value, priv->min_width);
        break;
      case PROP_MIN_HEIGHT:
        g_value_set_int (value, priv->min_height);
        break;
      case PROP_SYNC:
        g_value_set_boolean (value, priv->sync);
        break;
      case PROP_ASYNC:
        g_value_set_boolean (value, priv->async);
        break;
      case PROP_FLIP_VIDEO:
        g_value_set_boolean (value, priv->flip_video);
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
  GtkWidgetClass *widget_class =
    GTK_WIDGET_CLASS (empathy_video_widget_class);
  GParamSpec *param_spec;

  g_type_class_add_private (empathy_video_widget_class,
    sizeof (EmpathyVideoWidgetPriv));

  object_class->dispose = empathy_video_widget_dispose;
  object_class->finalize = empathy_video_widget_finalize;
  object_class->constructed = empathy_video_widget_constructed;

  object_class->set_property = empathy_video_widget_set_property;
  object_class->get_property = empathy_video_widget_get_property;

  widget_class->draw = empathy_video_widget_draw;

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

  param_spec = g_param_spec_int ("min-width",
    "min-width",
    "Minimal width of the widget",
    0, G_MAXINT, EMPATHY_VIDEO_WIDGET_DEFAULT_WIDTH,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MIN_WIDTH, param_spec);

  param_spec = g_param_spec_int ("min-height",
    "min-height",
    "Minimal height of the widget",
    0, G_MAXINT, EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT,
    G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MIN_HEIGHT, param_spec);

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

  param_spec = g_param_spec_boolean ("flip-video",
    "flip video",
    "Whether the video should be flipped horizontally or not",
    FALSE,
    G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_FLIP_VIDEO, param_spec);
}

void
empathy_video_widget_dispose (GObject *object)
{
  EmpathyVideoWidget *self = EMPATHY_VIDEO_WIDGET (object);
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_signal_handlers_disconnect_by_func (priv->bus,
    empathy_video_widget_sync_message_cb, object);

  if (priv->bus != NULL)
    g_object_unref (priv->bus);

  priv->bus = NULL;

  if (priv->videosink != NULL)
    g_object_unref (priv->videosink);

  priv->videosink = NULL;


  /* release any references held by the object here */

  if (G_OBJECT_CLASS (empathy_video_widget_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_video_widget_parent_class)->dispose (object);
}

void
empathy_video_widget_finalize (GObject *object)
{
  EmpathyVideoWidget *self = EMPATHY_VIDEO_WIDGET (object);
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  /* free any data held directly by the object here */
  g_mutex_free (priv->lock);

  G_OBJECT_CLASS (empathy_video_widget_parent_class)->finalize (object);
}


static void
empathy_video_widget_element_set_sink_properties_unlocked (
  EmpathyVideoWidget *self)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  if (priv->overlay == NULL)
    return;

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (priv->overlay),
      "force-aspect-ratio"))
    g_object_set (G_OBJECT (priv->overlay), "force-aspect-ratio", TRUE, NULL);

  if (g_object_class_find_property (
      G_OBJECT_GET_CLASS (priv->overlay), "sync"))
    g_object_set (G_OBJECT (priv->overlay), "sync", priv->sync, NULL);

  if (g_object_class_find_property (G_OBJECT_GET_CLASS (priv->overlay),
      "async"))
    g_object_set (G_OBJECT (priv->overlay), "async", priv->async, NULL);
}

static void
empathy_video_widget_element_set_sink_properties (EmpathyVideoWidget *self)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  g_mutex_lock (priv->lock);
  empathy_video_widget_element_set_sink_properties_unlocked (self);
  g_mutex_unlock (priv->lock);
}

static void
empathy_video_widget_element_added_cb (FsElementAddedNotifier *notifier,
  GstBin *bin, GstElement *element, EmpathyVideoWidget *self)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);

  /* We assume the overlay is the sink */
  g_mutex_lock (priv->lock);
  if (priv->overlay == NULL && GST_IS_X_OVERLAY (element))
    {
      priv->overlay = element;
      g_object_add_weak_pointer (G_OBJECT (element),
        (gpointer) &priv->overlay);
      empathy_video_widget_element_set_sink_properties_unlocked (self);
      gst_x_overlay_expose (GST_X_OVERLAY (priv->overlay));
    }
  g_mutex_unlock (priv->lock);
}

static void
empathy_video_widget_sync_message_cb (GstBus *bus, GstMessage *message,
  EmpathyVideoWidget *self)
{
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);
  const GstStructure *s;

  if (GST_MESSAGE_TYPE (message) != GST_MESSAGE_ELEMENT)
    return;

  if (GST_MESSAGE_SRC (message) != (GstObject *) priv->overlay)
    return;

  s = gst_message_get_structure (message);

  if (gst_structure_has_name (s, "prepare-xwindow-id"))
    {
      g_assert (priv->xid != 0);
      gst_x_overlay_set_window_handle (GST_X_OVERLAY (priv->overlay),
          priv->xid);
    }
}

static gboolean
empathy_video_widget_draw (GtkWidget *widget,
    cairo_t *cr)
{
  EmpathyVideoWidget *self = EMPATHY_VIDEO_WIDGET (widget);
  EmpathyVideoWidgetPriv *priv = GET_PRIV (self);
  GtkAllocation allocation;

  if (priv->overlay == NULL)
    {
      gtk_widget_get_allocation (widget, &allocation);

      gtk_render_frame (gtk_widget_get_style_context (widget), cr,
          0, 0,
          gtk_widget_get_allocated_width (widget),
          gtk_widget_get_allocated_height (widget));
      return TRUE;
    }

  gst_x_overlay_set_xwindow_id (GST_X_OVERLAY (priv->overlay),
    GDK_WINDOW_XID (gtk_widget_get_window (widget)));

  gst_x_overlay_expose (GST_X_OVERLAY (priv->overlay));

  return TRUE;
}

GtkWidget *
empathy_video_widget_new_with_size (GstBus *bus, gint width, gint height)
{
  g_return_val_if_fail (bus != NULL, NULL);

  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_VIDEO_WIDGET,
    "gst-bus", bus,
    "min-width", width,
    "min-height", height,
    NULL));
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

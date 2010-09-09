/*
 * empathy-gst-video-src.c - Source for EmpathyGstVideoSrc
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

#include <gst/interfaces/colorbalance.h>

#include "empathy-video-src.h"

G_DEFINE_TYPE(EmpathyGstVideoSrc, empathy_video_src, GST_TYPE_BIN)

/* Keep in sync with EmpathyGstVideoSrcChannel */
static const gchar *channel_names[NR_EMPATHY_GST_VIDEO_SRC_CHANNELS] = {
  "contrast", "brightness", "gamma" };

/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

/* private structure */
typedef struct _EmpathyGstVideoSrcPrivate EmpathyGstVideoSrcPrivate;

struct _EmpathyGstVideoSrcPrivate
{
  gboolean dispose_has_run;
  GstElement *src;
  /* Element implementing a ColorBalance interface */
  GstElement *balance;
};

#define EMPATHY_GST_VIDEO_SRC_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_GST_VIDEO_SRC, \
    EmpathyGstVideoSrcPrivate))
/**
 * empathy_gst_make_to_bin - make gst faktory, add to bin and link it.
 * @bin - bit to add to.
 * @src - Element to link new crated element with. IF src == NULL,
 *        we will only create Factory and add it to the bin.
 * @factoryname - factory name of factory to create.
 * we will return  pointer to new Element
 */
GstElement *
empathy_gst_make_to_bin (GstBin *bin, GstElement *src,
  const gchar *factoryname)
{
  GstElement *ret;

  if (!(ret = gst_element_factory_make (factoryname, NULL)))
  {
    g_warning ("Factory \"%s\" not found.", factoryname);
    goto error;
  }

  if (!gst_bin_add (bin, ret))
  {
    g_warning ("Can't add \"%s\" to bin.", factoryname);
    goto error;
  }

  /* do not link if src == NULL, just exit here */
  if (src == NULL)
    return ret;

  if (!gst_element_link (src, ret))
  {
    g_warning ("Can't link \"%s\".", factoryname);
    gst_bin_remove (bin, ret);
    goto error;
  }

  return ret;

error:
  gst_object_unref (ret);
  return NULL;
}


static void
empathy_video_src_init (EmpathyGstVideoSrc *obj)
{
  EmpathyGstVideoSrcPrivate *priv = EMPATHY_GST_VIDEO_SRC_GET_PRIVATE (obj);
  GstElement *element, *element_back;
  GstPad *ghost, *src;
  GstCaps *caps;
  gchar *str;

  /* allocate caps here, so we can update it by optional elements */
  caps = gst_caps_new_simple ("video/x-raw-yuv",
    "width", G_TYPE_INT, 320,
    "height", G_TYPE_INT, 240,
    NULL);

  /* allocate any data required by the object here */
  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          NULL, "gconfvideosrc")))
    g_error ("gst-plugins-good is probably missing. exit");

  /* we need to save our source to priv->src */
  priv->src = element;

  /* videomaxrate is curently optional plugin, becouse it depend on
   * gst-plugins-bad. So we won't fail if it not exist.
   * todo: in some time videorate will probably replace videomaxrate */
  /* make a backup of *element before optional GstElement */
  element_back = element;
  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          element, "videomaxrate")))
  {
    g_warning ("gst-plugins-bad is probably missing.");
    element = element_back;
  }
  else
    gst_caps_set_simple (caps,
      "framerate", GST_TYPE_FRACTION, 15, 1,
      NULL);

  str = gst_caps_to_string (caps);
  g_debug ("Current video src caps are : %s", str);
  g_free (str);

  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          element, "videoscale")))
    g_error ("gst-plugins-base is probably missing. exit");

  /* postproc_tmpnois is other optional plugin, it depend on gst-ffmpeg.
   * may be some day it will go to gst-plugins-base, than we can restrickt it */
  element_back = element;
  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          element, "postproc_tmpnoise")))
  {
    g_warning ("gst-ffmpeg is probably missing.");
    element = element_back;
  }

  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          element, "ffmpegcolorspace")))
    g_error ("gst-plugins-base is probably missing. exit");

  if (!(element = empathy_gst_make_to_bin (GST_BIN (obj),
                                          element, "capsfilter")))
    g_error ("core libgstreamer is probably missing. exit");

  g_object_set (G_OBJECT (element), "caps", caps, NULL);


  src = gst_element_get_static_pad (element, "src");
  if (!src)
    g_error ("src pad not found. exit");

  ghost = gst_ghost_pad_new ("src", src);
  if (!ghost)
    g_error ("can't create ghost pad. exit");

  if (!gst_element_add_pad (GST_ELEMENT (obj), ghost))
    g_error ("pad with the same name already existed or "
            "the pad already had another parent. exit");

  gst_object_unref (G_OBJECT (src));
}

static void empathy_video_src_dispose (GObject *object);
static void empathy_video_src_finalize (GObject *object);

static void
empathy_video_src_class_init (EmpathyGstVideoSrcClass *empathy_video_src_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_video_src_class);

  g_type_class_add_private (empathy_video_src_class,
    sizeof (EmpathyGstVideoSrcPrivate));

  object_class->dispose = empathy_video_src_dispose;
  object_class->finalize = empathy_video_src_finalize;
}

void
empathy_video_src_dispose (GObject *object)
{
  EmpathyGstVideoSrc *self = EMPATHY_GST_VIDEO_SRC (object);
  EmpathyGstVideoSrcPrivate *priv = EMPATHY_GST_VIDEO_SRC_GET_PRIVATE (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  /* release any references held by the object here */

  if (G_OBJECT_CLASS (empathy_video_src_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_video_src_parent_class)->dispose (object);
}

void
empathy_video_src_finalize (GObject *object)
{
  //EmpathyGstVideoSrc *self = EMPATHY_GST_VIDEO_SRC (object);
  //EmpathyGstVideoSrcPrivate *priv = EMPATHY_GST_VIDEO_SRC_GET_PRIVATE (self);

  /* free any data held directly by the object here */

  G_OBJECT_CLASS (empathy_video_src_parent_class)->finalize (object);
}

GstElement *
empathy_video_src_new (void)
{
  static gboolean registered = FALSE;

  if (!registered) {
    if (!gst_element_register (NULL, "empathyvideosrc",
            GST_RANK_NONE, EMPATHY_TYPE_GST_VIDEO_SRC))
      return NULL;
    registered = TRUE;
  }
  return gst_element_factory_make ("empathyvideosrc", NULL);
}

void
empathy_video_src_set_channel (GstElement *src,
  EmpathyGstVideoSrcChannel channel, guint percent)
{
  GstElement *color;
  GstColorBalance *balance;
  const GList *channels;
  GList *l;

  /* Find something supporting GstColorBalance */
  color = gst_bin_get_by_interface (GST_BIN (src), GST_TYPE_COLOR_BALANCE);

  if (color == NULL)
    return;

  balance = GST_COLOR_BALANCE (color);

  channels = gst_color_balance_list_channels (balance);

  for (l = (GList *) channels; l != NULL; l = g_list_next (l))
    {
      GstColorBalanceChannel *c = GST_COLOR_BALANCE_CHANNEL (l->data);

      if (g_ascii_strcasecmp (c->label, channel_names[channel]) == 0)
        {
          gst_color_balance_set_value (balance, c,
            ((c->max_value - c->min_value) * percent)/100
              + c->min_value);
          break;
        }
    }

  g_object_unref (color);
}

guint
empathy_video_src_get_channel (GstElement *src,
  EmpathyGstVideoSrcChannel channel)
{
  GstElement *color;
  GstColorBalance *balance;
  const GList *channels;
  GList *l;
  guint percent = 0;

  /* Find something supporting GstColorBalance */
  color = gst_bin_get_by_interface (GST_BIN (src), GST_TYPE_COLOR_BALANCE);

  if (color == NULL)
    return percent;

  balance = GST_COLOR_BALANCE (color);

  channels = gst_color_balance_list_channels (balance);

  for (l = (GList *) channels; l != NULL; l = g_list_next (l))
    {
      GstColorBalanceChannel *c = GST_COLOR_BALANCE_CHANNEL (l->data);

      if (g_ascii_strcasecmp (c->label, channel_names[channel]) == 0)
        {
          percent =
            ((gst_color_balance_get_value (balance, c)
                - c->min_value) * 100) /
              (c->max_value - c->min_value);

          break;
        }
    }

  g_object_unref (color);

  return percent;
}


guint
empathy_video_src_get_supported_channels (GstElement *src)
{
  GstElement *color;
  GstColorBalance *balance;
  const GList *channels;
  GList *l;
  guint result = 0;

  /* Find something supporting GstColorBalance */
  color = gst_bin_get_by_interface (GST_BIN (src), GST_TYPE_COLOR_BALANCE);

  if (color == NULL)
    goto out;

  balance = GST_COLOR_BALANCE (color);

  channels = gst_color_balance_list_channels (balance);

  for (l = (GList *) channels; l != NULL; l = g_list_next (l))
    {
      GstColorBalanceChannel *channel = GST_COLOR_BALANCE_CHANNEL (l->data);
      int i;

      for (i = 0; i < NR_EMPATHY_GST_VIDEO_SRC_CHANNELS; i++)
        {
          if (g_ascii_strcasecmp (channel->label, channel_names[i]) == 0)
            {
              result |= (1 << i);
              break;
            }
        }
    }

  g_object_unref (color);

out:
  return result;
}


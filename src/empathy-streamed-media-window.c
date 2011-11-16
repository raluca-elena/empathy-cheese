/*
 * empathy-streamed-media-window.c - Source for EmpathyStreamedMediaWindow
 * Copyright (C) 2008-2009 Collabora Ltd.
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

#include <math.h>

#include <gdk/gdkkeysyms.h>
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <telepathy-glib/util.h>
#include <telepathy-farsight/channel.h>
#include <telepathy-glib/util.h>

#include <gst/farsight/fs-element-added-notifier.h>
#include <gst/farsight/fs-utils.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-utils.h>
#include <libempathy-gtk/empathy-avatar-image.h>
#include <libempathy-gtk/empathy-dialpad-widget.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-sound-manager.h>
#include <libempathy-gtk/empathy-geometry.h>
#include <libempathy-gtk/empathy-images.h>

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

#include "empathy-streamed-media-window.h"
#include "empathy-streamed-media-window-fullscreen.h"
#include "empathy-video-widget.h"
#include "empathy-audio-src.h"
#include "empathy-audio-sink.h"
#include "empathy-video-src.h"
#include "ev-sidebar.h"

#define CONTENT_HBOX_BORDER_WIDTH 6
#define CONTENT_HBOX_SPACING 3
#define CONTENT_HBOX_CHILDREN_PACKING_PADDING 3

#define SELF_VIDEO_SECTION_WIDTH 160
#define SELF_VIDEO_SECTION_HEIGTH 120

/* The avatar's default width and height are set to the same value because we
   want a square icon. */
#define REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT
#define REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT \
  EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT

/* If an video input error occurs, the error message will start with "v4l" */
#define VIDEO_INPUT_ERROR_PREFIX "v4l"

/* The time interval in milliseconds between 2 outgoing rings */
#define MS_BETWEEN_RING 500

G_DEFINE_TYPE(EmpathyStreamedMediaWindow, empathy_streamed_media_window, GTK_TYPE_WINDOW)

/* signal enum */
#if 0
enum
{
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};
#endif

enum {
  PROP_STREAMED_MEDIA_HANDLER = 1,
};

typedef enum {
  CONNECTING,
  CONNECTED,
  DISCONNECTED,
  REDIALING
} CallState;

typedef enum {
  CAMERA_STATE_OFF = 0,
  CAMERA_STATE_PREVIEW,
  CAMERA_STATE_ON,
} CameraState;

/* private structure */
typedef struct _EmpathyStreamedMediaWindowPriv EmpathyStreamedMediaWindowPriv;

struct _EmpathyStreamedMediaWindowPriv
{
  gboolean dispose_has_run;
  EmpathyStreamedMediaHandler *handler;
  EmpathyContact *contact;

  guint call_state;
  gboolean outgoing;

  GtkUIManager *ui_manager;
  GtkWidget *errors_vbox;
  /* widget displays the video received from the remote user. This widget is
   * alive only during call. */
  GtkWidget *video_output;
  GtkWidget *video_preview;
  GtkWidget *remote_user_avatar_widget;
  GtkWidget *self_user_avatar_widget;
  GtkWidget *sidebar;
  GtkWidget *sidebar_button;
  GtkWidget *statusbar;
  GtkWidget *volume_button;
  GtkWidget *redial_button;
  GtkWidget *mic_button;
  GtkWidget *toolbar;
  GtkWidget *pane;
  GtkAction *redial;
  GtkAction *menu_fullscreen;
  GtkAction *action_camera_on;
  GtkWidget *tool_button_camera_off;
  GtkWidget *tool_button_camera_preview;
  GtkWidget *tool_button_camera_on;

  /* The frames and boxes that contain self and remote avatar and video
     input/output. When we redial, we destroy and re-create the boxes */
  GtkWidget *remote_user_output_frame;
  GtkWidget *self_user_output_frame;
  GtkWidget *remote_user_output_hbox;
  GtkWidget *self_user_output_hbox;

  /* We keep a reference on the hbox which contains the main content so we can
     easilly repack everything when toggling fullscreen */
  GtkWidget *content_hbox;

  /* This vbox is contained in the content_hbox and it contains the
     self_user_output_frame and the sidebar button. When toggling fullscreen,
     it needs to be repacked. We keep a reference on it for easier access. */
  GtkWidget *vbox;

  gulong video_output_motion_handler_id;
  guint bus_message_source_id;

  gdouble volume;
  GtkWidget *volume_scale;
  GtkWidget *volume_progress_bar;
  GtkAdjustment *audio_input_adj;

  GtkWidget *dtmf_panel;

  /* Details vbox */
  GtkWidget *details_vbox;
  GtkWidget *vcodec_encoding_label;
  GtkWidget *acodec_encoding_label;
  GtkWidget *vcodec_decoding_label;
  GtkWidget *acodec_decoding_label;

  GtkWidget *audio_remote_candidate_label;
  GtkWidget *audio_local_candidate_label;
  GtkWidget *video_remote_candidate_label;
  GtkWidget *video_local_candidate_label;
  GtkWidget *video_remote_candidate_info_img;
  GtkWidget *video_local_candidate_info_img;
  GtkWidget *audio_remote_candidate_info_img;
  GtkWidget *audio_local_candidate_info_img;

  GstElement *video_input;
  GstElement *audio_input;
  GstElement *audio_output;
  GstElement *pipeline;
  GstElement *video_tee;

  GstElement *funnel;

  FsElementAddedNotifier *fsnotifier;

  guint context_id;

  GTimer *timer;
  guint timer_id;

  GtkWidget *video_contrast;
  GtkWidget *video_brightness;
  GtkWidget *video_gamma;

  GMutex *lock;
  gboolean call_started;
  gboolean sending_video;
  CameraState camera_state;

  EmpathyStreamedMediaWindowFullscreen *fullscreen;
  gboolean is_fullscreen;

  /* Those fields represent the state of the window before it actually was in
     fullscreen mode. */
  gboolean sidebar_was_visible_before_fs;
  gint original_width_before_fs;
  gint original_height_before_fs;

  /* TRUE if the call should be started when the pipeline is playing */
  gboolean start_call_when_playing;
  /* TRUE if we requested to set the pipeline in the playing state */
  gboolean pipeline_playing;

  EmpathySoundManager *sound_mgr;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_STREAMED_MEDIA_WINDOW, \
    EmpathyStreamedMediaWindowPriv))

static void empathy_streamed_media_window_realized_cb (GtkWidget *widget,
  EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_delete_cb (GtkWidget *widget,
  GdkEvent *event, EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_state_event_cb (GtkWidget *widget,
  GdkEventWindowState *event, EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_sidebar_toggled_cb (GtkToggleButton *toggle,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_set_send_video (EmpathyStreamedMediaWindow *window,
  CameraState state);

static void empathy_streamed_media_window_mic_toggled_cb (
  GtkToggleToolButton *toggle, EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_sidebar_hidden_cb (EvSidebar *sidebar,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_sidebar_shown_cb (EvSidebar *sidebar,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_hangup_cb (gpointer object,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_fullscreen_cb (gpointer object,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_fullscreen_toggle (EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_video_button_press_cb (
  GtkWidget *video_output, GdkEventButton *event, EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_key_press_cb (GtkWidget *video_output,
  GdkEventKey *event, EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_video_output_motion_notify (
  GtkWidget *widget, GdkEventMotion *event, EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_video_menu_popup (EmpathyStreamedMediaWindow *window,
  guint button);

static void empathy_streamed_media_window_redial_cb (gpointer object,
  EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_restart_call (EmpathyStreamedMediaWindow *window);

static void empathy_streamed_media_window_status_message (EmpathyStreamedMediaWindow *window,
  gchar *message);

static void empathy_streamed_media_window_update_avatars_visibility (EmpathyTpStreamedMedia *call,
  EmpathyStreamedMediaWindow *window);

static gboolean empathy_streamed_media_window_bus_message (GstBus *bus,
  GstMessage *message, gpointer user_data);

static void
empathy_streamed_media_window_volume_changed_cb (GtkScaleButton *button,
  gdouble value, EmpathyStreamedMediaWindow *window);

static void block_camera_control_signals (EmpathyStreamedMediaWindow *self);
static void unblock_camera_control_signals (EmpathyStreamedMediaWindow *self);

static void
empathy_streamed_media_window_setup_toolbar (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GtkToolItem *tool_item;
  GtkWidget *camera_off_icon;
  GdkPixbuf *pixbuf, *modded_pixbuf;

  /* set the icon of the 'camera off' button by greying off the webcam icon */
  pixbuf = empathy_pixbuf_from_icon_name ("camera-web",
      GTK_ICON_SIZE_SMALL_TOOLBAR);

  modded_pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8,
      gdk_pixbuf_get_width (pixbuf),
      gdk_pixbuf_get_height (pixbuf));

  gdk_pixbuf_saturate_and_pixelate (pixbuf, modded_pixbuf, 1.0, TRUE);
  g_object_unref (pixbuf);

  camera_off_icon = gtk_image_new_from_pixbuf (modded_pixbuf);
  g_object_unref (modded_pixbuf);
  gtk_tool_button_set_icon_widget (GTK_TOOL_BUTTON (
        priv->tool_button_camera_off), camera_off_icon);

  /* Add an empty expanded GtkToolItem so the volume button is at the end of
   * the toolbar. */
  tool_item = gtk_tool_item_new ();
  gtk_tool_item_set_expand (tool_item, TRUE);
  gtk_widget_show (GTK_WIDGET (tool_item));
  gtk_toolbar_insert (GTK_TOOLBAR (priv->toolbar), tool_item, -1);

  priv->volume_button = gtk_volume_button_new ();
  /* FIXME listen to the audiosinks signals and update the button according to
   * that, for now starting out at 1.0 and assuming only the app changes the
   * volume will do */
  gtk_scale_button_set_value (GTK_SCALE_BUTTON (priv->volume_button), 1.0);
  g_signal_connect (G_OBJECT (priv->volume_button), "value-changed",
    G_CALLBACK (empathy_streamed_media_window_volume_changed_cb), self);

  tool_item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER (tool_item), priv->volume_button);
  gtk_widget_show_all (GTK_WIDGET (tool_item));
  gtk_toolbar_insert (GTK_TOOLBAR (priv->toolbar), tool_item, -1);
}

static void
dtmf_start_tone_cb (EmpathyDialpadWidget *dialpad,
    TpDTMFEvent event,
    EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  EmpathyTpStreamedMedia *call;

  g_object_get (priv->handler, "tp-call", &call, NULL);

  empathy_tp_streamed_media_start_tone (call, event);

  g_object_unref (call);
}

static void
dtmf_stop_tone_cb (EmpathyDialpadWidget *self,
    TpDTMFEvent event,
    EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  EmpathyTpStreamedMedia *call;

  g_object_get (priv->handler, "tp-call", &call, NULL);

  empathy_tp_streamed_media_stop_tone (call);

  g_object_unref (call);
}

static GtkWidget *
empathy_streamed_media_window_create_video_input_add_slider (EmpathyStreamedMediaWindow *self,
  gchar *label_text, GtkWidget *bin)
{
   GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 2);
   GtkWidget *scale = gtk_vscale_new_with_range (0, 100, 10);
   GtkWidget *label = gtk_label_new (label_text);

   gtk_widget_set_sensitive (scale, FALSE);

   gtk_container_add (GTK_CONTAINER (bin), vbox);

   gtk_range_set_inverted (GTK_RANGE (scale), TRUE);
   gtk_box_pack_start (GTK_BOX (vbox), scale, TRUE, TRUE, 0);
   gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);

   return scale;
}

static void
empathy_streamed_media_window_video_contrast_changed_cb (GtkAdjustment *adj,
  EmpathyStreamedMediaWindow *self)

{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  empathy_video_src_set_channel (priv->video_input,
    EMPATHY_GST_VIDEO_SRC_CHANNEL_CONTRAST, gtk_adjustment_get_value (adj));
}

static void
empathy_streamed_media_window_video_brightness_changed_cb (GtkAdjustment *adj,
  EmpathyStreamedMediaWindow *self)

{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  empathy_video_src_set_channel (priv->video_input,
    EMPATHY_GST_VIDEO_SRC_CHANNEL_BRIGHTNESS, gtk_adjustment_get_value (adj));
}

static void
empathy_streamed_media_window_video_gamma_changed_cb (GtkAdjustment *adj,
  EmpathyStreamedMediaWindow *self)

{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  empathy_video_src_set_channel (priv->video_input,
    EMPATHY_GST_VIDEO_SRC_CHANNEL_GAMMA, gtk_adjustment_get_value (adj));
}


static GtkWidget *
empathy_streamed_media_window_create_video_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GtkWidget *hbox;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_box_set_homogeneous (hbox, TRUE);

  priv->video_contrast = empathy_streamed_media_window_create_video_input_add_slider (
    self,  _("Contrast"), hbox);

  priv->video_brightness = empathy_streamed_media_window_create_video_input_add_slider (
    self,  _("Brightness"), hbox);

  priv->video_gamma = empathy_streamed_media_window_create_video_input_add_slider (
    self,  _("Gamma"), hbox);

  return hbox;
}

static void
empathy_streamed_media_window_setup_video_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  guint supported;
  GtkAdjustment *adj;

  supported = empathy_video_src_get_supported_channels (priv->video_input);

  if (supported & EMPATHY_GST_VIDEO_SRC_SUPPORTS_CONTRAST)
    {
      adj = gtk_range_get_adjustment (GTK_RANGE (priv->video_contrast));

      gtk_adjustment_set_value (adj,
        empathy_video_src_get_channel (priv->video_input,
          EMPATHY_GST_VIDEO_SRC_CHANNEL_CONTRAST));

      g_signal_connect (G_OBJECT (adj), "value-changed",
        G_CALLBACK (empathy_streamed_media_window_video_contrast_changed_cb), self);

      gtk_widget_set_sensitive (priv->video_contrast, TRUE);
    }

  if (supported & EMPATHY_GST_VIDEO_SRC_SUPPORTS_BRIGHTNESS)
    {
      adj = gtk_range_get_adjustment (GTK_RANGE (priv->video_brightness));

      gtk_adjustment_set_value (adj,
        empathy_video_src_get_channel (priv->video_input,
          EMPATHY_GST_VIDEO_SRC_CHANNEL_BRIGHTNESS));

      g_signal_connect (G_OBJECT (adj), "value-changed",
        G_CALLBACK (empathy_streamed_media_window_video_brightness_changed_cb), self);
      gtk_widget_set_sensitive (priv->video_brightness, TRUE);
    }

  if (supported & EMPATHY_GST_VIDEO_SRC_SUPPORTS_GAMMA)
    {
      adj = gtk_range_get_adjustment (GTK_RANGE (priv->video_gamma));

      gtk_adjustment_set_value (adj,
        empathy_video_src_get_channel (priv->video_input,
          EMPATHY_GST_VIDEO_SRC_CHANNEL_GAMMA));

      g_signal_connect (G_OBJECT (adj), "value-changed",
        G_CALLBACK (empathy_streamed_media_window_video_gamma_changed_cb), self);
      gtk_widget_set_sensitive (priv->video_gamma, TRUE);
    }
}

static void
empathy_streamed_media_window_mic_volume_changed_cb (GtkAdjustment *adj,
  EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  gdouble volume;

  volume = gtk_adjustment_get_value (adj)/100.0;

  /* Don't store the volume because of muting */
  if (volume > 0 || gtk_toggle_tool_button_get_active (
        GTK_TOGGLE_TOOL_BUTTON (priv->mic_button)))
    priv->volume = volume;

  /* Ensure that the toggle button is active if the volume is > 0 and inactive
   * if it's smaller than 0 */
  if ((volume > 0) != gtk_toggle_tool_button_get_active (
        GTK_TOGGLE_TOOL_BUTTON (priv->mic_button)))
    gtk_toggle_tool_button_set_active (
      GTK_TOGGLE_TOOL_BUTTON (priv->mic_button), volume > 0);

  empathy_audio_src_set_volume (EMPATHY_GST_AUDIO_SRC (priv->audio_input),
    volume);
}

static void
empathy_streamed_media_window_audio_input_level_changed_cb (EmpathyGstAudioSrc *src,
  gdouble level, EmpathyStreamedMediaWindow *window)
{
  gdouble value;
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  value = CLAMP (pow (10, level / 20), 0.0, 1.0);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->volume_progress_bar),
      value);
}

static GtkWidget *
empathy_streamed_media_window_create_audio_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GtkWidget *hbox, *vbox, *label;

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_box_set_homogeneous (hbox, TRUE);

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 3);

  priv->volume_scale = gtk_vscale_new_with_range (0, 150, 100);
  gtk_range_set_inverted (GTK_RANGE (priv->volume_scale), TRUE);
  label = gtk_label_new (_("Volume"));

  priv->audio_input_adj = gtk_range_get_adjustment (
    GTK_RANGE (priv->volume_scale));
  priv->volume =  empathy_audio_src_get_volume (EMPATHY_GST_AUDIO_SRC
    (priv->audio_input));
  gtk_adjustment_set_value (priv->audio_input_adj, priv->volume * 100);

  g_signal_connect (G_OBJECT (priv->audio_input_adj), "value-changed",
    G_CALLBACK (empathy_streamed_media_window_mic_volume_changed_cb), self);

  gtk_box_pack_start (GTK_BOX (vbox), priv->volume_scale, TRUE, TRUE, 3);
  gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 3);

  priv->volume_progress_bar = gtk_progress_bar_new ();

  gtk_orientable_set_orientation (GTK_ORIENTABLE (priv->volume_progress_bar),
      GTK_ORIENTATION_VERTICAL);

  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (priv->volume_progress_bar),
      0);

  gtk_box_pack_start (GTK_BOX (hbox), priv->volume_progress_bar, FALSE, FALSE,
      3);

  return hbox;
}

static void
create_video_output_widget (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstBus *bus;

  g_assert (priv->video_output == NULL);
  g_assert (priv->pipeline != NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
  priv->video_output = empathy_video_widget_new (bus);

  gtk_box_pack_start (GTK_BOX (priv->remote_user_output_hbox),
      priv->video_output, TRUE, TRUE, 0);

  gtk_widget_add_events (priv->video_output,
      GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK);
  g_signal_connect (G_OBJECT (priv->video_output), "button-press-event",
      G_CALLBACK (empathy_streamed_media_window_video_button_press_cb), self);

  g_object_unref (bus);
}

static void
create_video_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  g_assert (priv->video_input == NULL);
  priv->video_input = empathy_video_src_new ();
  gst_object_ref (priv->video_input);
  gst_object_sink (priv->video_input);
}

static void
create_audio_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  g_assert (priv->audio_input == NULL);
  priv->audio_input = empathy_audio_src_new ();
  gst_object_ref (priv->audio_input);
  gst_object_sink (priv->audio_input);

  tp_g_signal_connect_object (priv->audio_input, "peak-level-changed",
    G_CALLBACK (empathy_streamed_media_window_audio_input_level_changed_cb),
    self, 0);
}

static void
add_video_preview_to_pipeline (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstElement *preview;

  g_assert (priv->video_preview != NULL);
  g_assert (priv->pipeline != NULL);
  g_assert (priv->video_input != NULL);
  g_assert (priv->video_tee != NULL);

  preview = empathy_video_widget_get_element (
      EMPATHY_VIDEO_WIDGET (priv->video_preview));

  if (!gst_bin_add (GST_BIN (priv->pipeline), priv->video_input))
    {
      g_warning ("Could not add video input to pipeline");
      return;
    }

  if (!gst_bin_add (GST_BIN (priv->pipeline), priv->video_tee))
    {
      g_warning ("Could not add video tee to pipeline");
      return;
    }

  if (!gst_bin_add (GST_BIN (priv->pipeline), preview))
    {
      g_warning ("Could not add video preview to pipeline");
      return;
    }

  if (!gst_element_link (priv->video_input, priv->video_tee))
    {
      g_warning ("Could not link video input to video tee");
      return;
    }

  if (!gst_element_link (priv->video_tee, preview))
    {
      g_warning ("Could not link video tee to video preview");
      return;
    }
}

static void
create_video_preview (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstBus *bus;

  g_assert (priv->video_preview == NULL);
  g_assert (priv->video_tee == NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));

  priv->video_preview = empathy_video_widget_new_with_size (bus,
      SELF_VIDEO_SECTION_WIDTH, SELF_VIDEO_SECTION_HEIGTH);
  g_object_set (priv->video_preview,
      "sync", FALSE,
      "async", TRUE,
      "flip-video", TRUE,
      NULL);

  gtk_box_pack_start (GTK_BOX (priv->self_user_output_hbox),
      priv->video_preview, TRUE, TRUE, 0);

  priv->video_tee = gst_element_factory_make ("tee", NULL);
  gst_object_ref (priv->video_tee);
  gst_object_sink (priv->video_tee);

  g_object_unref (bus);
}

static void
play_camera (EmpathyStreamedMediaWindow *window,
    gboolean play)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  GstElement *preview;
  GstState state;

  if (priv->video_preview == NULL)
    {
      create_video_preview (window);
      add_video_preview_to_pipeline (window);
    }

  if (play)
    state = GST_STATE_PLAYING;
  else
    state = GST_STATE_NULL;

  preview = empathy_video_widget_get_element (
      EMPATHY_VIDEO_WIDGET (priv->video_preview));

  gst_element_set_state (preview, state);
  gst_element_set_state (priv->video_input, state);
  gst_element_set_state (priv->video_tee, state);
}

static void
display_video_preview (EmpathyStreamedMediaWindow *self,
    gboolean display)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (display)
    {
      /* Display the preview and hide the self avatar */
      DEBUG ("Show video preview");

      play_camera (self, TRUE);
      gtk_widget_show (priv->video_preview);
      gtk_widget_hide (priv->self_user_avatar_widget);
    }
  else
    {
      /* Display the self avatar and hide the preview */
      DEBUG ("Show self avatar");

      if (priv->video_preview != NULL)
        {
          gtk_widget_hide (priv->video_preview);
          play_camera (self, FALSE);
        }
      gtk_widget_show (priv->self_user_avatar_widget);
    }
}

static void
empathy_streamed_media_window_set_state_connecting (EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  empathy_streamed_media_window_status_message (window, _("Connecting…"));
  priv->call_state = CONNECTING;

  if (priv->outgoing)
    empathy_sound_manager_start_playing (priv->sound_mgr, GTK_WIDGET (window),
        EMPATHY_SOUND_PHONE_OUTGOING, MS_BETWEEN_RING);
}

static void
disable_camera (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->camera_state == CAMERA_STATE_OFF)
    return;

  DEBUG ("Disable camera");

  display_video_preview (self, FALSE);

  if (priv->camera_state == CAMERA_STATE_ON)
    empathy_streamed_media_window_set_send_video (self, CAMERA_STATE_OFF);

  block_camera_control_signals (self);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
        priv->tool_button_camera_on), FALSE);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
      priv->tool_button_camera_preview), FALSE);

  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
      priv->tool_button_camera_off), TRUE);
  gtk_radio_action_set_current_value (GTK_RADIO_ACTION (priv->action_camera_on),
      CAMERA_STATE_OFF);
  unblock_camera_control_signals (self);

  priv->camera_state = CAMERA_STATE_OFF;
}

static void
tool_button_camera_off_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (!gtk_toggle_tool_button_get_active (toggle))
    {
      if (priv->camera_state == CAMERA_STATE_OFF)
        {
          /* We can't change the state by disabling the button */
          block_camera_control_signals (self);
          gtk_toggle_tool_button_set_active (toggle, TRUE);
          unblock_camera_control_signals (self);
        }

      return;
    }

  disable_camera (self);
}

static void
enable_preview (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->camera_state == CAMERA_STATE_PREVIEW)
    return;

  DEBUG ("Enable preview");

  if (priv->camera_state == CAMERA_STATE_ON)
    {
      /* preview is already displayed so we just have to stop sending */
      empathy_streamed_media_window_set_send_video (self, CAMERA_STATE_PREVIEW);
    }
  else
    {
      display_video_preview (self, TRUE);
    }

  block_camera_control_signals (self);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
      priv->tool_button_camera_off), FALSE);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
        priv->tool_button_camera_on), FALSE);

  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
        priv->tool_button_camera_preview), TRUE);
  gtk_radio_action_set_current_value (GTK_RADIO_ACTION (priv->action_camera_on),
      CAMERA_STATE_PREVIEW);
  unblock_camera_control_signals (self);

  priv->camera_state = CAMERA_STATE_PREVIEW;
}

static void
tool_button_camera_preview_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (!gtk_toggle_tool_button_get_active (toggle))
    {
      if (priv->camera_state == CAMERA_STATE_PREVIEW)
        {
          /* We can't change the state by disabling the button */
          block_camera_control_signals (self);
          gtk_toggle_tool_button_set_active (toggle, TRUE);
          unblock_camera_control_signals (self);
        }

      return;
    }

  enable_preview (self);
}

static void
enable_camera (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->camera_state == CAMERA_STATE_ON)
    return;

  if (priv->video_input == NULL)
    {
      DEBUG ("Can't enable camera, no input");
      return;
    }


  DEBUG ("Enable camera");

  empathy_streamed_media_window_set_send_video (self, CAMERA_STATE_ON);

  block_camera_control_signals (self);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
      priv->tool_button_camera_off), FALSE);
  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
        priv->tool_button_camera_preview), FALSE);

  gtk_toggle_tool_button_set_active (GTK_TOGGLE_TOOL_BUTTON (
      priv->tool_button_camera_on), TRUE);
  gtk_radio_action_set_current_value (GTK_RADIO_ACTION (priv->action_camera_on),
      CAMERA_STATE_ON);
  unblock_camera_control_signals (self);

  priv->camera_state = CAMERA_STATE_ON;
}

static void
tool_button_camera_on_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (!gtk_toggle_tool_button_get_active (toggle))
    {
      if (priv->camera_state == CAMERA_STATE_ON)
        {
          /* We can't change the state by disabling the button */
          block_camera_control_signals (self);
          gtk_toggle_tool_button_set_active (toggle, TRUE);
          unblock_camera_control_signals (self);
        }

      return;
    }

  enable_camera (self);
}

static void
action_camera_change_cb (GtkRadioAction *action,
    GtkRadioAction *current,
    EmpathyStreamedMediaWindow *self)
{
  CameraState state;

  state = gtk_radio_action_get_current_value (current);

  switch (state)
    {
      case CAMERA_STATE_OFF:
        disable_camera (self);
        break;

      case CAMERA_STATE_PREVIEW:
        enable_preview (self);
        break;

      case CAMERA_STATE_ON:
        enable_camera (self);
        break;

      default:
        g_assert_not_reached ();
    }
}

static void
create_pipeline (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstBus *bus;

  g_assert (priv->pipeline == NULL);

  priv->pipeline = gst_pipeline_new (NULL);
  priv->pipeline_playing = FALSE;

  bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
  priv->bus_message_source_id = gst_bus_add_watch (bus,
      empathy_streamed_media_window_bus_message, self);

  g_object_unref (bus);
}


static void
empathy_streamed_media_window_init (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GtkBuilder *gui;
  GtkWidget *top_vbox;
  GtkWidget *h;
  GtkWidget *arrow;
  GtkWidget *page;
  gchar *filename;
  GtkWidget *scroll;

  filename = empathy_file_lookup ("empathy-streamed-media-window.ui", "src");
  gui = empathy_builder_get_file (filename,
    "call_window_vbox", &top_vbox,
    "errors_vbox", &priv->errors_vbox,
    "pane", &priv->pane,
    "statusbar", &priv->statusbar,
    "redial", &priv->redial_button,
    "microphone", &priv->mic_button,
    "toolbar", &priv->toolbar,
    "menuredial", &priv->redial,
    "ui_manager", &priv->ui_manager,
    "menufullscreen", &priv->menu_fullscreen,
    "camera_off", &priv->tool_button_camera_off,
    "camera_preview", &priv->tool_button_camera_preview,
    "camera_on", &priv->tool_button_camera_on,
    "action_camera_on",  &priv->action_camera_on,
    "details_vbox",  &priv->details_vbox,
    "vcodec_encoding_label", &priv->vcodec_encoding_label,
    "acodec_encoding_label", &priv->acodec_encoding_label,
    "acodec_decoding_label", &priv->acodec_decoding_label,
    "vcodec_decoding_label", &priv->vcodec_decoding_label,
    "audio_remote_candidate_label", &priv->audio_remote_candidate_label,
    "audio_local_candidate_label", &priv->audio_local_candidate_label,
    "video_remote_candidate_label", &priv->video_remote_candidate_label,
    "video_local_candidate_label", &priv->video_local_candidate_label,
    "video_remote_candidate_info_img", &priv->video_remote_candidate_info_img,
    "video_local_candidate_info_img", &priv->video_local_candidate_info_img,
    "audio_remote_candidate_info_img", &priv->audio_remote_candidate_info_img,
    "audio_local_candidate_info_img", &priv->audio_local_candidate_info_img,
    NULL);
  g_free (filename);

  empathy_builder_connect (gui, self,
    "menuhangup", "activate", empathy_streamed_media_window_hangup_cb,
    "hangup", "clicked", empathy_streamed_media_window_hangup_cb,
    "menuredial", "activate", empathy_streamed_media_window_redial_cb,
    "redial", "clicked", empathy_streamed_media_window_redial_cb,
    "microphone", "toggled", empathy_streamed_media_window_mic_toggled_cb,
    "menufullscreen", "activate", empathy_streamed_media_window_fullscreen_cb,
    "camera_off", "toggled", tool_button_camera_off_toggled_cb,
    "camera_preview", "toggled", tool_button_camera_preview_toggled_cb,
    "camera_on", "toggled", tool_button_camera_on_toggled_cb,
    "action_camera_on", "changed", action_camera_change_cb,
    NULL);

  gtk_action_set_sensitive (priv->menu_fullscreen, FALSE);

  priv->lock = g_mutex_new ();

  gtk_container_add (GTK_CONTAINER (self), top_vbox);

  priv->content_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL,
      CONTENT_HBOX_SPACING);
  gtk_container_set_border_width (GTK_CONTAINER (priv->content_hbox),
                                  CONTENT_HBOX_BORDER_WIDTH);
  gtk_paned_pack1 (GTK_PANED (priv->pane), priv->content_hbox, TRUE, FALSE);

  /* remote user output frame */
  priv->remote_user_output_frame = gtk_frame_new (NULL);
  gtk_widget_set_size_request (priv->remote_user_output_frame,
      EMPATHY_VIDEO_WIDGET_DEFAULT_WIDTH, EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT);
  gtk_box_pack_start (GTK_BOX (priv->content_hbox),
      priv->remote_user_output_frame, TRUE, TRUE,
      CONTENT_HBOX_CHILDREN_PACKING_PADDING);

  priv->remote_user_output_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  priv->remote_user_avatar_widget = gtk_image_new ();

  gtk_box_pack_start (GTK_BOX (priv->remote_user_output_hbox),
      priv->remote_user_avatar_widget, TRUE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (priv->remote_user_output_frame),
      priv->remote_user_output_hbox);

  /* self user output frame */
  priv->self_user_output_frame = gtk_frame_new (NULL);
  gtk_widget_set_size_request (priv->self_user_output_frame,
      SELF_VIDEO_SECTION_WIDTH, SELF_VIDEO_SECTION_HEIGTH);

  priv->self_user_output_hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  priv->self_user_avatar_widget = gtk_image_new ();
  gtk_box_pack_start (GTK_BOX (priv->self_user_output_hbox),
      priv->self_user_avatar_widget, TRUE, TRUE, 0);

  gtk_container_add (GTK_CONTAINER (priv->self_user_output_frame),
      priv->self_user_output_hbox);

  create_pipeline (self);
  create_video_output_widget (self);
  create_audio_input (self);
  create_video_input (self);

  priv->fsnotifier = fs_element_added_notifier_new ();
  fs_element_added_notifier_add (priv->fsnotifier, GST_BIN (priv->pipeline));

  /* The call will be started as soon the pipeline is playing */
  priv->start_call_when_playing = TRUE;

  priv->vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start (GTK_BOX (priv->content_hbox), priv->vbox,
      FALSE, FALSE, CONTENT_HBOX_CHILDREN_PACKING_PADDING);
  gtk_box_pack_start (GTK_BOX (priv->vbox), priv->self_user_output_frame,
      FALSE, FALSE, 0);

  empathy_streamed_media_window_setup_toolbar (self);

  priv->sidebar_button = gtk_toggle_button_new_with_mnemonic (_("_Sidebar"));
  arrow = gtk_arrow_new (GTK_ARROW_RIGHT, GTK_SHADOW_NONE);
  g_signal_connect (G_OBJECT (priv->sidebar_button), "toggled",
    G_CALLBACK (empathy_streamed_media_window_sidebar_toggled_cb), self);

  gtk_button_set_image (GTK_BUTTON (priv->sidebar_button), arrow);

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_box_pack_end (GTK_BOX (priv->vbox), h, FALSE, FALSE, 3);
  gtk_box_pack_end (GTK_BOX (h), priv->sidebar_button, FALSE, FALSE, 3);

  priv->sidebar = ev_sidebar_new ();
  g_signal_connect (G_OBJECT (priv->sidebar),
    "hide", G_CALLBACK (empathy_streamed_media_window_sidebar_hidden_cb), self);
  g_signal_connect (G_OBJECT (priv->sidebar),
    "show", G_CALLBACK (empathy_streamed_media_window_sidebar_shown_cb), self);
  gtk_paned_pack2 (GTK_PANED (priv->pane), priv->sidebar, FALSE, FALSE);

  page = empathy_streamed_media_window_create_audio_input (self);
  ev_sidebar_add_page (EV_SIDEBAR (priv->sidebar), "audio-input",
      _("Audio input"), page);

  page = empathy_streamed_media_window_create_video_input (self);
  ev_sidebar_add_page (EV_SIDEBAR (priv->sidebar), "video-input",
      _("Video input"), page);

  priv->dtmf_panel = empathy_dialpad_widget_new ();
  g_signal_connect (priv->dtmf_panel, "start-tone",
      G_CALLBACK (dtmf_start_tone_cb), self);
  g_signal_connect (priv->dtmf_panel, "stop-tone",
      G_CALLBACK (dtmf_stop_tone_cb), self);
  ev_sidebar_add_page (EV_SIDEBAR (priv->sidebar), "dialpad",
      _("Dialpad"), priv->dtmf_panel);

  gtk_widget_set_sensitive (priv->dtmf_panel, FALSE);

  /* Put the details vbox in a scroll window as it can require a lot of
   * horizontal space. */
  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW (scroll),
      priv->details_vbox);

  ev_sidebar_add_page (EV_SIDEBAR (priv->sidebar), "details",
      _("Details"), scroll);

  gtk_widget_show_all (top_vbox);

  gtk_widget_hide (priv->sidebar);

  priv->fullscreen = empathy_streamed_media_window_fullscreen_new (self);
  empathy_streamed_media_window_fullscreen_set_video_widget (priv->fullscreen,
      priv->video_output);
  g_signal_connect (G_OBJECT (priv->fullscreen->leave_fullscreen_button),
      "clicked", G_CALLBACK (empathy_streamed_media_window_fullscreen_cb), self);

  g_signal_connect (G_OBJECT (self), "realize",
    G_CALLBACK (empathy_streamed_media_window_realized_cb), self);

  g_signal_connect (G_OBJECT (self), "delete-event",
    G_CALLBACK (empathy_streamed_media_window_delete_cb), self);

  g_signal_connect (G_OBJECT (self), "window-state-event",
    G_CALLBACK (empathy_streamed_media_window_state_event_cb), self);

  g_signal_connect (G_OBJECT (self), "key-press-event",
      G_CALLBACK (empathy_streamed_media_window_key_press_cb), self);

  priv->timer = g_timer_new ();

  g_object_ref (priv->ui_manager);
  g_object_unref (gui);

  priv->sound_mgr = empathy_sound_manager_dup_singleton ();

  empathy_geometry_bind (GTK_WINDOW (self), "av-window");
}

/* Instead of specifying a width and a height, we specify only one size. That's
   because we want a square avatar icon.  */
static void
init_contact_avatar_with_size (EmpathyContact *contact,
    GtkWidget *image_widget,
    gint size)
{
  GdkPixbuf *pixbuf_avatar = NULL;

  if (contact != NULL)
    {
      pixbuf_avatar = empathy_pixbuf_avatar_from_contact_scaled (contact,
        size, size);
    }

  if (pixbuf_avatar == NULL)
    {
      pixbuf_avatar = empathy_pixbuf_from_icon_name_sized (
          EMPATHY_IMAGE_AVATAR_DEFAULT, size);
    }

  gtk_image_set_from_pixbuf (GTK_IMAGE (image_widget), pixbuf_avatar);

  if (pixbuf_avatar != NULL)
    g_object_unref (pixbuf_avatar);
}

static void
set_window_title (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  gchar *tmp;

  /* translators: Call is a noun and %s is the contact name. This string
   * is used in the window title */
  tmp = g_strdup_printf (_("Call with %s"),
      empathy_contact_get_alias (priv->contact));
  gtk_window_set_title (GTK_WINDOW (self), tmp);
  g_free (tmp);
}

static void
contact_name_changed_cb (EmpathyContact *contact,
    GParamSpec *pspec, EmpathyStreamedMediaWindow *self)
{
  set_window_title (self);
}

static void
contact_avatar_changed_cb (EmpathyContact *contact,
    GParamSpec *pspec, GtkWidget *avatar_widget)
{
  int size;
  GtkAllocation allocation;

  gtk_widget_get_allocation (avatar_widget, &allocation);
  size = allocation.height;

  if (size == 0)
    {
      /* the widget is not allocated yet, set a default size */
      size = MIN (REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT,
          REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH);
    }

  init_contact_avatar_with_size (contact, avatar_widget, size);
}

static void
empathy_streamed_media_window_got_self_contact_cb (TpConnection *connection,
    EmpathyContact *contact, const GError *error, gpointer user_data,
    GObject *weak_object)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  init_contact_avatar_with_size (contact, priv->self_user_avatar_widget,
      MIN (SELF_VIDEO_SECTION_WIDTH, SELF_VIDEO_SECTION_HEIGTH));

  g_signal_connect (contact, "notify::avatar",
      G_CALLBACK (contact_avatar_changed_cb), priv->self_user_avatar_widget);
}

static void
empathy_streamed_media_window_setup_avatars (EmpathyStreamedMediaWindow *self,
    EmpathyStreamedMediaHandler *handler)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  g_object_get (handler, "contact", &(priv->contact), NULL);

  if (priv->contact != NULL)
    {
      TpConnection *connection;

      set_window_title (self);

      g_signal_connect (priv->contact, "notify::name",
          G_CALLBACK (contact_name_changed_cb), self);
      g_signal_connect (priv->contact, "notify::avatar",
          G_CALLBACK (contact_avatar_changed_cb),
          priv->remote_user_avatar_widget);

      /* Retreiving the self avatar */
      connection = empathy_contact_get_connection (priv->contact);
      empathy_tp_contact_factory_get_from_handle (connection,
          tp_connection_get_self_handle (connection),
          empathy_streamed_media_window_got_self_contact_cb, self, NULL, G_OBJECT (self));
    }
  else
    {
      g_warning ("call handler doesn't have a contact");
      /* translators: Call is a noun. This string is used in the window
       * title */
      gtk_window_set_title (GTK_WINDOW (self), _("Call"));

      /* Since we can't access the remote contact, we can't get a connection
         to it and can't get the self contact (and its avatar). This means
         that we have to manually set the self avatar. */
      init_contact_avatar_with_size (NULL, priv->self_user_avatar_widget,
          MIN (SELF_VIDEO_SECTION_WIDTH, SELF_VIDEO_SECTION_HEIGTH));
    }

  init_contact_avatar_with_size (priv->contact,
      priv->remote_user_avatar_widget,
      MIN (REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH,
          REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT));

  /* The remote avatar is shown by default and will be hidden when we receive
     video from the remote side. */
  gtk_widget_hide (priv->video_output);
  gtk_widget_show (priv->remote_user_avatar_widget);
}

static void
update_send_codec (EmpathyStreamedMediaWindow *self,
    gboolean audio)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  FsCodec *codec;
  GtkWidget *widget;
  gchar *tmp;

  if (audio)
    {
      codec = empathy_streamed_media_handler_get_send_audio_codec (priv->handler);
      widget = priv->acodec_encoding_label;
    }
  else
    {
      codec = empathy_streamed_media_handler_get_send_video_codec (priv->handler);
      widget = priv->vcodec_encoding_label;
    }

  if (codec == NULL)
    return;

  tmp = g_strdup_printf ("%s/%u", codec->encoding_name, codec->clock_rate);
  gtk_label_set_text (GTK_LABEL (widget), tmp);
  g_free (tmp);
}

static void
send_audio_codec_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = user_data;

  update_send_codec (self, TRUE);
}

static void
send_video_codec_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = user_data;

  update_send_codec (self, FALSE);
}

static void
update_recv_codec (EmpathyStreamedMediaWindow *self,
    gboolean audio)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GList *codecs, *l;
  GtkWidget *widget;
  GString *str = NULL;

  if (audio)
    {
      codecs = empathy_streamed_media_handler_get_recv_audio_codecs (priv->handler);
      widget = priv->acodec_decoding_label;
    }
  else
    {
      codecs = empathy_streamed_media_handler_get_recv_video_codecs (priv->handler);
      widget = priv->vcodec_decoding_label;
    }

  if (codecs == NULL)
    return;

  for (l = codecs; l != NULL; l = g_list_next (l))
    {
      FsCodec *codec = l->data;

      if (str == NULL)
        str = g_string_new (NULL);
      else
        g_string_append (str, ", ");

      g_string_append_printf (str, "%s/%u", codec->encoding_name,
          codec->clock_rate);
    }

  gtk_label_set_text (GTK_LABEL (widget), str->str);
  g_string_free (str, TRUE);
}

static void
recv_audio_codecs_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = user_data;

  update_recv_codec (self, TRUE);
}

static void
recv_video_codecs_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = user_data;

  update_recv_codec (self, FALSE);
}

static const gchar *
candidate_type_to_str (FsCandidate *candidate)
{
  switch (candidate->type)
    {
      case FS_CANDIDATE_TYPE_HOST:
        return "host";
      case FS_CANDIDATE_TYPE_SRFLX:
        return "server reflexive";
      case FS_CANDIDATE_TYPE_PRFLX:
        return "peer reflexive";
      case FS_CANDIDATE_TYPE_RELAY:
        return "relay";
      case FS_CANDIDATE_TYPE_MULTICAST:
        return "multicast";
    }

  return NULL;
}

static const gchar *
candidate_type_to_desc (FsCandidate *candidate)
{
  switch (candidate->type)
    {
      case FS_CANDIDATE_TYPE_HOST:
        return _("The IP address as seen by the machine");
      case FS_CANDIDATE_TYPE_SRFLX:
        return _("The IP address as seen by a server on the Internet");
      case FS_CANDIDATE_TYPE_PRFLX:
        return _("The IP address of the peer as seen by the other side");
      case FS_CANDIDATE_TYPE_RELAY:
        return _("The IP address of a relay server");
      case FS_CANDIDATE_TYPE_MULTICAST:
        return _("The IP address of the multicast group");
    }

  return NULL;
}

static void
update_candidat_widget (EmpathyStreamedMediaWindow *self,
    GtkWidget *label,
    GtkWidget *img,
    FsCandidate *candidate)
{
  gchar *str;

  g_assert (candidate != NULL);
  str = g_strdup_printf ("%s %u (%s)", candidate->ip,
      candidate->port, candidate_type_to_str (candidate));

  gtk_label_set_text (GTK_LABEL (label), str);
  gtk_widget_set_tooltip_text (img, candidate_type_to_desc (candidate));

  g_free (str);
}

static void
candidates_changed_cb (GObject *object,
    FsMediaType type,
    EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  FsCandidate *candidate = NULL;

  if (type == FS_MEDIA_TYPE_VIDEO)
    {
      /* Update remote candidate */
      candidate = empathy_streamed_media_handler_get_video_remote_candidate (
          priv->handler);

      update_candidat_widget (self, priv->video_remote_candidate_label,
          priv->video_remote_candidate_info_img, candidate);

      /* Update local candidate */
      candidate = empathy_streamed_media_handler_get_video_local_candidate (
          priv->handler);

      update_candidat_widget (self, priv->video_local_candidate_label,
          priv->video_local_candidate_info_img, candidate);
    }
  else
    {
      /* Update remote candidate */
      candidate = empathy_streamed_media_handler_get_audio_remote_candidate (
          priv->handler);

      update_candidat_widget (self, priv->audio_remote_candidate_label,
          priv->audio_remote_candidate_info_img, candidate);

      /* Update local candidate */
      candidate = empathy_streamed_media_handler_get_audio_local_candidate (
          priv->handler);

      update_candidat_widget (self, priv->audio_local_candidate_label,
          priv->audio_local_candidate_info_img, candidate);
    }
}

static void
empathy_streamed_media_window_constructed (GObject *object)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (object);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  EmpathyTpStreamedMedia *call;

  g_assert (priv->handler != NULL);

  g_object_get (priv->handler, "tp-call", &call, NULL);
  priv->outgoing = (call == NULL);
  if (call != NULL)
    g_object_unref (call);

  empathy_streamed_media_window_setup_avatars (self, priv->handler);
  empathy_streamed_media_window_set_state_connecting (self);

  if (!empathy_streamed_media_handler_has_initial_video (priv->handler))
    {
      gtk_toggle_tool_button_set_active (
          GTK_TOGGLE_TOOL_BUTTON (priv->tool_button_camera_off), TRUE);
    }
  /* If call has InitialVideo, the preview will be started once the call has
   * been started (start_call()). */

  update_send_codec (self, TRUE);
  update_send_codec (self, FALSE);
  update_recv_codec (self, TRUE);
  update_recv_codec (self, FALSE);

  tp_g_signal_connect_object (priv->handler, "notify::send-audio-codec",
      G_CALLBACK (send_audio_codec_notify_cb), self, 0);
  tp_g_signal_connect_object (priv->handler, "notify::send-video-codec",
      G_CALLBACK (send_video_codec_notify_cb), self, 0);
  tp_g_signal_connect_object (priv->handler, "notify::recv-audio-codecs",
      G_CALLBACK (recv_audio_codecs_notify_cb), self, 0);
  tp_g_signal_connect_object (priv->handler, "notify::recv-video-codecs",
      G_CALLBACK (recv_video_codecs_notify_cb), self, 0);

  tp_g_signal_connect_object (priv->handler, "candidates-changed",
      G_CALLBACK (candidates_changed_cb), self, 0);
}

static void empathy_streamed_media_window_dispose (GObject *object);
static void empathy_streamed_media_window_finalize (GObject *object);

static void
empathy_streamed_media_window_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_STREAMED_MEDIA_HANDLER:
        priv->handler = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_streamed_media_window_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_STREAMED_MEDIA_HANDLER:
        g_value_set_object (value, priv->handler);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_streamed_media_window_class_init (
  EmpathyStreamedMediaWindowClass *empathy_streamed_media_window_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_streamed_media_window_class);
  GParamSpec *param_spec;

  g_type_class_add_private (empathy_streamed_media_window_class,
    sizeof (EmpathyStreamedMediaWindowPriv));

  object_class->constructed = empathy_streamed_media_window_constructed;
  object_class->set_property = empathy_streamed_media_window_set_property;
  object_class->get_property = empathy_streamed_media_window_get_property;

  object_class->dispose = empathy_streamed_media_window_dispose;
  object_class->finalize = empathy_streamed_media_window_finalize;

  param_spec = g_param_spec_object ("handler",
    "handler", "The call handler",
    EMPATHY_TYPE_STREAMED_MEDIA_HANDLER,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
    PROP_STREAMED_MEDIA_HANDLER, param_spec);
}

static void
empathy_streamed_media_window_video_stream_changed_cb (EmpathyTpStreamedMedia *call,
    GParamSpec *property, EmpathyStreamedMediaWindow *self)
{
  DEBUG ("video stream changed");
  empathy_streamed_media_window_update_avatars_visibility (call, self);
}

void
empathy_streamed_media_window_dispose (GObject *object)
{
  EmpathyTpStreamedMedia *call;
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (object);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  g_object_get (priv->handler, "tp-call", &call, NULL);

  if (call != NULL)
    {
      g_object_unref (call);
    }

  if (priv->handler != NULL)
    {
      empathy_streamed_media_handler_stop_call (priv->handler);
      g_object_unref (priv->handler);
    }
  priv->handler = NULL;

  if (priv->bus_message_source_id != 0)
    {
      g_source_remove (priv->bus_message_source_id);
      priv->bus_message_source_id = 0;
    }

  if (priv->pipeline != NULL)
    g_object_unref (priv->pipeline);
  priv->pipeline = NULL;

  if (priv->video_input != NULL)
    g_object_unref (priv->video_input);
  priv->video_input = NULL;

  if (priv->audio_input != NULL)
    g_object_unref (priv->audio_input);
  priv->audio_input = NULL;

  if (priv->video_tee != NULL)
    g_object_unref (priv->video_tee);
  priv->video_tee = NULL;

  if (priv->fsnotifier != NULL)
    g_object_unref (priv->fsnotifier);
  priv->fsnotifier = NULL;

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);
  priv->timer_id = 0;

  if (priv->ui_manager != NULL)
    g_object_unref (priv->ui_manager);
  priv->ui_manager = NULL;

  if (priv->fullscreen != NULL)
    g_object_unref (priv->fullscreen);
  priv->fullscreen = NULL;

  if (priv->contact != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->contact,
          contact_name_changed_cb, self);
      g_object_unref (priv->contact);
      priv->contact = NULL;
    }

  tp_clear_object (&priv->sound_mgr);

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (empathy_streamed_media_window_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_streamed_media_window_parent_class)->dispose (object);
}

static void
disconnect_video_output_motion_handler (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->video_output_motion_handler_id != 0)
    {
      g_signal_handler_disconnect (G_OBJECT (priv->video_output),
          priv->video_output_motion_handler_id);
      priv->video_output_motion_handler_id = 0;
    }
}

void
empathy_streamed_media_window_finalize (GObject *object)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (object);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  disconnect_video_output_motion_handler (self);

  /* free any data held directly by the object here */
  g_mutex_free (priv->lock);

  g_timer_destroy (priv->timer);

  G_OBJECT_CLASS (empathy_streamed_media_window_parent_class)->finalize (object);
}


EmpathyStreamedMediaWindow *
empathy_streamed_media_window_new (EmpathyStreamedMediaHandler *handler)
{
  return EMPATHY_STREAMED_MEDIA_WINDOW (
    g_object_new (EMPATHY_TYPE_STREAMED_MEDIA_WINDOW, "handler", handler, NULL));
}

static void
empathy_streamed_media_window_conference_added_cb (EmpathyStreamedMediaHandler *handler,
  GstElement *conference, gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GKeyFile *keyfile;

  keyfile = fs_utils_get_default_element_properties (conference);
  if (keyfile != NULL)
    fs_element_added_notifier_set_properties_from_keyfile (priv->fsnotifier,
        keyfile);

  gst_bin_add (GST_BIN (priv->pipeline), conference);

  gst_element_set_state (conference, GST_STATE_PLAYING);
}

static gboolean
empathy_streamed_media_window_request_resource_cb (EmpathyStreamedMediaHandler *handler,
  FsMediaType type, FsStreamDirection direction, gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (type != FS_MEDIA_TYPE_VIDEO)
    return TRUE;

  if (direction == FS_DIRECTION_RECV)
    return TRUE;

  /* video and direction is send */
  return priv->video_input != NULL;
}

static gboolean
empathy_streamed_media_window_reset_pipeline (EmpathyStreamedMediaWindow *self)
{
  GstStateChangeReturn state_change_return;
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (priv->pipeline == NULL)
    return TRUE;

  if (priv->bus_message_source_id != 0)
    {
      g_source_remove (priv->bus_message_source_id);
      priv->bus_message_source_id = 0;
    }

  state_change_return = gst_element_set_state (priv->pipeline, GST_STATE_NULL);

  if (state_change_return == GST_STATE_CHANGE_SUCCESS ||
        state_change_return == GST_STATE_CHANGE_NO_PREROLL)
    {
      if (priv->pipeline != NULL)
        g_object_unref (priv->pipeline);
      priv->pipeline = NULL;

      g_signal_handlers_disconnect_by_func (priv->audio_input_adj,
          empathy_streamed_media_window_mic_volume_changed_cb, self);

      if (priv->video_tee != NULL)
        g_object_unref (priv->video_tee);
      priv->video_tee = NULL;

      if (priv->video_preview != NULL)
        gtk_widget_destroy (priv->video_preview);
      priv->video_preview = NULL;

      priv->funnel = NULL;

      create_pipeline (self);
      /* Call will be started when user will hit the 'redial' button */
      priv->start_call_when_playing = FALSE;
      gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);

      return TRUE;
    }
  else
    {
      g_message ("Error: could not destroy pipeline. Closing call window");
      gtk_widget_destroy (GTK_WIDGET (self));

      return FALSE;
    }
}

static void
reset_details_pane (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  /* translators: encoding video codec is unknown */
  gtk_label_set_text (GTK_LABEL (priv->vcodec_encoding_label),
      C_("codec", "Unknown"));
  /* translators: encoding audio codec is unknown */
  gtk_label_set_text (GTK_LABEL (priv->acodec_encoding_label),
      C_("codec", "Unknown"));
  /* translators: decoding video codec is unknown */
  gtk_label_set_text (GTK_LABEL (priv->vcodec_decoding_label),
      C_("codec", "Unknown"));
  /* translators: decoding audio codec is unknown */
  gtk_label_set_text (GTK_LABEL (priv->acodec_decoding_label),
      C_("codec", "Unknown"));
}

static gboolean
empathy_streamed_media_window_disconnected (EmpathyStreamedMediaWindow *self,
    gboolean restart)
{
  gboolean could_disconnect = FALSE;
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  gboolean could_reset_pipeline;

  /* Leave full screen mode if needed */
  gtk_window_unfullscreen (GTK_WINDOW (self));

  gtk_action_set_sensitive (priv->menu_fullscreen, FALSE);

  could_reset_pipeline = empathy_streamed_media_window_reset_pipeline (self);

  if (priv->call_state == CONNECTING)
      empathy_sound_manager_stop (priv->sound_mgr,
          EMPATHY_SOUND_PHONE_OUTGOING);

  if (priv->call_state != REDIALING)
    priv->call_state = DISCONNECTED;

  if (could_reset_pipeline)
    {
      g_mutex_lock (priv->lock);

      g_timer_stop (priv->timer);

      if (priv->timer_id != 0)
        g_source_remove (priv->timer_id);
      priv->timer_id = 0;

      g_mutex_unlock (priv->lock);

      if (!restart)
        /* We are about to destroy the window, no need to update it or create
         * a video preview */
        return TRUE;

      empathy_streamed_media_window_status_message (self, _("Disconnected"));

      gtk_action_set_sensitive (priv->redial, TRUE);
      gtk_widget_set_sensitive (priv->redial_button, TRUE);

      /* Unsensitive the camera and mic button */
      gtk_widget_set_sensitive (priv->tool_button_camera_on, FALSE);
      gtk_action_set_sensitive (priv->action_camera_on, FALSE);
      gtk_widget_set_sensitive (priv->mic_button, FALSE);

      /* Be sure that the mic button is enabled */
      gtk_toggle_tool_button_set_active (
          GTK_TOGGLE_TOOL_BUTTON (priv->mic_button), TRUE);

      if (priv->camera_state == CAMERA_STATE_ON)
        {
          /* Enable the 'preview' button as we are not sending atm. */
          gtk_toggle_tool_button_set_active (
              GTK_TOGGLE_TOOL_BUTTON (priv->tool_button_camera_preview), TRUE);
        }
      else if (priv->camera_state == CAMERA_STATE_PREVIEW)
        {
          /* Restart the preview with the new pipeline. */
          display_video_preview (self, TRUE);
        }

      gtk_progress_bar_set_fraction (
          GTK_PROGRESS_BAR (priv->volume_progress_bar), 0);

      /* destroy the video output; it will be recreated when we'll redial */
      disconnect_video_output_motion_handler (self);
      gtk_widget_destroy (priv->video_output);
      priv->video_output = NULL;

      gtk_widget_show (priv->remote_user_avatar_widget);

      reset_details_pane (self);

      priv->sending_video = FALSE;
      priv->call_started = FALSE;

      could_disconnect = TRUE;

      /* TODO: display the self avatar of the preview (depends if the "Always
       * Show Video Preview" is enabled or not) */
    }

  return could_disconnect;
}


static void
empathy_streamed_media_window_channel_closed_cb (EmpathyStreamedMediaHandler *handler,
    gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  if (empathy_streamed_media_window_disconnected (self, TRUE) &&
      priv->call_state == REDIALING)
      empathy_streamed_media_window_restart_call (self);
}


static void
empathy_streamed_media_window_channel_stream_closed_cb (EmpathyStreamedMediaHandler *handler,
    TfStream *stream, gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  guint media_type;

  g_object_get (stream, "media-type", &media_type, NULL);

  /*
   * This assumes that there is only one video stream per channel...
   */

  if (media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
    {
      if (priv->funnel != NULL)
        {
          GstElement *output;

          output = empathy_video_widget_get_element (EMPATHY_VIDEO_WIDGET
              (priv->video_output));

          gst_element_set_state (output, GST_STATE_NULL);
          gst_element_set_state (priv->funnel, GST_STATE_NULL);

          gst_bin_remove (GST_BIN (priv->pipeline), output);
          gst_bin_remove (GST_BIN (priv->pipeline), priv->funnel);
          priv->funnel = NULL;
        }
    }
  else if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
    {
      if (priv->audio_output != NULL)
        {
          gst_element_set_state (priv->audio_output, GST_STATE_NULL);

          gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_output);
          priv->audio_output = NULL;
        }
    }
}

/* Called with global lock held */
static GstPad *
empathy_streamed_media_window_get_video_sink_pad (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  GstElement *output;

  if (priv->funnel == NULL)
    {
      output = empathy_video_widget_get_element (EMPATHY_VIDEO_WIDGET
        (priv->video_output));

      priv->funnel = gst_element_factory_make ("fsfunnel", NULL);

      if (!priv->funnel)
        {
          g_warning ("Could not create fsfunnel");
          return NULL;
        }

      if (!gst_bin_add (GST_BIN (priv->pipeline), priv->funnel))
        {
          gst_object_unref (priv->funnel);
          priv->funnel = NULL;
          g_warning ("Could  not add funnel to pipeline");
          return NULL;
        }

      if (!gst_bin_add (GST_BIN (priv->pipeline), output))
        {
          g_warning ("Could not add the video output widget to the pipeline");
          goto error;
        }

      if (!gst_element_link (priv->funnel, output))
        {
          g_warning ("Could not link output sink to funnel");
          goto error_output_added;
        }

      if (gst_element_set_state (output, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
          g_warning ("Could not start video sink");
          goto error_output_added;
        }

      if (gst_element_set_state (priv->funnel, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
          g_warning ("Could not start funnel");
          goto error_output_added;
        }
    }

  pad = gst_element_get_request_pad (priv->funnel, "sink%d");

  if (!pad)
    g_warning ("Could not get request pad from funnel");

  return pad;


 error_output_added:

  gst_element_set_locked_state (priv->funnel, TRUE);
  gst_element_set_locked_state (output, TRUE);

  gst_element_set_state (priv->funnel, GST_STATE_NULL);
  gst_element_set_state (output, GST_STATE_NULL);

  gst_bin_remove (GST_BIN (priv->pipeline), output);
  gst_element_set_locked_state (output, FALSE);

 error:

  gst_bin_remove (GST_BIN (priv->pipeline), priv->funnel);
  priv->funnel = NULL;

  return NULL;
}

/* Called with global lock held */
static GstPad *
empathy_streamed_media_window_get_audio_sink_pad (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  GstPadTemplate *template;

  if (priv->audio_output == NULL)
    {
      priv->audio_output = empathy_audio_sink_new ();

      if (!gst_bin_add (GST_BIN (priv->pipeline), priv->audio_output))
        {
          g_warning ("Could not add audio sink to pipeline");
          g_object_unref (priv->audio_output);
          goto error_add_output;
        }

      if (gst_element_set_state (priv->audio_output, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
        {
          g_warning ("Could not start audio sink");
          goto error;
        }
    }

  template = gst_element_class_get_pad_template (
    GST_ELEMENT_GET_CLASS (priv->audio_output), "sink%d");

  pad = gst_element_request_pad (priv->audio_output,
    template, NULL, NULL);

  if (pad == NULL)
    {
      g_warning ("Could not get sink pad from sink");
      return NULL;
    }

  return pad;

error:
  gst_element_set_locked_state (priv->audio_output, TRUE);
  gst_element_set_state (priv->audio_output, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_output);
  priv->audio_output = NULL;

error_add_output:

  return NULL;
}

static gboolean
empathy_streamed_media_window_update_timer (gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  gchar *str;
  gdouble time_;

  time_ = g_timer_elapsed (priv->timer, NULL);

  /* Translators: number of minutes:seconds the caller has been connected */
  str = g_strdup_printf (_("Connected — %d:%02dm"), (int) time_ / 60,
    (int) time_ % 60);
  empathy_streamed_media_window_status_message (self, str);
  g_free (str);

  return TRUE;
}

static void
display_error (EmpathyStreamedMediaWindow *self,
    EmpathyTpStreamedMedia *call,
    const gchar *img,
    const gchar *title,
    const gchar *desc,
    const gchar *details)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GtkWidget *info_bar;
  GtkWidget *content_area;
  GtkWidget *hbox;
  GtkWidget *vbox;
  GtkWidget *image;
  GtkWidget *label;
  gchar *txt;

  /* Create info bar */
  info_bar = gtk_info_bar_new_with_buttons (GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
      NULL);

  gtk_info_bar_set_message_type (GTK_INFO_BAR (info_bar), GTK_MESSAGE_WARNING);

  content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (info_bar));

  /* hbox containing the image and the messages vbox */
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 3);
  gtk_container_add (GTK_CONTAINER (content_area), hbox);

  /* Add image */
  image = gtk_image_new_from_icon_name (img, GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

  /* vbox containing the main message and the details expander */
  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 3);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

  /* Add text */
  txt = g_strdup_printf ("<b>%s</b>\n%s", title, desc);

  label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (label), txt);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  g_free (txt);

  gtk_box_pack_start (GTK_BOX (vbox), label, TRUE, TRUE, 0);

  /* Add details */
  if (details != NULL)
    {
      GtkWidget *expander;

      expander = gtk_expander_new (_("Technical Details"));

      txt = g_strdup_printf ("<i>%s</i>", details);

      label = gtk_label_new (NULL);
      gtk_label_set_markup (GTK_LABEL (label), txt);
      gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
      gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
      g_free (txt);

      gtk_container_add (GTK_CONTAINER (expander), label);
      gtk_box_pack_start (GTK_BOX (vbox), expander, TRUE, TRUE, 0);
    }

  g_signal_connect (info_bar, "response",
      G_CALLBACK (gtk_widget_destroy), NULL);

  gtk_box_pack_start (GTK_BOX (priv->errors_vbox), info_bar,
      FALSE, FALSE, CONTENT_HBOX_CHILDREN_PACKING_PADDING);
  gtk_widget_show_all (info_bar);
}

static gchar *
media_stream_error_to_txt (EmpathyStreamedMediaWindow *self,
    EmpathyTpStreamedMedia *call,
    gboolean audio,
    TpMediaStreamError error)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  const gchar *cm;
  gchar *url;
  gchar *result;

  switch (error)
    {
      case TP_MEDIA_STREAM_ERROR_CODEC_NEGOTIATION_FAILED:
        if (audio)
          return g_strdup_printf (
              _("%s's software does not understand any of the audio formats "
                "supported by your computer"),
            empathy_contact_get_alias (priv->contact));
        else
          return g_strdup_printf (
              _("%s's software does not understand any of the video formats "
                "supported by your computer"),
            empathy_contact_get_alias (priv->contact));

      case TP_MEDIA_STREAM_ERROR_CONNECTION_FAILED:
        return g_strdup_printf (
            _("Can't establish a connection to %s. "
              "One of you might be on a network that does not allow "
              "direct connections."),
          empathy_contact_get_alias (priv->contact));

      case TP_MEDIA_STREAM_ERROR_NETWORK_ERROR:
          return g_strdup (_("There was a failure on the network"));

      case TP_MEDIA_STREAM_ERROR_NO_CODECS:
        if (audio)
          return g_strdup (_("The audio formats necessary for this call "
                "are not installed on your computer"));
        else
          return g_strdup (_("The video formats necessary for this call "
                "are not installed on your computer"));

      case TP_MEDIA_STREAM_ERROR_INVALID_CM_BEHAVIOR:
        cm = empathy_tp_streamed_media_get_connection_manager (call);

        url = g_strdup_printf ("http://bugs.freedesktop.org/enter_bug.cgi?"
            "product=Telepathy&amp;component=%s", cm);

        result = g_strdup_printf (
            _("Something unexpected happened in a Telepathy component. "
              "Please <a href=\"%s\">report this bug</a> and attach "
              "logs gathered from the 'Debug' window in the Help menu."), url);

        g_free (url);
        return result;

      case TP_MEDIA_STREAM_ERROR_MEDIA_ERROR:
        return g_strdup (_("There was a failure in the call engine"));

      case TP_MEDIA_STREAM_ERROR_EOS:
        return g_strdup (_("The end of the stream was reached"));

      case TP_MEDIA_STREAM_ERROR_UNKNOWN:
      default:
        return NULL;
    }
}

static void
empathy_streamed_media_window_stream_error (EmpathyStreamedMediaWindow *self,
    EmpathyTpStreamedMedia *call,
    gboolean audio,
    guint code,
    const gchar *msg,
    const gchar *icon,
    const gchar *title)
{
  gchar *desc;

  desc = media_stream_error_to_txt (self, call, audio, code);
  if (desc == NULL)
    {
      /* No description, use the error message. That's not great as it's not
       * localized but it's better than nothing. */
      display_error (self, call, icon, title, msg, NULL);
    }
  else
    {
      display_error (self, call, icon, title, desc, msg);
      g_free (desc);
    }
}

static void
empathy_streamed_media_window_audio_stream_error (EmpathyTpStreamedMedia *call,
    guint code,
    const gchar *msg,
    EmpathyStreamedMediaWindow *self)
{
  empathy_streamed_media_window_stream_error (self, call, TRUE, code, msg,
      "gnome-stock-mic", _("Can't establish audio stream"));
}

static void
empathy_streamed_media_window_video_stream_error (EmpathyTpStreamedMedia *call,
    guint code,
    const gchar *msg,
    EmpathyStreamedMediaWindow *self)
{
  empathy_streamed_media_window_stream_error (self, call, FALSE, code, msg,
      "camera-web", _("Can't establish video stream"));
}

static gboolean
empathy_streamed_media_window_connected (gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  EmpathyTpStreamedMedia *call;
  gboolean can_send_video;

  empathy_sound_manager_stop (priv->sound_mgr, EMPATHY_SOUND_PHONE_OUTGOING);

  can_send_video = priv->video_input != NULL && priv->contact != NULL &&
    empathy_contact_can_voip_video (priv->contact);

  g_object_get (priv->handler, "tp-call", &call, NULL);

  tp_g_signal_connect_object (call, "notify::video-stream",
    G_CALLBACK (empathy_streamed_media_window_video_stream_changed_cb),
    self, 0);

  if (empathy_tp_streamed_media_has_dtmf (call))
    gtk_widget_set_sensitive (priv->dtmf_panel, TRUE);

  if (priv->video_input == NULL)
    empathy_streamed_media_window_set_send_video (self, CAMERA_STATE_OFF);

  priv->sending_video = can_send_video ?
    empathy_tp_streamed_media_is_sending_video (call) : FALSE;

  gtk_toggle_tool_button_set_active (
      GTK_TOGGLE_TOOL_BUTTON (priv->tool_button_camera_on),
      priv->sending_video && priv->video_input != NULL);
  gtk_widget_set_sensitive (priv->tool_button_camera_on, can_send_video);
  gtk_action_set_sensitive (priv->action_camera_on, can_send_video);

  gtk_action_set_sensitive (priv->redial, FALSE);
  gtk_widget_set_sensitive (priv->redial_button, FALSE);

  gtk_widget_set_sensitive (priv->mic_button, TRUE);

  empathy_streamed_media_window_update_avatars_visibility (call, self);

  g_object_unref (call);

  g_mutex_lock (priv->lock);

  priv->timer_id = g_timeout_add_seconds (1,
    empathy_streamed_media_window_update_timer, self);

  g_mutex_unlock (priv->lock);

  empathy_streamed_media_window_update_timer (self);

  gtk_action_set_sensitive (priv->menu_fullscreen, TRUE);

  return FALSE;
}


/* Called from the streaming thread */
static gboolean
empathy_streamed_media_window_src_added_cb (EmpathyStreamedMediaHandler *handler,
  GstPad *src, guint media_type, gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  gboolean retval = FALSE;

  GstPad *pad;

  g_mutex_lock (priv->lock);

  if (priv->call_state != CONNECTED)
    {
      g_timer_start (priv->timer);
      priv->timer_id = g_idle_add  (empathy_streamed_media_window_connected, self);
      priv->call_state = CONNECTED;
    }

  switch (media_type)
    {
      case TP_MEDIA_STREAM_TYPE_AUDIO:
        pad = empathy_streamed_media_window_get_audio_sink_pad (self);
        break;
      case TP_MEDIA_STREAM_TYPE_VIDEO:
        gtk_widget_hide (priv->remote_user_avatar_widget);
        gtk_widget_show (priv->video_output);
        pad = empathy_streamed_media_window_get_video_sink_pad (self);
        break;
      default:
        g_assert_not_reached ();
    }

  if (pad == NULL)
    goto out;

  if (GST_PAD_LINK_FAILED (gst_pad_link (src, pad)))
      g_warning ("Could not link %s sink pad",
          media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video");
  else
      retval = TRUE;

  gst_object_unref (pad);

 out:

  /* If no sink could be linked, try to add fakesink to prevent the whole call
   * aborting */

  if (!retval)
    {
      GstElement *fakesink = gst_element_factory_make ("fakesink", NULL);

      if (gst_bin_add (GST_BIN (priv->pipeline), fakesink))
        {
          GstPad *sinkpad = gst_element_get_static_pad (fakesink, "sink");
          if (gst_element_set_state (fakesink, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE ||
              GST_PAD_LINK_FAILED (gst_pad_link (src, sinkpad)))
            {
              gst_element_set_locked_state (fakesink, TRUE);
              gst_element_set_state (fakesink, GST_STATE_NULL);
              gst_bin_remove (GST_BIN (priv->pipeline), fakesink);
            }
          else
            {
              DEBUG ("Could not link real sink, linked fakesink instead");
            }
          gst_object_unref (sinkpad);
        }
      else
        {
          gst_object_unref (fakesink);
        }
    }


  g_mutex_unlock (priv->lock);

  return TRUE;
}

static gboolean
empathy_streamed_media_window_sink_added_cb (EmpathyStreamedMediaHandler *handler,
  GstPad *sink, guint media_type, gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  gboolean retval = FALSE;

  switch (media_type)
    {
      case TP_MEDIA_STREAM_TYPE_AUDIO:
        if (!gst_bin_add (GST_BIN (priv->pipeline), priv->audio_input))
          {
            g_warning ("Could not add audio source to pipeline");
            break;
          }

        pad = gst_element_get_static_pad (priv->audio_input, "src");
        if (!pad)
          {
            gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_input);
            g_warning ("Could not get source pad from audio source");
            break;
          }

        if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sink)))
          {
            gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_input);
            g_warning ("Could not link audio source to farsight");
            break;
          }

        if (gst_element_set_state (priv->audio_input, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
          {
            g_warning ("Could not start audio source");
            gst_element_set_state (priv->audio_input, GST_STATE_NULL);
            gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_input);
            break;
          }

        retval = TRUE;
        break;
      case TP_MEDIA_STREAM_TYPE_VIDEO:
        if (priv->video_input != NULL)
          {
            if (priv->video_tee != NULL)
              {
                pad = gst_element_get_request_pad (priv->video_tee, "src%d");
                if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sink)))
                  {
                    g_warning ("Could not link videp soure input pipeline");
                    break;
                  }
                gst_object_unref (pad);
              }

            retval = TRUE;
          }
        break;
      default:
        g_assert_not_reached ();
    }

  return retval;
}

static void
empathy_streamed_media_window_remove_video_input (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstElement *preview;

  disable_camera (self);

  DEBUG ("remove video input");
  preview = empathy_video_widget_get_element (
    EMPATHY_VIDEO_WIDGET (priv->video_preview));

  gst_element_set_state (priv->video_input, GST_STATE_NULL);
  gst_element_set_state (priv->video_tee, GST_STATE_NULL);
  gst_element_set_state (preview, GST_STATE_NULL);

  gst_bin_remove_many (GST_BIN (priv->pipeline), priv->video_input,
    priv->video_tee, preview, NULL);

  g_object_unref (priv->video_input);
  priv->video_input = NULL;
  g_object_unref (priv->video_tee);
  priv->video_tee = NULL;
  gtk_widget_destroy (priv->video_preview);
  priv->video_preview = NULL;

  gtk_widget_set_sensitive (priv->tool_button_camera_on, FALSE);
  gtk_action_set_sensitive (priv->action_camera_on, FALSE);
  gtk_widget_set_sensitive (priv->tool_button_camera_preview, FALSE);
}

static void
start_call (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  priv->call_started = TRUE;
  empathy_streamed_media_handler_start_call (priv->handler,
      empathy_get_current_action_time ());

  if (empathy_streamed_media_handler_has_initial_video (priv->handler))
    {
      /* Enable 'send video' buttons and display the preview */
      gtk_toggle_tool_button_set_active (
          GTK_TOGGLE_TOOL_BUTTON (priv->tool_button_camera_on), TRUE);
    }
}

static gboolean
empathy_streamed_media_window_bus_message (GstBus *bus, GstMessage *message,
  gpointer user_data)
{
  EmpathyStreamedMediaWindow *self = EMPATHY_STREAMED_MEDIA_WINDOW (user_data);
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  GstState newstate;

  empathy_streamed_media_handler_bus_message (priv->handler, bus, message);

  switch (GST_MESSAGE_TYPE (message))
    {
      case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC (message) == GST_OBJECT (priv->video_input))
          {
            gst_message_parse_state_changed (message, NULL, &newstate, NULL);
            if (newstate == GST_STATE_PAUSED)
                empathy_streamed_media_window_setup_video_input (self);
          }
        if (GST_MESSAGE_SRC (message) == GST_OBJECT (priv->pipeline) &&
            !priv->call_started)
          {
            gst_message_parse_state_changed (message, NULL, &newstate, NULL);
            if (newstate == GST_STATE_PAUSED)
              {
                gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
                priv->pipeline_playing = TRUE;

                if (priv->start_call_when_playing)
                  start_call (self);
              }
          }
        break;
      case GST_MESSAGE_ERROR:
        {
          GError *error = NULL;
          GstElement *gst_error;
          gchar *debug;

          gst_message_parse_error (message, &error, &debug);
          gst_error = GST_ELEMENT (GST_MESSAGE_SRC (message));

          g_message ("Element error: %s -- %s\n", error->message, debug);

          if (g_str_has_prefix (gst_element_get_name (gst_error),
                VIDEO_INPUT_ERROR_PREFIX))
            {
              /* Remove the video input and continue */
              if (priv->video_input != NULL)
                empathy_streamed_media_window_remove_video_input (self);
              gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
            }
          else
            {
              empathy_streamed_media_window_disconnected (self, TRUE);
            }
          g_error_free (error);
          g_free (debug);
        }
      case GST_MESSAGE_UNKNOWN:
      case GST_MESSAGE_EOS:
      case GST_MESSAGE_WARNING:
      case GST_MESSAGE_INFO:
      case GST_MESSAGE_TAG:
      case GST_MESSAGE_BUFFERING:
      case GST_MESSAGE_STATE_DIRTY:
      case GST_MESSAGE_STEP_DONE:
      case GST_MESSAGE_CLOCK_PROVIDE:
      case GST_MESSAGE_CLOCK_LOST:
      case GST_MESSAGE_NEW_CLOCK:
      case GST_MESSAGE_STRUCTURE_CHANGE:
      case GST_MESSAGE_STREAM_STATUS:
      case GST_MESSAGE_APPLICATION:
      case GST_MESSAGE_ELEMENT:
      case GST_MESSAGE_SEGMENT_START:
      case GST_MESSAGE_SEGMENT_DONE:
      case GST_MESSAGE_DURATION:
      case GST_MESSAGE_ANY:
      default:
        break;
    }

  return TRUE;
}

static void
empathy_streamed_media_window_update_avatars_visibility (EmpathyTpStreamedMedia *call,
    EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (empathy_tp_streamed_media_is_receiving_video (call))
    {
      gtk_widget_hide (priv->remote_user_avatar_widget);
      gtk_widget_show (priv->video_output);
    }
  else
    {
      gtk_widget_hide (priv->video_output);
      gtk_widget_show (priv->remote_user_avatar_widget);
    }
}

static void
call_handler_notify_tp_streamed_media_cb (EmpathyStreamedMediaHandler *handler,
    GParamSpec *spec,
    EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);
  EmpathyTpStreamedMedia *call;

  g_object_get (priv->handler, "tp-call", &call, NULL);
  if (call == NULL)
    return;

  tp_g_signal_connect_object (call, "audio-stream-error",
      G_CALLBACK (empathy_streamed_media_window_audio_stream_error), self, 0);
  tp_g_signal_connect_object (call, "video-stream-error",
      G_CALLBACK (empathy_streamed_media_window_video_stream_error), self, 0);

  g_object_unref (call);
}

static void
empathy_streamed_media_window_realized_cb (GtkWidget *widget, EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  EmpathyTpStreamedMedia *call;

  g_signal_connect (priv->handler, "conference-added",
    G_CALLBACK (empathy_streamed_media_window_conference_added_cb), window);
  g_signal_connect (priv->handler, "request-resource",
    G_CALLBACK (empathy_streamed_media_window_request_resource_cb), window);
  g_signal_connect (priv->handler, "closed",
    G_CALLBACK (empathy_streamed_media_window_channel_closed_cb), window);
  g_signal_connect (priv->handler, "src-pad-added",
    G_CALLBACK (empathy_streamed_media_window_src_added_cb), window);
  g_signal_connect (priv->handler, "sink-pad-added",
    G_CALLBACK (empathy_streamed_media_window_sink_added_cb), window);
  g_signal_connect (priv->handler, "stream-closed",
    G_CALLBACK (empathy_streamed_media_window_channel_stream_closed_cb), window);

  g_object_get (priv->handler, "tp-call", &call, NULL);
  if (call != NULL)
    {
      tp_g_signal_connect_object (call, "audio-stream-error",
        G_CALLBACK (empathy_streamed_media_window_audio_stream_error), window,
        0);
      tp_g_signal_connect_object (call, "video-stream-error",
        G_CALLBACK (empathy_streamed_media_window_video_stream_error), window,
        0);

      g_object_unref (call);
    }
  else
    {
      /* tp-call doesn't exist yet, we'll connect signals once it has been
       * set */
      g_signal_connect (priv->handler, "notify::tp-call",
        G_CALLBACK (call_handler_notify_tp_streamed_media_cb), window);
    }

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
}

static gboolean
empathy_streamed_media_window_delete_cb (GtkWidget *widget, GdkEvent*event,
  EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->pipeline != NULL)
    {
      if (priv->bus_message_source_id != 0)
        {
          g_source_remove (priv->bus_message_source_id);
          priv->bus_message_source_id = 0;
        }

      gst_element_set_state (priv->pipeline, GST_STATE_NULL);
    }

  if (priv->call_state == CONNECTING)
    empathy_sound_manager_stop (priv->sound_mgr, EMPATHY_SOUND_PHONE_OUTGOING);

  return FALSE;
}

static void
show_controls (EmpathyStreamedMediaWindow *window, gboolean set_fullscreen)
{
  GtkWidget *menu;
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  menu = gtk_ui_manager_get_widget (priv->ui_manager,
            "/menubar1");

  if (set_fullscreen)
    {
      gtk_widget_hide (priv->sidebar);
      gtk_widget_hide (menu);
      gtk_widget_hide (priv->vbox);
      gtk_widget_hide (priv->statusbar);
      gtk_widget_hide (priv->toolbar);
    }
  else
    {
      if (priv->sidebar_was_visible_before_fs)
        gtk_widget_show (priv->sidebar);

      gtk_widget_show (menu);
      gtk_widget_show (priv->vbox);
      gtk_widget_show (priv->statusbar);
      gtk_widget_show (priv->toolbar);

      gtk_window_resize (GTK_WINDOW (window), priv->original_width_before_fs,
          priv->original_height_before_fs);
    }
}

static void
show_borders (EmpathyStreamedMediaWindow *window, gboolean set_fullscreen)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  gtk_container_set_border_width (GTK_CONTAINER (priv->content_hbox),
      set_fullscreen ? 0 : CONTENT_HBOX_BORDER_WIDTH);
  gtk_box_set_spacing (GTK_BOX (priv->content_hbox),
      set_fullscreen ? 0 : CONTENT_HBOX_SPACING);

  if (priv->video_output != NULL)
    {
      gtk_box_set_child_packing (GTK_BOX (priv->content_hbox),
          priv->video_output, TRUE, TRUE,
          set_fullscreen ? 0 : CONTENT_HBOX_CHILDREN_PACKING_PADDING,
          GTK_PACK_START);
    }

  gtk_box_set_child_packing (GTK_BOX (priv->content_hbox),
      priv->vbox, TRUE, TRUE,
      set_fullscreen ? 0 : CONTENT_HBOX_CHILDREN_PACKING_PADDING,
      GTK_PACK_START);
}

static gboolean
empathy_streamed_media_window_state_event_cb (GtkWidget *widget,
  GdkEventWindowState *event, EmpathyStreamedMediaWindow *window)
{
  if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
      EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
      gboolean set_fullscreen = event->new_window_state &
        GDK_WINDOW_STATE_FULLSCREEN;

      if (set_fullscreen)
        {
          gboolean sidebar_was_visible;
          GtkAllocation allocation;
          gint original_width, original_height;

          gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
          original_width = allocation.width;
          original_height = allocation.height;

          g_object_get (priv->sidebar, "visible", &sidebar_was_visible, NULL);

          priv->sidebar_was_visible_before_fs = sidebar_was_visible;
          priv->original_width_before_fs = original_width;
          priv->original_height_before_fs = original_height;

          if (priv->video_output_motion_handler_id == 0 &&
                priv->video_output != NULL)
            {
              priv->video_output_motion_handler_id = g_signal_connect (
                  G_OBJECT (priv->video_output), "motion-notify-event",
                  G_CALLBACK (empathy_streamed_media_window_video_output_motion_notify),
                  window);
            }
        }
      else
        {
          disconnect_video_output_motion_handler (window);
        }

      empathy_streamed_media_window_fullscreen_set_fullscreen (priv->fullscreen,
          set_fullscreen);
      show_controls (window, set_fullscreen);
      show_borders (window, set_fullscreen);
      gtk_action_set_stock_id (priv->menu_fullscreen,
          (set_fullscreen ? "gtk-leave-fullscreen" : "gtk-fullscreen"));
      priv->is_fullscreen = set_fullscreen;
  }

  return FALSE;
}

static void
empathy_streamed_media_window_sidebar_toggled_cb (GtkToggleButton *toggle,
  EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  GtkWidget *arrow;
  int w, h, handle_size;
  GtkAllocation allocation, sidebar_allocation;

  gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
  w = allocation.width;
  h = allocation.height;

  gtk_widget_style_get (priv->pane, "handle_size", &handle_size, NULL);

  gtk_widget_get_allocation (priv->sidebar, &sidebar_allocation);
  if (gtk_toggle_button_get_active (toggle))
    {
      arrow = gtk_arrow_new (GTK_ARROW_LEFT, GTK_SHADOW_NONE);
      gtk_widget_show (priv->sidebar);
      w += sidebar_allocation.width + handle_size;
    }
  else
    {
      arrow = gtk_arrow_new (GTK_ARROW_RIGHT, GTK_SHADOW_NONE);
      w -= sidebar_allocation.width + handle_size;
      gtk_widget_hide (priv->sidebar);
    }

  gtk_button_set_image (GTK_BUTTON (priv->sidebar_button), arrow);

  if (w > 0 && h > 0)
    gtk_window_resize (GTK_WINDOW (window), w, h);
}

static void
empathy_streamed_media_window_set_send_video (EmpathyStreamedMediaWindow *window,
  CameraState state)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  EmpathyTpStreamedMedia *call;

  priv->sending_video = (state == CAMERA_STATE_ON);

  if (state == CAMERA_STATE_PREVIEW ||
      state == CAMERA_STATE_ON)
    {
      /* When we start sending video, we want to show the video preview by
         default. */
      display_video_preview (window, TRUE);
    }
  else
    {
      display_video_preview (window, FALSE);
    }

  if (priv->call_state != CONNECTED)
    return;

  g_object_get (priv->handler, "tp-call", &call, NULL);
  DEBUG ("%s sending video", priv->sending_video ? "start": "stop");
  empathy_tp_streamed_media_request_video_stream_direction (call, priv->sending_video);
  g_object_unref (call);
}

static void
empathy_streamed_media_window_mic_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);
  gboolean active;

  active = (gtk_toggle_tool_button_get_active (toggle));

  if (active)
    {
      empathy_audio_src_set_volume (EMPATHY_GST_AUDIO_SRC (priv->audio_input),
        priv->volume);
      gtk_adjustment_set_value (priv->audio_input_adj, priv->volume * 100);
    }
  else
    {
      /* TODO, Instead of setting the input volume to 0 we should probably
       * stop sending but this would cause the audio call to drop if both
       * sides mute at the same time on certain CMs AFAIK. Need to revisit this
       * in the future. GNOME #574574
       */
      empathy_audio_src_set_volume (EMPATHY_GST_AUDIO_SRC (priv->audio_input),
        0);
      gtk_adjustment_set_value (priv->audio_input_adj, 0);
    }
}

static void
empathy_streamed_media_window_sidebar_hidden_cb (EvSidebar *sidebar,
  EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->sidebar_button),
    FALSE);
}

static void
empathy_streamed_media_window_sidebar_shown_cb (EvSidebar *sidebar,
  EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->sidebar_button),
    TRUE);
}

static void
empathy_streamed_media_window_hangup_cb (gpointer object,
                               EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  empathy_streamed_media_handler_stop_call (priv->handler);

  if (empathy_streamed_media_window_disconnected (window, FALSE))
    gtk_widget_destroy (GTK_WIDGET (window));
}

static void
empathy_streamed_media_window_restart_call (EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  /* Remove error info bars */
  gtk_container_forall (GTK_CONTAINER (priv->errors_vbox),
      (GtkCallback) gtk_widget_destroy, NULL);

  create_video_output_widget (window);

  g_signal_connect (G_OBJECT (priv->audio_input_adj), "value-changed",
      G_CALLBACK (empathy_streamed_media_window_mic_volume_changed_cb), window);

  /* While the call was disconnected, the input volume might have changed.
   * However, since the audio_input source was destroyed, its volume has not
   * been updated during that time. That's why we manually update it here */
  empathy_streamed_media_window_mic_volume_changed_cb (priv->audio_input_adj, window);

  priv->outgoing = TRUE;
  empathy_streamed_media_window_set_state_connecting (window);

  if (priv->pipeline_playing)
    start_call (window);
  else
    /* call will be started when the pipeline is ready */
    priv->start_call_when_playing = TRUE;


  empathy_streamed_media_window_setup_avatars (window, priv->handler);

  gtk_action_set_sensitive (priv->redial, FALSE);
  gtk_widget_set_sensitive (priv->redial_button, FALSE);
}

static void
empathy_streamed_media_window_redial_cb (gpointer object,
    EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->call_state == CONNECTED)
    priv->call_state = REDIALING;

  empathy_streamed_media_handler_stop_call (priv->handler);

  if (priv->call_state != CONNECTED)
    empathy_streamed_media_window_restart_call (window);
}

static void
empathy_streamed_media_window_fullscreen_cb (gpointer object,
                                   EmpathyStreamedMediaWindow *window)
{
  empathy_streamed_media_window_fullscreen_toggle (window);
}

static void
empathy_streamed_media_window_fullscreen_toggle (EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen)
    gtk_window_unfullscreen (GTK_WINDOW (window));
  else
    gtk_window_fullscreen (GTK_WINDOW (window));
}

static gboolean
empathy_streamed_media_window_video_button_press_cb (GtkWidget *video_output,
  GdkEventButton *event, EmpathyStreamedMediaWindow *window)
{
  if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
    {
      empathy_streamed_media_window_video_menu_popup (window, event->button);
      return TRUE;
    }

  return FALSE;
}

static gboolean
empathy_streamed_media_window_key_press_cb (GtkWidget *video_output,
  GdkEventKey *event, EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen && event->keyval == GDK_KEY_Escape)
    {
      /* Since we are in fullscreen mode, toggling will bring us back to
         normal mode. */
      empathy_streamed_media_window_fullscreen_toggle (window);
      return TRUE;
    }

  return FALSE;
}

static gboolean
empathy_streamed_media_window_video_output_motion_notify (GtkWidget *widget,
    GdkEventMotion *event, EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen)
    {
      empathy_streamed_media_window_fullscreen_show_popup (priv->fullscreen);
      return TRUE;
    }
  return FALSE;
}

static void
empathy_streamed_media_window_video_menu_popup (EmpathyStreamedMediaWindow *window,
  guint button)
{
  GtkWidget *menu;
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  menu = gtk_ui_manager_get_widget (priv->ui_manager,
            "/video-popup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      button, gtk_get_current_event_time ());
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static void
empathy_streamed_media_window_status_message (EmpathyStreamedMediaWindow *window,
  gchar *message)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->context_id == 0)
    {
      priv->context_id = gtk_statusbar_get_context_id (
        GTK_STATUSBAR (priv->statusbar), "voip call status messages");
    }
  else
    {
      gtk_statusbar_pop (GTK_STATUSBAR (priv->statusbar), priv->context_id);
    }

  gtk_statusbar_push (GTK_STATUSBAR (priv->statusbar), priv->context_id,
    message);
}

static void
empathy_streamed_media_window_volume_changed_cb (GtkScaleButton *button,
  gdouble value, EmpathyStreamedMediaWindow *window)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (window);

  if (priv->audio_output == NULL)
    return;

  empathy_audio_sink_set_volume (EMPATHY_GST_AUDIO_SINK (priv->audio_output),
    value);
}

/* block all the signals related to camera control widgets. This is useful
 * when we are manually updating the UI and so don't want to fire the
 * callbacks */
static void
block_camera_control_signals (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  g_signal_handlers_block_by_func (priv->tool_button_camera_off,
      tool_button_camera_off_toggled_cb, self);
  g_signal_handlers_block_by_func (priv->tool_button_camera_preview,
      tool_button_camera_preview_toggled_cb, self);
  g_signal_handlers_block_by_func (priv->tool_button_camera_on,
      tool_button_camera_on_toggled_cb, self);
  g_signal_handlers_block_by_func (priv->action_camera_on,
      action_camera_change_cb, self);
}

static void
unblock_camera_control_signals (EmpathyStreamedMediaWindow *self)
{
  EmpathyStreamedMediaWindowPriv *priv = GET_PRIV (self);

  g_signal_handlers_unblock_by_func (priv->tool_button_camera_off,
      tool_button_camera_off_toggled_cb, self);
  g_signal_handlers_unblock_by_func (priv->tool_button_camera_preview,
      tool_button_camera_preview_toggled_cb, self);
  g_signal_handlers_unblock_by_func (priv->tool_button_camera_on,
      tool_button_camera_on_toggled_cb, self);
  g_signal_handlers_unblock_by_func (priv->action_camera_on,
      action_camera_change_cb, self);
}

/*
 * empathy-call-window.c - Source for EmpathyCallWindow
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

#include <math.h>

#include <gdk/gdkkeysyms.h>
#include <gst/gst.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <clutter/clutter.h>
#include <clutter-gtk/clutter-gtk.h>
#include <clutter-gst/clutter-gst.h>

#include <telepathy-glib/util.h>
#include <telepathy-farstream/telepathy-farstream.h>
#include <telepathy-glib/util.h>

#include <gst/farsight/fs-element-added-notifier.h>
#include <gst/farsight/fs-utils.h>

#include <libempathy/empathy-camera-monitor.h>
#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-avatar-image.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-sound-manager.h>
#include <libempathy-gtk/empathy-geometry.h>
#include <libempathy-gtk/empathy-images.h>

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

#ifdef HAVE_VIDEO_EFFECT
#include <cheese/cheese-effect.h>
#endif /* HAVE_VIDEO_EFFECT */

#include "empathy-call-window.h"
#include "empathy-call-window-fullscreen.h"
#include "empathy-call-factory.h"
#include "empathy-video-widget.h"
#include "empathy-about-dialog.h"
#include "empathy-audio-src.h"
#include "empathy-audio-sink.h"
#include "empathy-video-src.h"
#include "empathy-mic-menu.h"
#include "empathy-preferences.h"
#include "empathy-rounded-actor.h"
#include "empathy-rounded-rectangle.h"
#include "empathy-rounded-texture.h"
#include "empathy-camera-menu.h"

#define CONTENT_HBOX_BORDER_WIDTH 6
#define CONTENT_HBOX_SPACING 3
#define CONTENT_HBOX_CHILDREN_PACKING_PADDING 3

#define SELF_VIDEO_SECTION_WIDTH 120
#define SELF_VIDEO_SECTION_HEIGHT 90
#define SELF_VIDEO_SECTION_MARGIN 10

#define FLOATING_TOOLBAR_OPACITY 192
#define FLOATING_TOOLBAR_WIDTH 280
#define FLOATING_TOOLBAR_HEIGHT 36
#define FLOATING_TOOLBAR_SPACING 20

/* The avatar's default width and height are set to the same value because we
   want a square icon. */
#define REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT
#define REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT \
  EMPATHY_VIDEO_WIDGET_DEFAULT_HEIGHT

#define SMALL_TOOLBAR_SIZE 36

/* If an video input error occurs, the error message will start with "v4l" */
#define VIDEO_INPUT_ERROR_PREFIX "v4l"

/* The time interval in milliseconds between 2 outgoing rings */
#define MS_BETWEEN_RING 500

/* display 3x3 effects per page in the video effect preview pages */
#define EFFECT_ROWS  3
#define EFFECT_COLS  3
#define EFFECTS_PER_PAGE (EFFECT_ROWS * EFFECT_COLS)

G_DEFINE_TYPE(EmpathyCallWindow, empathy_call_window, GTK_TYPE_WINDOW)

enum {
  PROP_CALL_HANDLER = 1,
};

typedef enum {
  CONNECTING,
  CONNECTED,
  HELD,
  DISCONNECTED,
  REDIALING
} CallState;

typedef enum {
  CAMERA_STATE_OFF = 0,
  CAMERA_STATE_ON,
} CameraState;

typedef enum {
  PREVIEW_POS_NONE,
  PREVIEW_POS_TOP_LEFT,
  PREVIEW_POS_TOP_RIGHT,
  PREVIEW_POS_BOTTOM_LEFT,
  PREVIEW_POS_BOTTOM_RIGHT,
} PreviewPosition;

struct _EmpathyCallWindowPriv
{
  gboolean dispose_has_run;
  EmpathyCallHandler *handler;

  EmpathyContact *contact;

  EmpathyCameraMonitor *camera_monitor;

  guint call_state;
  gboolean outgoing;

  GtkUIManager *ui_manager;
  GtkWidget *errors_vbox;
  /* widget displays the video received from the remote user. This widget is
   * alive only during call. */
  ClutterActor *video_output;
  ClutterActor *video_preview;
  ClutterActor *preview_shown_button;
  ClutterActor *preview_hidden_button;
  ClutterActor *preview_rectangle1;
  ClutterActor *preview_rectangle2;
  ClutterActor *preview_rectangle3;
  ClutterActor *preview_rectangle4;
  ClutterActor *preview_rectangle_box1;
  ClutterActor *preview_rectangle_box2;
  ClutterActor *preview_rectangle_box3;
  ClutterActor *preview_rectangle_box4;
  GtkWidget *video_container;
  GtkWidget *remote_user_avatar_widget;
  GtkWidget *remote_user_avatar_toolbar;
  GtkWidget *remote_user_name_toolbar;
  GtkWidget *status_label;
  GtkWidget *hangup_button;
  GtkWidget *audio_call_button;
  GtkWidget *video_call_button;
  GtkWidget *mic_button;
  GtkWidget *camera_button;
  GtkWidget *video_effects_button;
  GtkWidget *dialpad_button;
  GtkWidget *toolbar;
  GtkWidget *bottom_toolbar;
  ClutterActor *floating_toolbar;
  GtkWidget *pane;
  GtkAction *menu_fullscreen;
  GtkAction *menu_swap_camera;

  ClutterState *transitions;

  /* The box that contains the video_box and the effects_box */
  ClutterActor *root_box;
  /* The box that contains the video effect previews */
  ClutterActor *effects_box;
  /* The box that contains self and remote avatar and video
     input/output. When we redial, we destroy and re-create the box */
  ClutterActor *video_box;
  ClutterLayoutManager *video_layout;

  /* Coordinates of the preview drag event's start. */
  PreviewPosition preview_pos;

  #ifdef HAVE_VIDEO_EFFECT
  /* array of CheeseEffect objects, sorted by name. First element=identity */
  CheeseEffect **cheese_effects;
  gint         cheese_effects_count;
  ClutterActor **effect_pages;
  gint         effect_pages_count, curr_effect_page;
  #endif /* HAVE_VIDEO_EFFECT */

  /* We keep a reference on the hbox which contains the main content so we can
     easilly repack everything when toggling fullscreen */
  GtkWidget *content_hbox;

  gulong video_output_motion_handler_id;
  guint bus_message_source_id;

  gdouble volume;

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
  GstElement *video_preview_sink;
  GstElement *video_output_sink;
  GstElement *audio_input;
  GstElement *audio_output;
  GstElement *pipeline;
  GstElement *video_tee;

  GstElement *funnel;

  GList *notifiers;

  GTimer *timer;
  guint timer_id;

  GMutex *lock;
  gboolean call_started;
  gboolean sending_video;
  CameraState camera_state;
  gboolean video_effects_on;

  EmpathyCallWindowFullscreen *fullscreen;
  gboolean is_fullscreen;

  gboolean got_video;
  guint got_video_src;

  guint inactivity_src;

  /* Those fields represent the state of the window before it actually was in
     fullscreen mode. */
  gboolean dialpad_was_visible_before_fs;
  gint original_width_before_fs;
  gint original_height_before_fs;

  gint x, y, w, h, dialpad_width;
  gboolean maximized;

  /* TRUE if the call should be started when the pipeline is playing */
  gboolean start_call_when_playing;
  /* TRUE if we requested to set the pipeline in the playing state */
  gboolean pipeline_playing;

  EmpathySoundManager *sound_mgr;

  GSettings *settings;
  EmpathyMicMenu *mic_menu;
  EmpathyCameraMenu *camera_menu;
};

#define GET_PRIV(o) (EMPATHY_CALL_WINDOW (o)->priv)

static void empathy_call_window_realized_cb (GtkWidget *widget,
  EmpathyCallWindow *window);

static gboolean empathy_call_window_delete_cb (GtkWidget *widget,
  GdkEvent *event, EmpathyCallWindow *window);

static gboolean empathy_call_window_state_event_cb (GtkWidget *widget,
  GdkEventWindowState *event, EmpathyCallWindow *window);

static void empathy_call_window_set_send_video (EmpathyCallWindow *window,
  CameraState state);

static void empathy_call_window_mic_toggled_cb (
  GtkToggleToolButton *toggle, EmpathyCallWindow *window);

static void empathy_call_window_hangup_cb (gpointer object,
  EmpathyCallWindow *window);

static void empathy_call_window_fullscreen_cb (gpointer object,
  EmpathyCallWindow *window);

static void empathy_call_window_fullscreen_toggle (EmpathyCallWindow *window);

static gboolean empathy_call_window_video_button_press_cb (
  GtkWidget *video_output, GdkEventButton *event, EmpathyCallWindow *window);

static gboolean empathy_call_window_key_press_cb (GtkWidget *video_output,
  GdkEventKey *event, EmpathyCallWindow *window);

static gboolean empathy_call_window_video_output_motion_notify (
  GtkWidget *widget, GdkEventMotion *event, EmpathyCallWindow *window);

static void empathy_call_window_video_menu_popup (EmpathyCallWindow *window,
  guint button);

static void empathy_call_window_dialpad_cb (GtkToggleToolButton *button,
  EmpathyCallWindow *window);

static void empathy_call_window_restart_call (EmpathyCallWindow *window);

static void empathy_call_window_status_message (EmpathyCallWindow *window,
  gchar *message);

static gboolean empathy_call_window_bus_message (GstBus *bus,
  GstMessage *message, gpointer user_data);

static void
empathy_call_window_volume_changed_cb (GtkScaleButton *button,
  gdouble value, EmpathyCallWindow *window);

static void
empathy_call_window_show_hangup_button (EmpathyCallWindow *self,
    gboolean show)
{
  gtk_widget_set_visible (self->priv->hangup_button, show);
  gtk_widget_set_visible (self->priv->audio_call_button, !show);
  gtk_widget_set_visible (self->priv->video_call_button, !show);
}

static void
empathy_call_window_audio_call_cb (GtkToggleToolButton *button,
    EmpathyCallWindow *self)
{
  g_object_set (self->priv->handler, "initial-video", FALSE, NULL);
  empathy_call_window_restart_call (self);
}

static void
empathy_call_window_video_call_cb (GtkToggleToolButton *button,
    EmpathyCallWindow *self)
{
  empathy_call_window_set_send_video (self, CAMERA_STATE_ON);
  g_object_set (self->priv->handler, "initial-video", TRUE, NULL);
  empathy_call_window_restart_call (self);
}

static void
dtmf_button_pressed_cb (GtkButton *button, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
  TpyCallChannel *call;
  GQuark button_quark;
  TpDTMFEvent event;

  g_object_get (priv->handler, "call-channel", &call, NULL);

  button_quark = g_quark_from_static_string (EMPATHY_DTMF_BUTTON_ID);
  event = GPOINTER_TO_UINT (g_object_get_qdata (G_OBJECT (button),
    button_quark));

  tpy_call_channel_dtmf_start_tone (call, event);

  g_object_unref (call);
}

static void
dtmf_button_released_cb (GtkButton *button, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
  TpyCallChannel *call;

  g_object_get (priv->handler, "call-channel", &call, NULL);

  tpy_call_channel_dtmf_stop_tone (call);

  g_object_unref (call);
}

static void
empathy_call_window_mic_volume_changed (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  gdouble volume;

  volume = g_settings_get_double (priv->settings,
      EMPATHY_PREFS_CALL_SOUND_VOLUME) / 100.0;

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
empathy_call_window_prefs_volume_changed_cb (GSettings *settings,
    gchar *key,
    EmpathyCallWindow *self)
{
  empathy_call_window_mic_volume_changed (self);
}

static void
empathy_call_window_raise_actors (EmpathyCallWindow *self)
{
  clutter_actor_raise_top (self->priv->floating_toolbar);

  clutter_actor_raise_top (self->priv->preview_rectangle_box1);
  clutter_actor_raise_top (self->priv->preview_rectangle_box2);
  clutter_actor_raise_top (self->priv->preview_rectangle_box3);
  clutter_actor_raise_top (self->priv->preview_rectangle_box4);
}

static void
empathy_call_window_show_video_output (EmpathyCallWindow *self,
    gboolean show)
{
  if (self->priv->video_output != NULL)
    g_object_set (self->priv->video_output, "visible", show, NULL);

  gtk_widget_set_visible (self->priv->remote_user_avatar_widget, !show);

  empathy_call_window_raise_actors (self);
}

static void
create_video_output_widget (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  g_assert (priv->video_output == NULL);
  g_assert (priv->pipeline != NULL);

  priv->video_output = clutter_texture_new ();

  clutter_texture_set_keep_aspect_ratio (CLUTTER_TEXTURE (priv->video_output),
      TRUE);

  priv->video_output_sink = clutter_gst_video_sink_new (
      CLUTTER_TEXTURE (priv->video_output));

  clutter_container_add_actor (CLUTTER_CONTAINER (priv->video_box),
      priv->video_output);

  g_signal_connect (G_OBJECT (priv->video_container), "button-press-event",
      G_CALLBACK (empathy_call_window_video_button_press_cb), self);
}

static void
create_video_input (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  g_assert (priv->video_input == NULL);
  priv->video_input = empathy_video_src_new ();
  gst_object_ref (priv->video_input);
  gst_object_sink (priv->video_input);
}

static void
create_audio_input (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  g_assert (priv->audio_input == NULL);
  priv->audio_input = empathy_audio_src_new ();
  gst_object_ref (priv->audio_input);
  gst_object_sink (priv->audio_input);
}

static void
add_video_preview_to_pipeline (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstElement *preview;

  g_assert (priv->video_preview != NULL);
  g_assert (priv->pipeline != NULL);
  g_assert (priv->video_input != NULL);
  g_assert (priv->video_tee != NULL);

  preview = priv->video_preview_sink;

  if (!gst_bin_add (GST_BIN (priv->pipeline), priv->video_input))
    {
      g_warning ("Could not add video input to pipeline");
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
empathy_call_window_disable_camera_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  clutter_actor_destroy (self->priv->preview_hidden_button);

  gtk_toggle_tool_button_set_active (
      GTK_TOGGLE_TOOL_BUTTON (self->priv->camera_button), FALSE);
}

static void
empathy_call_window_minimise_camera_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  clutter_actor_hide (self->priv->video_preview);
  clutter_actor_show (self->priv->preview_hidden_button);
}

static void
empathy_call_window_maximise_camera_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  clutter_actor_show (self->priv->video_preview);
  clutter_actor_hide (self->priv->preview_hidden_button);
}

static void
empathy_call_window_swap_camera_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  const GList *cameras, *l;
  gchar *current_cam;

  DEBUG ("Swapping the camera");

  cameras = empathy_camera_monitor_get_cameras (self->priv->camera_monitor);
  current_cam = empathy_video_src_dup_device (
      EMPATHY_GST_VIDEO_SRC (self->priv->video_input));

  for (l = cameras; l != NULL; l = l->next)
    {
      EmpathyCamera *camera = l->data;

      if (!tp_strdiff (camera->device, current_cam))
        {
          EmpathyCamera *next;

          if (l->next != NULL)
            next = l->next->data;
          else
            next = cameras->data;

          /* EmpathyCameraMenu will update itself and do the actual change
           * for us */
          g_settings_set_string (self->priv->settings,
              EMPATHY_PREFS_CALL_CAMERA_DEVICE,
              next->device);

          break;
        }
    }

  g_free (current_cam);
}

static void
empathy_call_window_camera_added_cb (EmpathyCameraMonitor *monitor,
    EmpathyCamera *camera,
    EmpathyCallWindow *self)
{
  const GList *cameras = empathy_camera_monitor_get_cameras (monitor);

  gtk_action_set_visible (self->priv->menu_swap_camera,
      g_list_length ((GList *) cameras) >= 2);
}

static void
empathy_call_window_camera_removed_cb (EmpathyCameraMonitor *monitor,
    EmpathyCamera *camera,
    EmpathyCallWindow *self)
{
  const GList *cameras = empathy_camera_monitor_get_cameras (monitor);

  gtk_action_set_visible (self->priv->menu_swap_camera,
      g_list_length ((GList *) cameras) >= 2);
}

static void
empathy_call_window_preview_button_clicked_cb (GtkButton *button,
    EmpathyCallWindow *self)
{
  GtkWidget *menu;

  menu = gtk_ui_manager_get_widget (self->priv->ui_manager,
      "/preview-menu");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      0, gtk_get_current_event_time ());
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static void
empathy_call_window_preview_hidden_button_clicked_cb (GtkButton *button,
    EmpathyCallWindow *self)
{
  GtkWidget *menu;

  menu = gtk_ui_manager_get_widget (self->priv->ui_manager,
      "/preview-hidden-menu");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      0, gtk_get_current_event_time ());
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static ClutterActor *
empathy_call_window_create_preview_rectangle (EmpathyCallWindow *self,
    ClutterActor **box,
    ClutterBinAlignment x,
    ClutterBinAlignment y)
{
  ClutterLayoutManager *layout1, *layout2;
  ClutterActor *rectangle;
  ClutterActor *box1, *box2;

  layout1 = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_START);

  box1 = clutter_box_new (layout1);

  *box = box1;

  rectangle = empathy_rounded_rectangle_new (
      SELF_VIDEO_SECTION_WIDTH + 5,
      SELF_VIDEO_SECTION_HEIGHT + 5);

  clutter_actor_set_size (box1,
      SELF_VIDEO_SECTION_WIDTH + 2 * SELF_VIDEO_SECTION_MARGIN,
      SELF_VIDEO_SECTION_HEIGHT + 2 * SELF_VIDEO_SECTION_MARGIN +
      FLOATING_TOOLBAR_HEIGHT + FLOATING_TOOLBAR_SPACING);

  layout2 = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_CENTER);

  /* We have a box with the margins and the video in the middle inside
   * a bigger box with an extra bottom margin so we're not on top of
   * the floating toolbar. */
  box2 = clutter_box_new (layout2);

  clutter_actor_set_size (box2,
      SELF_VIDEO_SECTION_WIDTH + 2 * SELF_VIDEO_SECTION_MARGIN,
      SELF_VIDEO_SECTION_HEIGHT + 2 * SELF_VIDEO_SECTION_MARGIN);

  clutter_container_add_actor (CLUTTER_CONTAINER (box1), box2);
  clutter_container_add_actor (CLUTTER_CONTAINER (box2), rectangle);

  clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (self->priv->video_layout),
      box1, x, y);

  clutter_actor_hide (rectangle);

  return rectangle;
}

static void
empathy_call_window_create_preview_rectangles (EmpathyCallWindow *self)
{
  self->priv->preview_rectangle1 =
      empathy_call_window_create_preview_rectangle (self,
          &self->priv->preview_rectangle_box1,
          CLUTTER_BIN_ALIGNMENT_START, CLUTTER_BIN_ALIGNMENT_START);
  self->priv->preview_rectangle2 =
      empathy_call_window_create_preview_rectangle (self,
          &self->priv->preview_rectangle_box2,
          CLUTTER_BIN_ALIGNMENT_START, CLUTTER_BIN_ALIGNMENT_END);
  self->priv->preview_rectangle3 =
      empathy_call_window_create_preview_rectangle (self,
          &self->priv->preview_rectangle_box3,
          CLUTTER_BIN_ALIGNMENT_END, CLUTTER_BIN_ALIGNMENT_START);
  self->priv->preview_rectangle4 =
      empathy_call_window_create_preview_rectangle (self,
          &self->priv->preview_rectangle_box4,
          CLUTTER_BIN_ALIGNMENT_END, CLUTTER_BIN_ALIGNMENT_END);
}

static void
empathy_call_window_show_preview_rectangles (EmpathyCallWindow *self,
    gboolean show)
{
  g_object_set (self->priv->preview_rectangle1, "visible", show, NULL);
  g_object_set (self->priv->preview_rectangle2, "visible", show, NULL);
  g_object_set (self->priv->preview_rectangle3, "visible", show, NULL);
  g_object_set (self->priv->preview_rectangle4, "visible", show, NULL);
}

static PreviewPosition
empathy_call_window_get_preview_position (EmpathyCallWindow *self,
    gfloat event_x,
    gfloat event_y)
{
  ClutterGeometry box;
  PreviewPosition pos = PREVIEW_POS_NONE;

  g_return_val_if_fail (clutter_actor_has_allocation (self->priv->video_box),
      pos);

  clutter_actor_get_geometry (self->priv->video_box, &box);

  if (0 + SELF_VIDEO_SECTION_MARGIN <= event_x &&
      event_x <= (0 + SELF_VIDEO_SECTION_MARGIN + (gint) SELF_VIDEO_SECTION_WIDTH) &&
      0 + SELF_VIDEO_SECTION_MARGIN <= event_y &&
      event_y <= (0 + SELF_VIDEO_SECTION_MARGIN + (gint) SELF_VIDEO_SECTION_HEIGHT))
    {
      pos = PREVIEW_POS_TOP_LEFT;
    }
  else if (box.width - SELF_VIDEO_SECTION_MARGIN >= event_x &&
      event_x >= (box.width - SELF_VIDEO_SECTION_MARGIN - (gint) SELF_VIDEO_SECTION_WIDTH) &&
      0 + SELF_VIDEO_SECTION_MARGIN <= event_y &&
      event_y <= (0 + SELF_VIDEO_SECTION_MARGIN + (gint) SELF_VIDEO_SECTION_HEIGHT))
    {
      pos = PREVIEW_POS_TOP_RIGHT;
    }
  else if (0 + SELF_VIDEO_SECTION_MARGIN <= event_x &&
      event_x <= (0 + SELF_VIDEO_SECTION_MARGIN + (gint) SELF_VIDEO_SECTION_WIDTH) &&
      box.height - SELF_VIDEO_SECTION_MARGIN - FLOATING_TOOLBAR_HEIGHT - FLOATING_TOOLBAR_SPACING >= event_y &&
      event_y >= (box.height - SELF_VIDEO_SECTION_MARGIN - FLOATING_TOOLBAR_HEIGHT - FLOATING_TOOLBAR_SPACING - (gint) SELF_VIDEO_SECTION_HEIGHT))
    {
      pos = PREVIEW_POS_BOTTOM_LEFT;
    }
  else if (box.width - SELF_VIDEO_SECTION_MARGIN >= event_x &&
      event_x >= (box.width - SELF_VIDEO_SECTION_MARGIN - (gint) SELF_VIDEO_SECTION_WIDTH) &&
      box.height - SELF_VIDEO_SECTION_MARGIN - SELF_VIDEO_SECTION_MARGIN - FLOATING_TOOLBAR_HEIGHT - FLOATING_TOOLBAR_SPACING >= event_y &&
      event_y >= (box.height - SELF_VIDEO_SECTION_MARGIN - FLOATING_TOOLBAR_HEIGHT - FLOATING_TOOLBAR_SPACING - (gint) SELF_VIDEO_SECTION_HEIGHT))
    {
      pos = PREVIEW_POS_BOTTOM_RIGHT;
    }

  return pos;
}

static ClutterActor *
empathy_call_window_get_preview_rectangle (EmpathyCallWindow *self,
    PreviewPosition pos)
{
  ClutterActor *rectangle;

  switch (pos)
    {
      case PREVIEW_POS_TOP_LEFT:
        rectangle = self->priv->preview_rectangle1;
        break;
      case PREVIEW_POS_TOP_RIGHT:
        rectangle = self->priv->preview_rectangle3;
        break;
      case PREVIEW_POS_BOTTOM_LEFT:
        rectangle = self->priv->preview_rectangle2;
        break;
      case PREVIEW_POS_BOTTOM_RIGHT:
        rectangle = self->priv->preview_rectangle4;
        break;
      default:
        rectangle = NULL;
    }

  return rectangle;
}

static void
empathy_call_window_move_video_preview (EmpathyCallWindow *self,
    PreviewPosition pos)
{
  ClutterBinLayout *layout = CLUTTER_BIN_LAYOUT (self->priv->video_layout);

  DEBUG ("moving the video preview to %d", pos);

  self->priv->preview_pos = pos;

  switch (pos)
    {
      case PREVIEW_POS_TOP_LEFT:
        clutter_bin_layout_set_alignment (layout,
            self->priv->video_preview,
            CLUTTER_BIN_ALIGNMENT_START,
            CLUTTER_BIN_ALIGNMENT_START);
        break;
      case PREVIEW_POS_TOP_RIGHT:
        clutter_bin_layout_set_alignment (layout,
            self->priv->video_preview,
            CLUTTER_BIN_ALIGNMENT_END,
            CLUTTER_BIN_ALIGNMENT_START);
        break;
      case PREVIEW_POS_BOTTOM_LEFT:
        clutter_bin_layout_set_alignment (layout,
            self->priv->video_preview,
            CLUTTER_BIN_ALIGNMENT_START,
            CLUTTER_BIN_ALIGNMENT_END);
        break;
      case PREVIEW_POS_BOTTOM_RIGHT:
        clutter_bin_layout_set_alignment (layout,
            self->priv->video_preview,
            CLUTTER_BIN_ALIGNMENT_END,
            CLUTTER_BIN_ALIGNMENT_END);
        break;
      default:
        g_warn_if_reached ();
    }
}

static void
empathy_call_window_highlight_preview_rectangle (EmpathyCallWindow *self,
    PreviewPosition pos)
{
  ClutterActor *rectangle;

  rectangle = empathy_call_window_get_preview_rectangle (self, pos);

  empathy_rounded_rectangle_set_border_width (
      EMPATHY_ROUNDED_RECTANGLE (rectangle), 5);
  empathy_rounded_rectangle_set_border_color (
      EMPATHY_ROUNDED_RECTANGLE (rectangle), CLUTTER_COLOR_Red);
}

static void
empathy_call_window_darken_preview_rectangle (EmpathyCallWindow *self,
    ClutterActor *rectangle)
{
  empathy_rounded_rectangle_set_border_width (
      EMPATHY_ROUNDED_RECTANGLE (rectangle), 1);
  empathy_rounded_rectangle_set_border_color (
      EMPATHY_ROUNDED_RECTANGLE (rectangle), CLUTTER_COLOR_Black);
}

static void
empathy_call_window_darken_preview_rectangles (EmpathyCallWindow *self)
{
  ClutterActor *rectangle;

  rectangle = empathy_call_window_get_preview_rectangle (self,
      self->priv->preview_pos);

  /* We don't want to darken the rectangle where the preview
   * currently is. */

  if (self->priv->preview_rectangle1 != rectangle)
    empathy_call_window_darken_preview_rectangle (self,
        self->priv->preview_rectangle1);

  if (self->priv->preview_rectangle2 != rectangle)
    empathy_call_window_darken_preview_rectangle (self,
        self->priv->preview_rectangle2);

  if (self->priv->preview_rectangle3 != rectangle)
    empathy_call_window_darken_preview_rectangle (self,
        self->priv->preview_rectangle3);

  if (self->priv->preview_rectangle4 != rectangle)
    empathy_call_window_darken_preview_rectangle (self,
        self->priv->preview_rectangle4);
}

static void
empathy_call_window_preview_on_drag_begin_cb (ClutterDragAction *action,
    ClutterActor *actor,
    gfloat event_x,
    gfloat event_y,
    ClutterModifierType modifiers,
    EmpathyCallWindow *self)
{
  ClutterActor *stage = clutter_actor_get_stage (actor);
  ClutterActor *preview = clutter_clone_new (actor);

  clutter_container_add_actor (CLUTTER_CONTAINER (stage), preview);
  clutter_actor_set_position (preview, event_x, event_y);

  clutter_drag_action_set_drag_handle (action, preview);

  clutter_actor_set_opacity (actor, 0);
  clutter_actor_hide (self->priv->preview_shown_button);

  empathy_call_window_show_preview_rectangles (self, TRUE);
  empathy_call_window_darken_preview_rectangles (self);
}

static void
empathy_call_window_preview_on_drag_end_cb (ClutterDragAction *action,
    ClutterActor *actor,
    gfloat event_x,
    gfloat event_y,
    ClutterModifierType modifiers,
    EmpathyCallWindow *self)
{
  ClutterActor *preview = clutter_drag_action_get_drag_handle (action);
  PreviewPosition pos;

  /* Get the position before destroying the drag actor, otherwise the
   * preview_box allocation won't be valid and we won't be able to
   * calculate the position. */
  pos = empathy_call_window_get_preview_position (self, event_x, event_y);

  /* Destroy the video preview copy that we were dragging */
  clutter_actor_destroy (preview);

  clutter_actor_set_opacity (actor, 255);
  clutter_actor_show (self->priv->preview_shown_button);

  if (pos != PREVIEW_POS_NONE)
    empathy_call_window_move_video_preview (self, pos);

  empathy_call_window_show_preview_rectangles (self, FALSE);
}

static void
empathy_call_window_preview_on_drag_motion_cb (ClutterDragAction *action,
    ClutterActor *actor,
    gfloat delta_x,
    gfloat delta_y,
    EmpathyCallWindow *self)
{
  PreviewPosition pos;
  gfloat event_x, event_y;

  clutter_drag_action_get_motion_coords (action, &event_x, &event_y);

  pos = empathy_call_window_get_preview_position (self, event_x, event_y);

  if (pos != PREVIEW_POS_NONE)
    empathy_call_window_highlight_preview_rectangle (self, pos);
  else
    empathy_call_window_darken_preview_rectangles (self);
}

static gboolean
empathy_call_window_preview_enter_event_cb (ClutterActor *actor,
    ClutterCrossingEvent *event,
    EmpathyCallWindow *self)
{
  ClutterActor *rectangle;

  rectangle = empathy_call_window_get_preview_rectangle (self,
      self->priv->preview_pos);

  empathy_call_window_highlight_preview_rectangle (self,
      self->priv->preview_pos);

  clutter_actor_show (rectangle);

  return FALSE;
}

static gboolean
empathy_call_window_preview_leave_event_cb (ClutterActor *actor,
    ClutterCrossingEvent *event,
    EmpathyCallWindow *self)
{
  ClutterActor *rectangle;

  rectangle = empathy_call_window_get_preview_rectangle (self,
      self->priv->preview_pos);

  empathy_call_window_darken_preview_rectangle (self, rectangle);

  clutter_actor_hide (rectangle);

  return FALSE;
}

static void
create_video_preview (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  ClutterLayoutManager *layout, *layout_center, *layout_end;
  ClutterActor *preview;
  ClutterActor *box;
  ClutterActor *b;
  ClutterAction *action;
  GtkWidget *button;

  g_assert (priv->video_preview == NULL);

  preview = empathy_rounded_texture_new ();
  clutter_actor_set_size (preview,
      SELF_VIDEO_SECTION_WIDTH, SELF_VIDEO_SECTION_HEIGHT);
  priv->video_preview_sink = clutter_gst_video_sink_new (
      CLUTTER_TEXTURE (preview));

  /* Add a little offset to the video preview */
  layout = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_START);
  priv->video_preview = clutter_box_new (layout);
  clutter_actor_set_size (priv->video_preview,
      SELF_VIDEO_SECTION_WIDTH + 2 * SELF_VIDEO_SECTION_MARGIN,
      SELF_VIDEO_SECTION_HEIGHT + 2 * SELF_VIDEO_SECTION_MARGIN +
      FLOATING_TOOLBAR_HEIGHT + FLOATING_TOOLBAR_SPACING);

  /* We have a box with the margins and the video in the middle inside
   * a bigger box with an extra bottom margin so we're not on top of
   * the floating toolbar. */
  layout_center = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_CENTER);
  box = clutter_box_new (layout_center);
  clutter_actor_set_size (box,
      SELF_VIDEO_SECTION_WIDTH + 2 * SELF_VIDEO_SECTION_MARGIN,
      SELF_VIDEO_SECTION_HEIGHT + 2 * SELF_VIDEO_SECTION_MARGIN);

  clutter_container_add_actor (CLUTTER_CONTAINER (box), preview);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->video_preview), box);

  g_object_set (priv->video_preview_sink,
      "sync", FALSE,
      "async", TRUE,
      NULL);

  /* Translators: this is an "Info" label. It should be as short
   * as possible. */
  button = gtk_button_new_with_label (_("i"));
  priv->preview_shown_button = b = empathy_rounded_actor_new ();
  gtk_container_add (
      GTK_CONTAINER (gtk_clutter_actor_get_widget (GTK_CLUTTER_ACTOR (b))),
      button);
  clutter_actor_set_size (b, 24, 24);

  layout_end = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_END,
      CLUTTER_BIN_ALIGNMENT_END);
  box = clutter_box_new (layout_end);
  clutter_actor_set_size (box,
      SELF_VIDEO_SECTION_WIDTH,
      SELF_VIDEO_SECTION_HEIGHT + SELF_VIDEO_SECTION_MARGIN);

  clutter_container_add_actor (CLUTTER_CONTAINER (box), b);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->video_preview), box);

  g_signal_connect (button, "clicked",
      G_CALLBACK (empathy_call_window_preview_button_clicked_cb),
      self);

  /* Translators: this is an "Info" label. It should be as short
   * as possible. */
  button = gtk_button_new_with_label (_("i"));
  b = empathy_rounded_actor_new ();
  gtk_container_add (
      GTK_CONTAINER (gtk_clutter_actor_get_widget (GTK_CLUTTER_ACTOR (b))),
      button);
  clutter_actor_set_size (b, 24, 24);
  priv->preview_hidden_button = b;

  clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (priv->video_layout),
      priv->preview_hidden_button,
      CLUTTER_BIN_ALIGNMENT_START,
      CLUTTER_BIN_ALIGNMENT_END);

  self->priv->preview_pos = PREVIEW_POS_BOTTOM_LEFT;

  clutter_actor_hide (priv->preview_hidden_button);

  g_signal_connect (button, "clicked",
      G_CALLBACK (empathy_call_window_preview_hidden_button_clicked_cb),
      self);

  clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (priv->video_layout),
      priv->video_preview,
      CLUTTER_BIN_ALIGNMENT_START,
      CLUTTER_BIN_ALIGNMENT_END);

  action = clutter_drag_action_new ();
  g_signal_connect (action, "drag-begin",
      G_CALLBACK (empathy_call_window_preview_on_drag_begin_cb), self);
  g_signal_connect (action, "drag-end",
      G_CALLBACK (empathy_call_window_preview_on_drag_end_cb), self);
  g_signal_connect (action, "drag-motion",
      G_CALLBACK (empathy_call_window_preview_on_drag_motion_cb), self);

  g_signal_connect (preview, "enter-event",
      G_CALLBACK (empathy_call_window_preview_enter_event_cb), self);
  g_signal_connect (preview, "leave-event",
      G_CALLBACK (empathy_call_window_preview_leave_event_cb), self);

  clutter_actor_add_action (preview, action);
  clutter_actor_set_reactive (preview, TRUE);
  clutter_actor_set_reactive (priv->preview_shown_button, TRUE);
}

void
empathy_call_window_play_camera (EmpathyCallWindow *window,
    gboolean play)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
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

  preview = priv->video_preview_sink;

  gst_element_set_state (preview, state);
  gst_element_set_state (priv->video_input, state);
  gst_element_set_state (priv->video_tee, state);
}

static void
display_video_preview (EmpathyCallWindow *self,
    gboolean display)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (display)
    {
      /* Display the video preview */
      DEBUG ("Show video preview");

      empathy_call_window_play_camera (self, TRUE);
      clutter_actor_show (priv->video_preview);
      clutter_actor_raise_top (priv->floating_toolbar);
    }
  else
    {
      /* Hide the video preview */
      DEBUG ("Hide video preview");

      if (priv->video_preview != NULL)
        {
          clutter_actor_hide (priv->video_preview);
          empathy_call_window_play_camera (self, FALSE);
        }
    }
}

static void
empathy_call_window_set_state_connecting (EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  empathy_call_window_status_message (window, _("Connecting…"));
  priv->call_state = CONNECTING;

  /* Show the toolbar */
  clutter_state_set_state (priv->transitions, "fade-in");

  if (priv->outgoing)
    empathy_sound_manager_start_playing (priv->sound_mgr, GTK_WIDGET (window),
        EMPATHY_SOUND_PHONE_OUTGOING, MS_BETWEEN_RING);
}

static void
disable_camera (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (priv->camera_state == CAMERA_STATE_OFF)
    return;

  DEBUG ("Disable camera");

  display_video_preview (self, FALSE);

  if (priv->camera_state == CAMERA_STATE_ON)
    empathy_call_window_set_send_video (self, CAMERA_STATE_OFF);

  priv->camera_state = CAMERA_STATE_OFF;
}

static void
enable_camera (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (priv->camera_state == CAMERA_STATE_ON)
    return;

  if (priv->video_input == NULL)
    {
      DEBUG ("Can't enable camera, no input");
      return;
    }

  DEBUG ("Enable camera");

  empathy_call_window_set_send_video (self, CAMERA_STATE_ON);

  priv->camera_state = CAMERA_STATE_ON;
}

#ifdef HAVE_VIDEO_EFFECT

static void
container_remove_all_children (ClutterContainer *c)
{
  GList *chld, *l;
  chld = clutter_container_get_children (c);
  for (l = chld; l; l = g_list_next (l))
    clutter_container_remove_actor (c, CLUTTER_ACTOR (l->data));
  g_list_free (chld);
}

static void
empathy_call_window_activate_effect_page (EmpathyCallWindow *self, int page_id)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  ClutterActor          *eff_page;

  container_remove_all_children (CLUTTER_CONTAINER (priv->effects_box));
  priv->curr_effect_page = page_id;
  eff_page = priv->effect_pages[page_id];

  /* keep a reference so it won't die when we'll remove it
   * in later call of container_remove_all_children () */
  g_object_ref (eff_page);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->effects_box), eff_page);

  g_object_set (G_OBJECT (eff_page), "opacity", 0, NULL);
  clutter_actor_animate (eff_page, CLUTTER_LINEAR, 1000, "opacity", 255, NULL);
}

static void
empathy_call_window_video_effects_toggle (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  priv->video_effects_on = !priv->video_effects_on;

  if (priv->video_effects_on)
  {
    clutter_actor_hide (priv->video_box);
    empathy_call_window_activate_effect_page (self, priv->curr_effect_page);
    clutter_actor_show (priv->effects_box);
  }
  else
  {
    clutter_actor_hide (priv->effects_box);
    clutter_actor_show (priv->video_box);
  }
}

static void
empathy_call_window_video_effects_cb (GtkAction *action,
  EmpathyCallWindow *self)
{
  empathy_call_window_video_effects_toggle (self);
}

#else /* HAVE_VIDEO_EFFECT */

static void
empathy_call_window_video_effects_cb (GtkAction *action,
  EmpathyCallWindow *self) { }

#endif /* HAVE_VIDEO_EFFECT */

static void
empathy_call_window_camera_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyCallWindow *self)
{
  if (gtk_toggle_tool_button_get_active (toggle))
    enable_camera (self);
  else
    disable_camera (self);
}

static void
create_pipeline (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstBus *bus;

  g_assert (priv->pipeline == NULL);

  priv->pipeline = gst_pipeline_new (NULL);
  priv->pipeline_playing = FALSE;

  priv->video_tee = gst_element_factory_make ("tee", NULL);
  gst_object_ref (priv->video_tee);
  gst_object_sink (priv->video_tee);

  gst_bin_add (GST_BIN (priv->pipeline), priv->video_tee);

  bus = gst_pipeline_get_bus (GST_PIPELINE (priv->pipeline));
  priv->bus_message_source_id = gst_bus_add_watch (bus,
      empathy_call_window_bus_message, self);

  g_object_unref (bus);
}

static void
empathy_call_window_settings_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  gchar *args = g_strdup_printf ("-p %s",
      empathy_preferences_tab_to_string (EMPATHY_PREFERENCES_TAB_CALLS));

  empathy_launch_program (BIN_DIR, "empathy", args);

  g_free (args);
}

static void
empathy_call_window_contents_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  empathy_url_show (GTK_WIDGET (self), "ghelp:empathy?audio-video");
}

static void
empathy_call_window_debug_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  empathy_launch_program (BIN_DIR, "empathy-debugger", "-s Empathy.Call");
}

static void
empathy_call_window_about_cb (GtkAction *action,
    EmpathyCallWindow *self)
{
  empathy_about_dialog_new (GTK_WINDOW (self));
}

static gboolean
empathy_call_window_toolbar_timeout (gpointer data)
{
  EmpathyCallWindow *self = data;

  /* We don't want to hide the toolbar if we're not in a call, as
   * to show the call status all the time. */
  if (self->priv->call_state != CONNECTING &&
      self->priv->call_state != DISCONNECTED)
    clutter_state_set_state (self->priv->transitions, "fade-out");

  return TRUE;
}

static gboolean
empathy_call_window_motion_notify_cb (GtkWidget *widget,
    GdkEvent *event,
    EmpathyCallWindow *self)
{
  clutter_state_set_state (self->priv->transitions, "fade-in");

  if (self->priv->inactivity_src > 0)
    g_source_remove (self->priv->inactivity_src);

  self->priv->inactivity_src = g_timeout_add_seconds (3,
      empathy_call_window_toolbar_timeout, self);

  return FALSE;
}

static gboolean
empathy_call_window_configure_event_cb (GtkWidget *widget,
    GdkEvent  *event,
    EmpathyCallWindow *self)
{
  GdkWindow *gdk_window;
  GdkWindowState window_state;

  gtk_window_get_position (GTK_WINDOW (self), &self->priv->x, &self->priv->y);
  gtk_window_get_size (GTK_WINDOW (self), &self->priv->w, &self->priv->h);

  gtk_widget_get_preferred_width (self->priv->dtmf_panel,
      &self->priv->dialpad_width, NULL);

  gdk_window = gtk_widget_get_window (widget);
  window_state = gdk_window_get_state (gdk_window);
  self->priv->maximized = (window_state & GDK_WINDOW_STATE_MAXIMIZED);

  return FALSE;
}

static void
empathy_call_window_destroyed_cb (GtkWidget *object,
    EmpathyCallWindow *self)
{
  if (gtk_widget_get_visible (self->priv->dtmf_panel))
    {
      /* Save the geometry as if the dialpad was hidden. */
      empathy_geometry_save_values (GTK_WINDOW (self),
          self->priv->x, self->priv->y,
          self->priv->w - self->priv->dialpad_width, self->priv->h,
          self->priv->maximized);
    }
}

#ifdef HAVE_VIDEO_EFFECT
static gint
sort_effects_by_name (gconstpointer pa, gconstpointer pb)
{
  gint cmp;
  gchar *a, *b;
  g_object_get (G_OBJECT (pa), "name", &a, NULL);
  g_object_get (G_OBJECT (pb), "name", &b, NULL);
  cmp = strcmp (a, b);
  g_free (a);
  g_free (b);
  return cmp;
}

static void
empathy_call_window_load_video_effects (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  CheeseEffect          *identity;
  GList                 *list, *l;
  int i;

  list = cheese_effect_load_effects ();
  if (!list)
    return;
  if (g_list_length (list) == 0)
  {
    g_list_free (list);
    return;
  }

  identity = cheese_effect_new ();
  g_object_set (identity, "name", _("No effect"), NULL);
  g_object_set (identity, "pipeline-desc", "identity", NULL);

  priv->cheese_effects_count = g_list_length (list) + 1;
  priv->cheese_effects = \
    g_malloc (sizeof (CheeseEffect*) * priv->cheese_effects_count);
  priv->cheese_effects[0] = identity;

  list = g_list_sort (list, sort_effects_by_name);

  for (l = list, i = 1; l; l = g_list_next(l), i++)
    priv->cheese_effects[i] = (CheeseEffect*) l->data;
  g_list_free (list);
}


static gboolean
empathy_call_window_on_selected_effect_change_cb (ClutterActor       *sender,
                                                  ClutterButtonEvent *event,
                                                  gpointer           self)
{
  return FALSE;
}


static void
empathy_call_window_init_video_effects (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  int i;

  empathy_call_window_load_video_effects (self);

  priv->effect_pages_count = \
    (priv->cheese_effects_count + EFFECTS_PER_PAGE - 1) / EFFECTS_PER_PAGE;
  priv->effect_pages = \
    g_malloc (sizeof (ClutterActor*) * priv->effect_pages_count);

  for (i = 0; i < priv->effect_pages_count; i++)
  {
    ClutterTableLayout* table_layout;
    ClutterBox* grid;

    table_layout = (ClutterTableLayout*) clutter_table_layout_new ();
    clutter_table_layout_set_column_spacing (table_layout, (guint) 10);
    clutter_table_layout_set_row_spacing (table_layout, (guint) 10);

    grid = (ClutterBox*) clutter_box_new ((ClutterLayoutManager*) table_layout);
    priv->effect_pages[i] = CLUTTER_ACTOR (grid);
  }

  for (i = 0; i < priv->cheese_effects_count; i++)
  {
    CheeseEffect         *effect;
    gchar                *effect_name = NULL;
    ClutterTexture       *texture;
    ClutterLayoutManager *layout;
    ClutterBox           *box;
    ClutterText          *text;
    ClutterRectangle     *rect;
    ClutterTableLayout   *table_layout;
    ClutterColor         rect_color, text_color;
    gfloat               text_height;
    int                  effect_page_nr;
    ClutterBox           *effect_page;

    effect = priv->cheese_effects[i];

    texture = CLUTTER_TEXTURE (clutter_texture_new ());
    clutter_texture_set_keep_aspect_ratio (texture, TRUE);

    layout = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
                                     CLUTTER_BIN_ALIGNMENT_CENTER);
    box = CLUTTER_BOX (clutter_box_new ((ClutterLayoutManager*) layout));
    clutter_box_pack (box, CLUTTER_ACTOR (texture), NULL, NULL, NULL);
    clutter_actor_set_reactive (CLUTTER_ACTOR (box), TRUE);
    g_signal_connect (CLUTTER_ACTOR (box), "button-press-event",
                      G_CALLBACK (empathy_call_window_on_selected_effect_change_cb), self);

    g_object_set_data (G_OBJECT (box), "effect", effect);
    g_object_set_data (G_OBJECT (effect), "texture", texture);

    rect = CLUTTER_RECTANGLE (clutter_rectangle_new ());
    clutter_actor_set_opacity ((ClutterActor*) rect, (guint) 128);
    clutter_color_from_string (&rect_color, "black");
    clutter_rectangle_set_color (rect, &rect_color);

    text = CLUTTER_TEXT (clutter_text_new ());
    g_object_get (effect, "name", &effect_name, NULL);
    clutter_text_set_text (text, effect_name);
    g_free (effect_name);
    clutter_color_from_string (&text_color, "white");
    clutter_text_set_color (text, &text_color);

    text_height = clutter_actor_get_height ((ClutterActor*) text);

    clutter_actor_set_height (CLUTTER_ACTOR (rect), text_height + 5);
    clutter_box_pack (box, CLUTTER_ACTOR (rect),
                      "x-align", CLUTTER_BIN_ALIGNMENT_FILL,
                      "y-align", CLUTTER_BIN_ALIGNMENT_END,
                      NULL, NULL);
    clutter_box_pack (box, CLUTTER_ACTOR (text),
                      "x-align", CLUTTER_BIN_ALIGNMENT_CENTER,
                      "y-align", CLUTTER_BIN_ALIGNMENT_END,
                      NULL, NULL);

    effect_page_nr = i / EFFECTS_PER_PAGE;
    effect_page = CLUTTER_BOX (priv->effect_pages[effect_page_nr]);
    table_layout = CLUTTER_TABLE_LAYOUT (clutter_box_get_layout_manager (effect_page));

    clutter_table_layout_pack (table_layout, CLUTTER_ACTOR (box),
                               (i % EFFECTS_PER_PAGE) % EFFECT_COLS,
                               (i % EFFECTS_PER_PAGE) / EFFECT_ROWS);
    clutter_table_layout_set_expand (table_layout, CLUTTER_ACTOR (box), FALSE, FALSE);
  }
  priv->curr_effect_page = 0;
}

#else /* HAVE_VIDEO_EFFECT */

static void
empathy_call_window_init_video_effects (EmpathyCallWindow *self) { }

#endif /* HAVE_VIDEO_EFFECT */

static void
empathy_call_window_stage_allocation_changed_cb (ClutterActor *stage,
    GParamSpec *pspec,
    ClutterBindConstraint *constraint)
{
  ClutterActorBox allocation;

  clutter_actor_get_allocation_box (stage, &allocation);

  clutter_bind_constraint_set_offset (constraint,
      allocation.y2 - allocation.y1 -
      FLOATING_TOOLBAR_SPACING - FLOATING_TOOLBAR_HEIGHT);
}

static void
empathy_call_window_init (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv;
  GtkBuilder *gui;
  GtkWidget *top_vbox;
  gchar *filename;
  ClutterConstraint *constraint;
  ClutterActor *remote_avatar;
  GtkStyleContext *context;
  GdkRGBA rgba;
  ClutterColor bg;

  priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_CALL_WINDOW, EmpathyCallWindowPriv);

  filename = empathy_file_lookup ("empathy-call-window.ui", "src");
  gui = empathy_builder_get_file (filename,
    "call_window_vbox", &top_vbox,
    "errors_vbox", &priv->errors_vbox,
    "pane", &priv->pane,
    "remote_user_name_toolbar", &priv->remote_user_name_toolbar,
    "remote_user_avatar_toolbar", &priv->remote_user_avatar_toolbar,
    "status_label", &priv->status_label,
    "audiocall", &priv->audio_call_button,
    "videocall", &priv->video_call_button,
    "microphone", &priv->mic_button,
    "camera", &priv->camera_button,
    "menupreviewvideoeffects", &priv->video_effects_button,
    "hangup", &priv->hangup_button,
    "dialpad", &priv->dialpad_button,
    "toolbar", &priv->toolbar,
    "bottom_toolbar", &priv->bottom_toolbar,
    "ui_manager", &priv->ui_manager,
    "menufullscreen", &priv->menu_fullscreen,
    "menupreviewswap", &priv->menu_swap_camera,
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
    "hangup", "clicked", empathy_call_window_hangup_cb,
    "audiocall", "clicked", empathy_call_window_audio_call_cb,
    "videocall", "clicked", empathy_call_window_video_call_cb,
    "volume", "value-changed", empathy_call_window_volume_changed_cb,
    "microphone", "toggled", empathy_call_window_mic_toggled_cb,
    "camera", "toggled", empathy_call_window_camera_toggled_cb,
    "dialpad", "toggled", empathy_call_window_dialpad_cb,
    "menufullscreen", "activate", empathy_call_window_fullscreen_cb,
    "menusettings", "activate", empathy_call_window_settings_cb,
    "menucontents", "activate", empathy_call_window_contents_cb,
    "menudebug", "activate", empathy_call_window_debug_cb,
    "menuabout", "activate", empathy_call_window_about_cb,
    "menupreviewdisable", "activate", empathy_call_window_disable_camera_cb,
    "menupreviewminimise", "activate", empathy_call_window_minimise_camera_cb,
    "menupreviewmaximise", "activate", empathy_call_window_maximise_camera_cb,
    "menupreviewswap", "activate", empathy_call_window_swap_camera_cb,
    "menupreviewvideoeffects", "activate", empathy_call_window_video_effects_cb,
    NULL);

  gtk_action_set_sensitive (priv->menu_fullscreen, FALSE);

  priv->camera_monitor = empathy_camera_monitor_dup_singleton ();

  g_object_bind_property (priv->camera_monitor, "available",
      priv->camera_button, "sensitive",
      G_BINDING_SYNC_CREATE);
  g_object_bind_property (priv->camera_monitor, "available",
      priv->video_effects_button, "sensitive",
      G_BINDING_SYNC_CREATE);

  #ifdef HAVE_VIDEO_EFFECT
  /* video_effects_button is hidden by default.
     Only show it if we have access to cheeese effects. */
  g_object_set (priv->video_effects_button, "visible", TRUE, NULL);
  #endif /* HAVE_VIDEO_EFFECT */

  g_signal_connect (priv->camera_monitor, "added",
      G_CALLBACK (empathy_call_window_camera_added_cb), self);
  g_signal_connect (priv->camera_monitor, "removed",
      G_CALLBACK (empathy_call_window_camera_removed_cb), self);

  priv->lock = g_mutex_new ();

  gtk_container_add (GTK_CONTAINER (self), top_vbox);

  priv->content_hbox = gtk_hbox_new (FALSE, CONTENT_HBOX_SPACING);
  gtk_container_set_border_width (GTK_CONTAINER (priv->content_hbox),
                                  CONTENT_HBOX_BORDER_WIDTH);
  gtk_box_pack_start (GTK_BOX (priv->pane), priv->content_hbox,
      TRUE, TRUE, 0);

  /* root box: contains the avatar/video box and the effects box */
  priv->root_box = clutter_box_new (clutter_bin_layout_new (
      CLUTTER_BIN_ALIGNMENT_FILL, CLUTTER_BIN_ALIGNMENT_FILL));

  /* root effects box: will contain boxes with 3x3 tables of effect previews */
  priv->effects_box = clutter_box_new (clutter_bin_layout_new (
      CLUTTER_BIN_ALIGNMENT_FILL, CLUTTER_BIN_ALIGNMENT_FILL));
  clutter_actor_hide (priv->effects_box);

  /* avatar/video box */
  priv->video_layout = clutter_bin_layout_new (CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_CENTER);

  priv->video_box = clutter_box_new (priv->video_layout);

  priv->video_container = gtk_clutter_embed_new ();

  /* Set the background color to that of the rest of the window */
  context = gtk_widget_get_style_context (priv->content_hbox);
  gtk_style_context_get_background_color (context,
      GTK_STATE_FLAG_NORMAL, &rgba);
  bg.red = CLAMP (rgba.red * 255.0, 0, 255);
  bg.green = CLAMP (rgba.green * 255.0, 0, 255);
  bg.blue = CLAMP (rgba.blue * 255.0, 0, 255);
  bg.alpha = CLAMP (rgba.alpha * 255.0, 0, 255);
  clutter_stage_set_color (
      CLUTTER_STAGE (gtk_clutter_embed_get_stage (
          GTK_CLUTTER_EMBED (priv->video_container))),
      &bg);

  clutter_container_add_actor (CLUTTER_CONTAINER (priv->root_box),
      priv->video_box);
  clutter_container_add_actor (CLUTTER_CONTAINER (priv->root_box),
      priv->effects_box);

  clutter_container_add (
      CLUTTER_CONTAINER (gtk_clutter_embed_get_stage (
          GTK_CLUTTER_EMBED (priv->video_container))),
      priv->root_box,
      NULL);

  constraint = clutter_bind_constraint_new (
      gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (priv->video_container)),
      CLUTTER_BIND_SIZE, 0);
  clutter_actor_add_constraint (priv->root_box, constraint);

  priv->remote_user_avatar_widget = gtk_image_new ();
  remote_avatar = gtk_clutter_actor_new_with_contents (
      priv->remote_user_avatar_widget);

  clutter_container_add_actor (CLUTTER_CONTAINER (priv->video_box),
      remote_avatar);

  empathy_call_window_create_preview_rectangles (self);

  gtk_box_pack_start (GTK_BOX (priv->content_hbox),
      priv->video_container, TRUE, TRUE,
      CONTENT_HBOX_CHILDREN_PACKING_PADDING);

  create_pipeline (self);
  create_video_output_widget (self);
  create_audio_input (self);
  create_video_input (self);
  empathy_call_window_init_video_effects (self);

  priv->floating_toolbar = empathy_rounded_actor_new ();

  gtk_widget_reparent (priv->bottom_toolbar,
      gtk_clutter_actor_get_widget (GTK_CLUTTER_ACTOR (priv->floating_toolbar)));

  constraint = clutter_bind_constraint_new (
      gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (priv->video_container)),
      CLUTTER_BIND_Y, 0);

  clutter_actor_add_constraint (priv->floating_toolbar, constraint);

  g_signal_connect (
      gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (priv->video_container)),
      "notify::allocation",
      G_CALLBACK (empathy_call_window_stage_allocation_changed_cb),
      constraint);

  clutter_actor_set_size (priv->floating_toolbar,
      FLOATING_TOOLBAR_WIDTH, FLOATING_TOOLBAR_HEIGHT);
  clutter_actor_set_opacity (priv->floating_toolbar, FLOATING_TOOLBAR_OPACITY);

  clutter_bin_layout_add (CLUTTER_BIN_LAYOUT (priv->video_layout),
      priv->floating_toolbar,
      CLUTTER_BIN_ALIGNMENT_CENTER,
      CLUTTER_BIN_ALIGNMENT_END);

  clutter_actor_raise_top (priv->floating_toolbar);

  /* Transitions for the floating toolbar */
  priv->transitions = clutter_state_new ();

  /* all transitions last for 2s */
  clutter_state_set_duration (priv->transitions, NULL, NULL, 2000);

  /* transition from any state to "fade-out" state */
  clutter_state_set (priv->transitions, NULL, "fade-out",
      priv->floating_toolbar,
      "opacity", CLUTTER_EASE_OUT_QUAD, 0,
      NULL);

  /* transition from any state to "fade-in" state */
  clutter_state_set (priv->transitions, NULL, "fade-in",
      priv->floating_toolbar,
      "opacity", CLUTTER_EASE_OUT_QUAD, FLOATING_TOOLBAR_OPACITY,
      NULL);

  /* put the actor into the "fade-in" state with no animation */
  clutter_state_warp_to_state (priv->transitions, "fade-in");

  /* The call will be started as soon the pipeline is playing */
  priv->start_call_when_playing = TRUE;

  priv->dtmf_panel = empathy_create_dtmf_dialpad (G_OBJECT (self),
      G_CALLBACK (dtmf_button_pressed_cb),
      G_CALLBACK (dtmf_button_released_cb));

  gtk_box_pack_start (GTK_BOX (priv->pane), priv->dtmf_panel,
      FALSE, FALSE, 6);

  gtk_box_pack_start (GTK_BOX (priv->pane), priv->details_vbox,
      FALSE, FALSE, 0);

  gtk_widget_set_sensitive (priv->dtmf_panel, FALSE);

  gtk_widget_show_all (top_vbox);

  gtk_widget_hide (priv->dtmf_panel);
  gtk_widget_hide (priv->details_vbox);

  priv->fullscreen = empathy_call_window_fullscreen_new (self);

  empathy_call_window_fullscreen_set_video_widget (priv->fullscreen,
      priv->video_container);

  /* We hide the bottom toolbar after 3s of inactivity and show it
   * again on mouse movement */
  priv->inactivity_src = g_timeout_add_seconds (3,
      empathy_call_window_toolbar_timeout, self);

  g_signal_connect (G_OBJECT (priv->fullscreen->leave_fullscreen_button),
      "clicked", G_CALLBACK (empathy_call_window_fullscreen_cb), self);

  g_signal_connect (G_OBJECT (self), "realize",
    G_CALLBACK (empathy_call_window_realized_cb), self);

  g_signal_connect (G_OBJECT (self), "delete-event",
    G_CALLBACK (empathy_call_window_delete_cb), self);

  g_signal_connect (G_OBJECT (self), "window-state-event",
    G_CALLBACK (empathy_call_window_state_event_cb), self);

  g_signal_connect (G_OBJECT (self), "key-press-event",
      G_CALLBACK (empathy_call_window_key_press_cb), self);

  g_signal_connect (self, "motion-notify-event",
      G_CALLBACK (empathy_call_window_motion_notify_cb), self);

  priv->timer = g_timer_new ();

  g_object_ref (priv->ui_manager);
  g_object_unref (gui);

  priv->sound_mgr = empathy_sound_manager_dup_singleton ();
  priv->mic_menu = empathy_mic_menu_new (self);
  priv->camera_menu = empathy_camera_menu_new (self);

  empathy_call_window_show_hangup_button (self, TRUE);

  priv->settings = g_settings_new (EMPATHY_PREFS_CALL_SCHEMA);

  /* Retrieve initial volume */
  priv->volume = g_settings_get_double (priv->settings,
      EMPATHY_PREFS_CALL_SOUND_VOLUME) / 100.0;

  g_signal_connect (priv->settings, "changed::"EMPATHY_PREFS_CALL_SOUND_VOLUME,
      G_CALLBACK (empathy_call_window_prefs_volume_changed_cb), self);

  empathy_geometry_bind (GTK_WINDOW (self), "call-window");
  /* These signals are used to track the window position and save it
   * when the window is destroyed. We need to do this as we don't want
   * the window geometry to be saved with the dialpad taken into account. */
  g_signal_connect (self, "destroy",
      G_CALLBACK (empathy_call_window_destroyed_cb), self);
  g_signal_connect (self, "configure-event",
      G_CALLBACK (empathy_call_window_configure_event_cb), self);
  g_signal_connect (self, "window-state-event",
      G_CALLBACK (empathy_call_window_configure_event_cb), self);

  /* Don't display labels in both toolbars */
  gtk_toolbar_set_style (GTK_TOOLBAR (priv->toolbar), GTK_TOOLBAR_ICONS);
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
set_window_title (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  gchar *tmp;

  if (priv->contact != NULL)
    {
      /* translators: Call is a noun and %s is the contact name. This string
       * is used in the window title */
      tmp = g_strdup_printf (_("Call with %s"),
          empathy_contact_get_alias (priv->contact));
      gtk_window_set_title (GTK_WINDOW (self), tmp);
      g_free (tmp);
    }
  else
    {
      g_warning ("Unknown remote contact!");
    }
}

static void
set_remote_user_name (EmpathyCallWindow *self,
  EmpathyContact *contact)
{
  const gchar *alias = empathy_contact_get_alias (contact);
  const gchar *status = empathy_contact_get_status (contact);
  gchar *label;

  label = g_strdup_printf ("%s\n<small>%s</small>", alias, status);
  gtk_label_set_markup (GTK_LABEL (self->priv->remote_user_name_toolbar),
      label);
  g_free (label);
}

static void
contact_name_changed_cb (EmpathyContact *contact,
    GParamSpec *pspec,
    EmpathyCallWindow *self)
{
  set_window_title (self);
  set_remote_user_name (self, contact);
}

static void
contact_presence_changed_cb (EmpathyContact *contact,
    GParamSpec *pspec,
    EmpathyCallWindow *self)
{
  set_remote_user_name (self, contact);
}

static void
contact_avatar_changed_cb (EmpathyContact *contact,
    GParamSpec *pspec,
    EmpathyCallWindow *self)
{
  int size;
  GtkAllocation allocation;
  GtkWidget *avatar_widget;

  avatar_widget = self->priv->remote_user_avatar_widget;

  gtk_widget_get_allocation (avatar_widget, &allocation);
  size = allocation.height;

  if (size == 0)
    {
      /* the widget is not allocated yet, set a default size */
      size = MIN (REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT,
          REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH);
    }

  init_contact_avatar_with_size (contact, avatar_widget, size);

  avatar_widget = self->priv->remote_user_avatar_toolbar;

  gtk_widget_get_allocation (avatar_widget, &allocation);
  size = allocation.height;

  if (size == 0)
    {
      /* the widget is not allocated yet, set a default size */
      size = SMALL_TOOLBAR_SIZE;
    }

  init_contact_avatar_with_size (contact, avatar_widget, size);
}

static void
empathy_call_window_setup_avatars (EmpathyCallWindow *self,
    EmpathyCallHandler *handler)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  tp_g_signal_connect_object (priv->contact, "notify::name",
      G_CALLBACK (contact_name_changed_cb), self, 0);
  tp_g_signal_connect_object (priv->contact, "notify::avatar",
    G_CALLBACK (contact_avatar_changed_cb), self, 0);
  tp_g_signal_connect_object (priv->contact, "notify::presence",
      G_CALLBACK (contact_presence_changed_cb), self, 0);

  set_window_title (self);
  set_remote_user_name (self, priv->contact);

  init_contact_avatar_with_size (priv->contact,
      priv->remote_user_avatar_widget,
      MIN (REMOTE_CONTACT_AVATAR_DEFAULT_WIDTH,
          REMOTE_CONTACT_AVATAR_DEFAULT_HEIGHT));

  init_contact_avatar_with_size (priv->contact,
      priv->remote_user_avatar_toolbar,
      SMALL_TOOLBAR_SIZE);

  /* The remote avatar is shown by default and will be hidden when we receive
     video from the remote side. */
  clutter_actor_hide (priv->video_output);
  gtk_widget_show (priv->remote_user_avatar_widget);
}

static void
update_send_codec (EmpathyCallWindow *self,
    gboolean audio)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  FsCodec *codec;
  GtkWidget *widget;
  gchar *tmp;

  if (audio)
    {
      codec = empathy_call_handler_get_send_audio_codec (priv->handler);
      widget = priv->acodec_encoding_label;
    }
  else
    {
      codec = empathy_call_handler_get_send_video_codec (priv->handler);
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
  EmpathyCallWindow *self = user_data;

  update_send_codec (self, TRUE);
}

static void
send_video_codec_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyCallWindow *self = user_data;

  update_send_codec (self, FALSE);
}

static void
update_recv_codec (EmpathyCallWindow *self,
    gboolean audio)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GList *codecs, *l;
  GtkWidget *widget;
  GString *str = NULL;

  if (audio)
    {
      codecs = empathy_call_handler_get_recv_audio_codecs (priv->handler);
      widget = priv->acodec_decoding_label;
    }
  else
    {
      codecs = empathy_call_handler_get_recv_video_codecs (priv->handler);
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
  EmpathyCallWindow *self = user_data;

  update_recv_codec (self, TRUE);
}

static void
recv_video_codecs_notify_cb (GObject *object,
    GParamSpec *pspec,
    gpointer user_data)
{
  EmpathyCallWindow *self = user_data;

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
update_candidat_widget (EmpathyCallWindow *self,
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
    EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  FsCandidate *candidate = NULL;

  if (type == FS_MEDIA_TYPE_VIDEO)
    {
      /* Update remote candidate */
      candidate = empathy_call_handler_get_video_remote_candidate (
          priv->handler);

      update_candidat_widget (self, priv->video_remote_candidate_label,
          priv->video_remote_candidate_info_img, candidate);

      /* Update local candidate */
      candidate = empathy_call_handler_get_video_local_candidate (
          priv->handler);

      update_candidat_widget (self, priv->video_local_candidate_label,
          priv->video_local_candidate_info_img, candidate);
    }
  else
    {
      /* Update remote candidate */
      candidate = empathy_call_handler_get_audio_remote_candidate (
          priv->handler);

      update_candidat_widget (self, priv->audio_remote_candidate_label,
          priv->audio_remote_candidate_info_img, candidate);

      /* Update local candidate */
      candidate = empathy_call_handler_get_audio_local_candidate (
          priv->handler);

      update_candidat_widget (self, priv->audio_local_candidate_label,
          priv->audio_local_candidate_info_img, candidate);
    }
}

static void
empathy_call_window_constructed (GObject *object)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (object);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  TpyCallChannel *call;

  g_assert (priv->handler != NULL);

  g_object_get (priv->handler, "call-channel", &call, NULL);
  priv->outgoing = (call == NULL);
  if (call != NULL)
    g_object_unref (call);

  g_object_get (priv->handler, "target-contact", &priv->contact, NULL);
  g_assert (priv->contact != NULL);

  empathy_call_window_setup_avatars (self, priv->handler);
  empathy_call_window_set_state_connecting (self);

  if (!empathy_call_handler_has_initial_video (priv->handler))
    {
      gtk_toggle_tool_button_set_active (
          GTK_TOGGLE_TOOL_BUTTON (priv->camera_button), FALSE);
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

static void empathy_call_window_dispose (GObject *object);
static void empathy_call_window_finalize (GObject *object);

static void
empathy_call_window_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CALL_HANDLER:
        priv->handler = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_call_window_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CALL_HANDLER:
        g_value_set_object (value, priv->handler);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_call_window_class_init (
  EmpathyCallWindowClass *empathy_call_window_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (empathy_call_window_class);
  GParamSpec *param_spec;

  g_type_class_add_private (empathy_call_window_class,
    sizeof (EmpathyCallWindowPriv));

  object_class->constructed = empathy_call_window_constructed;
  object_class->set_property = empathy_call_window_set_property;
  object_class->get_property = empathy_call_window_get_property;

  object_class->dispose = empathy_call_window_dispose;
  object_class->finalize = empathy_call_window_finalize;

  param_spec = g_param_spec_object ("handler",
    "handler", "The call handler",
    EMPATHY_TYPE_CALL_HANDLER,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
    PROP_CALL_HANDLER, param_spec);
}

void
empathy_call_window_dispose (GObject *object)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (object);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->handler != NULL)
    {
      empathy_call_handler_stop_call (priv->handler);
      tp_clear_object (&priv->handler);
    }

  if (priv->bus_message_source_id != 0)
    {
      g_source_remove (priv->bus_message_source_id);
      priv->bus_message_source_id = 0;
    }

  if (priv->got_video_src > 0)
    {
      g_source_remove (priv->got_video_src);
      priv->got_video_src = 0;
    }

  if (priv->inactivity_src > 0)
    {
      g_source_remove (priv->inactivity_src);
      priv->inactivity_src = 0;
    }

  tp_clear_object (&priv->pipeline);
  tp_clear_object (&priv->video_input);
  tp_clear_object (&priv->audio_input);
  tp_clear_object (&priv->video_tee);
  tp_clear_object (&priv->ui_manager);
  tp_clear_object (&priv->fullscreen);
  tp_clear_object (&priv->camera_monitor);
  tp_clear_object (&priv->settings);
  tp_clear_object (&priv->sound_mgr);
  tp_clear_object (&priv->mic_menu);
  tp_clear_object (&priv->camera_menu);

  g_list_free_full (priv->notifiers, g_object_unref);

  if (priv->timer_id != 0)
    g_source_remove (priv->timer_id);
  priv->timer_id = 0;

  if (priv->contact != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->contact,
          contact_name_changed_cb, self);
      priv->contact = NULL;
    }

  G_OBJECT_CLASS (empathy_call_window_parent_class)->dispose (object);
}

static void
disconnect_video_output_motion_handler (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (priv->video_output_motion_handler_id != 0)
    {
      g_signal_handler_disconnect (G_OBJECT (priv->video_container),
          priv->video_output_motion_handler_id);
      priv->video_output_motion_handler_id = 0;
    }
}

void
empathy_call_window_finalize (GObject *object)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (object);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  disconnect_video_output_motion_handler (self);

  /* free any data held directly by the object here */
  g_mutex_free (priv->lock);

  g_timer_destroy (priv->timer);

  G_OBJECT_CLASS (empathy_call_window_parent_class)->finalize (object);
}


EmpathyCallWindow *
empathy_call_window_new (EmpathyCallHandler *handler)
{
  return EMPATHY_CALL_WINDOW (
    g_object_new (EMPATHY_TYPE_CALL_WINDOW, "handler", handler, NULL));
}

static void
empathy_call_window_conference_added_cb (EmpathyCallHandler *handler,
  GstElement *conference, gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  FsElementAddedNotifier *notifier;
  GKeyFile *keyfile;

  DEBUG ("Conference added");

  /* Add notifier to set the various element properties as needed */
  notifier = fs_element_added_notifier_new ();
  keyfile = fs_utils_get_default_element_properties (conference);

  if (keyfile != NULL)
    fs_element_added_notifier_set_properties_from_keyfile (notifier, keyfile);

  fs_element_added_notifier_add (notifier, GST_BIN (priv->pipeline));

  priv->notifiers = g_list_prepend (priv->notifiers, notifier);

  gst_bin_add (GST_BIN (priv->pipeline), conference);
  gst_element_set_state (conference, GST_STATE_PLAYING);
}

static void
empathy_call_window_conference_removed_cb (EmpathyCallHandler *handler,
  GstElement *conference, gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  gst_bin_remove (GST_BIN (priv->pipeline), conference);
  gst_element_set_state (conference, GST_STATE_NULL);
}

static gboolean
empathy_call_window_reset_pipeline (EmpathyCallWindow *self)
{
  GstStateChangeReturn state_change_return;
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

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

      if (priv->audio_output != NULL)
        g_object_unref (priv->audio_output);
      priv->audio_output = NULL;

      if (priv->video_tee != NULL)
        g_object_unref (priv->video_tee);
      priv->video_tee = NULL;

      if (priv->video_preview != NULL)
        clutter_actor_destroy (priv->video_preview);
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
reset_details_pane (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  gtk_label_set_text (GTK_LABEL (priv->vcodec_encoding_label), _("Unknown"));
  gtk_label_set_text (GTK_LABEL (priv->acodec_encoding_label), _("Unknown"));
  gtk_label_set_text (GTK_LABEL (priv->vcodec_decoding_label), _("Unknown"));
  gtk_label_set_text (GTK_LABEL (priv->acodec_decoding_label), _("Unknown"));
}

static gboolean
empathy_call_window_disconnected (EmpathyCallWindow *self,
    gboolean restart)
{
  gboolean could_disconnect = FALSE;
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  gboolean could_reset_pipeline;

  /* Leave full screen mode if needed */
  gtk_window_unfullscreen (GTK_WINDOW (self));

  gtk_action_set_sensitive (priv->menu_fullscreen, FALSE);
  gtk_widget_set_sensitive (priv->dtmf_panel, FALSE);

  could_reset_pipeline = empathy_call_window_reset_pipeline (self);

  if (priv->call_state == CONNECTING)
      empathy_sound_manager_stop (priv->sound_mgr, EMPATHY_SOUND_PHONE_OUTGOING);

  if (priv->call_state != REDIALING)
    priv->call_state = DISCONNECTED;

  /* Show the toolbar */
  clutter_state_set_state (priv->transitions, "fade-in");

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

      empathy_call_window_status_message (self, _("Disconnected"));

      empathy_call_window_show_hangup_button (self, FALSE);

      /* Unsensitive the camera and mic button */
      gtk_widget_set_sensitive (priv->camera_button, FALSE);
      gtk_widget_set_sensitive (priv->mic_button, FALSE);

      /* Be sure that the mic button is enabled */
      gtk_toggle_tool_button_set_active (
          GTK_TOGGLE_TOOL_BUTTON (priv->mic_button), TRUE);

      if (priv->camera_state == CAMERA_STATE_ON)
        {
          /* Restart the preview with the new pipeline. */
          display_video_preview (self, TRUE);
        }

      /* destroy the video output; it will be recreated when we'll redial */
      disconnect_video_output_motion_handler (self);
      if (priv->video_output != NULL)
        clutter_actor_destroy (priv->video_output);
      priv->video_output = NULL;
      if (priv->got_video_src > 0)
        {
          g_source_remove (priv->got_video_src);
          priv->got_video_src = 0;
        }

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
empathy_call_window_channel_closed_cb (EmpathyCallHandler *handler,
    gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  if (empathy_call_window_disconnected (self, TRUE) &&
      priv->call_state == REDIALING)
      empathy_call_window_restart_call (self);
}

static gboolean
empathy_call_window_sink_removed_cb (EmpathyCallHandler *handler,
    GstPad *sink,
    FsMediaType media_type,
    EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  DEBUG ("removing content");

  /*
   * This assumes that there is only one video stream per channel...
   */

  if ((guint) media_type == FS_MEDIA_TYPE_VIDEO)
    {
      if (priv->funnel != NULL)
        {
          GstElement *output;

          output = priv->video_output_sink;

          gst_element_set_state (output, GST_STATE_NULL);
          gst_element_set_state (priv->funnel, GST_STATE_NULL);

          gst_bin_remove (GST_BIN (priv->pipeline), output);
          gst_bin_remove (GST_BIN (priv->pipeline), priv->funnel);
          priv->funnel = NULL;
          return TRUE;
        }
    }
  else if (media_type == FS_MEDIA_TYPE_AUDIO)
    {
      if (priv->audio_output != NULL)
        {
          gst_element_set_state (priv->audio_output, GST_STATE_NULL);

          gst_bin_remove (GST_BIN (priv->pipeline), priv->audio_output);
          priv->audio_output = NULL;
          return TRUE;
        }
    }

  return FALSE;
}

/* Called with global lock held */
static GstPad *
empathy_call_window_get_video_sink_pad (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  GstElement *output;

  if (priv->funnel == NULL)
    {
      output = priv->video_output_sink;

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
empathy_call_window_get_audio_sink_pad (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  GstPadTemplate *template;

  if (priv->audio_output == NULL)
    {
      priv->audio_output = empathy_audio_sink_new ();
      g_object_ref_sink (priv->audio_output);

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
empathy_call_window_update_timer (gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  const gchar *status;
  gchar *str;
  gdouble time_;

  time_ = g_timer_elapsed (priv->timer, NULL);

  if (priv->call_state == HELD)
    status = _("On hold");
  else if (!gtk_toggle_tool_button_get_active (
      GTK_TOGGLE_TOOL_BUTTON (priv->mic_button)))
    status = _("Mute");
  else
    status = _("Duration");

  /* Translators: 'status - minutes:seconds' the caller has been connected */
  str = g_strdup_printf (_("%s — %d:%02dm"),
      status,
      (int) time_ / 60, (int) time_ % 60);
  empathy_call_window_status_message (self, str);
  g_free (str);

  return TRUE;
}

#if 0
static void
display_error (EmpathyCallWindow *self,
    TpyCallChannel *call,
    const gchar *img,
    const gchar *title,
    const gchar *desc,
    const gchar *details)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
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
  hbox = gtk_hbox_new (FALSE, 3);
  gtk_container_add (GTK_CONTAINER (content_area), hbox);

  /* Add image */
  image = gtk_image_new_from_icon_name (img, GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);

  /* vbox containing the main message and the details expander */
  vbox = gtk_vbox_new (FALSE, 3);
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
media_stream_error_to_txt (EmpathyCallWindow *self,
    TpyCallChannel *call,
    gboolean audio,
    TpMediaStreamError error)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  const gchar *cm = NULL;
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
        tp_connection_parse_object_path (
            tp_channel_borrow_connection (TP_CHANNEL (call)),
            NULL, &cm);

        url = g_strdup_printf ("http://bugs.freedesktop.org/enter_bug.cgi?"
            "product=Telepathy&amp;component=%s", cm);

        result = g_strdup_printf (
            _("Something unexpected happened in a Telepathy component. "
              "Please <a href=\"%s\">report this bug</a> and attach "
              "logs gathered from the 'Debug' window in the Help menu."), url);

        g_free (url);
        g_free (cm);
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
empathy_call_window_stream_error (EmpathyCallWindow *self,
    TpyCallChannel *call,
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
empathy_call_window_audio_stream_error (TpyCallChannel *call,
    guint code,
    const gchar *msg,
    EmpathyCallWindow *self)
{
  empathy_call_window_stream_error (self, call, TRUE, code, msg,
      "gnome-stock-mic", _("Can't establish audio stream"));
}

static void
empathy_call_window_video_stream_error (TpyCallChannel *call,
    guint code,
    const gchar *msg,
    EmpathyCallWindow *self)
{
  empathy_call_window_stream_error (self, call, FALSE, code, msg,
      "camera-web", _("Can't establish video stream"));
}
#endif

static void
empathy_call_window_state_changed_cb (EmpathyCallHandler *handler,
    TpyCallState state,
    EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  TpyCallChannel *call;
  gboolean can_send_video;

  if (state != TPY_CALL_STATE_ACCEPTED)
    return;

  if (priv->call_state == CONNECTED)
    return;

  g_timer_start (priv->timer);
  priv->call_state = CONNECTED;

  empathy_sound_manager_stop (priv->sound_mgr, EMPATHY_SOUND_PHONE_OUTGOING);

  can_send_video = priv->video_input != NULL &&
    empathy_contact_can_voip_video (priv->contact) &&
    empathy_camera_monitor_get_available (priv->camera_monitor);

  g_object_get (priv->handler, "call-channel", &call, NULL);

  if (tpy_call_channel_has_dtmf (call))
    gtk_widget_set_sensitive (priv->dtmf_panel, TRUE);

  if (priv->video_input == NULL)
    empathy_call_window_set_send_video (self, CAMERA_STATE_OFF);

  gtk_widget_set_sensitive (priv->camera_button, can_send_video);

  empathy_call_window_show_hangup_button (self, TRUE);

  gtk_widget_set_sensitive (priv->mic_button, TRUE);

  clutter_actor_hide (priv->video_output);
  gtk_widget_show (priv->remote_user_avatar_widget);

  g_object_unref (call);

  g_mutex_lock (priv->lock);

  priv->timer_id = g_timeout_add_seconds (1,
    empathy_call_window_update_timer, self);

  g_mutex_unlock (priv->lock);

  empathy_call_window_update_timer (self);

  gtk_action_set_sensitive (priv->menu_fullscreen, TRUE);
}

static gboolean
empathy_call_window_show_video_output_cb (gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);

  if (self->priv->video_output != NULL)
    {
      gtk_widget_hide (self->priv->remote_user_avatar_widget);
      clutter_actor_show (self->priv->video_output);
      empathy_call_window_raise_actors (self);
    }

  return FALSE;
}

static gboolean
empathy_call_window_check_video_cb (gpointer data)
{
  EmpathyCallWindow *self = data;

  if (self->priv->got_video)
    {
      self->priv->got_video = FALSE;
      return TRUE;
    }

  /* No video in the last N seconds, display the remote avatar */
  empathy_call_window_show_video_output (self, FALSE);

  return TRUE;
}

/* Called from the streaming thread */
static gboolean
empathy_call_window_video_probe_cb (GstPad *pad,
    GstMiniObject *mini_obj,
    EmpathyCallWindow *self)
{
  /* Ignore events */
  if (GST_IS_EVENT (mini_obj))
    return TRUE;

  if (G_UNLIKELY (!self->priv->got_video))
    {
      /* show the remote video */
      g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
          empathy_call_window_show_video_output_cb,
          g_object_ref (self), g_object_unref);

      self->priv->got_video = TRUE;
    }

  return TRUE;
}

/* Called from the streaming thread */
static gboolean
empathy_call_window_src_added_cb (EmpathyCallHandler *handler,
  GstPad *src, guint media_type, gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  gboolean retval = FALSE;

  GstPad *pad;

  g_mutex_lock (priv->lock);

  switch (media_type)
    {
      case TP_MEDIA_STREAM_TYPE_AUDIO:
        pad = empathy_call_window_get_audio_sink_pad (self);
        break;
      case TP_MEDIA_STREAM_TYPE_VIDEO:
        g_idle_add (empathy_call_window_show_video_output_cb, self);
        pad = empathy_call_window_get_video_sink_pad (self);

        gst_pad_add_data_probe (src,
            G_CALLBACK (empathy_call_window_video_probe_cb), self);
        if (priv->got_video_src > 0)
          g_source_remove (priv->got_video_src);
        priv->got_video_src = g_timeout_add_seconds (5,
            empathy_call_window_check_video_cb, self);
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
empathy_call_window_sink_added_cb (EmpathyCallHandler *handler,
  GstPad *sink, FsMediaType media_type, gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstPad *pad;
  gboolean retval = FALSE;

  switch (media_type)
    {
      case FS_MEDIA_TYPE_AUDIO:
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
      case FS_MEDIA_TYPE_VIDEO:
        if (priv->video_tee != NULL)
          {
            pad = gst_element_get_request_pad (priv->video_tee, "src%d");
            if (GST_PAD_LINK_FAILED (gst_pad_link (pad, sink)))
              {
                g_warning ("Could not link video source input pipeline");
                break;
              }
            gst_object_unref (pad);
          }

        retval = TRUE;
        break;
      default:
        g_assert_not_reached ();
    }

  return retval;
}

static void
empathy_call_window_remove_video_input (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstElement *preview;

  disable_camera (self);

  DEBUG ("remove video input");
  preview = priv->video_preview_sink;

  gst_element_set_state (priv->video_input, GST_STATE_NULL);
  gst_element_set_state (priv->video_tee, GST_STATE_NULL);
  gst_element_set_state (preview, GST_STATE_NULL);

  gst_bin_remove_many (GST_BIN (priv->pipeline), priv->video_input,
    preview, NULL);

  g_object_unref (priv->video_input);
  priv->video_input = NULL;
  g_object_unref (priv->video_tee);
  priv->video_tee = NULL;
  clutter_actor_destroy (priv->video_preview);
  priv->video_preview = NULL;

  gtk_widget_set_sensitive (priv->camera_button, FALSE);
  gtk_widget_set_sensitive (priv->video_effects_button, FALSE);
}

static void
start_call (EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);

  priv->call_started = TRUE;
  empathy_call_handler_start_call (priv->handler,
      gtk_get_current_event_time ());

  if (empathy_call_handler_has_initial_video (priv->handler))
    {
      TpyCallChannel *call;
      TpySendingState s;

      g_object_get (priv->handler, "call-channel", &call, NULL);
      s = tpy_call_channel_get_video_state (call);

      if (s == TPY_SENDING_STATE_PENDING_SEND ||
          s == TPY_SENDING_STATE_SENDING)
        {
          /* Enable 'send video' buttons and display the preview */
          gtk_toggle_tool_button_set_active (
            GTK_TOGGLE_TOOL_BUTTON (priv->camera_button), TRUE);
        }
      else
        {
          gtk_toggle_tool_button_set_active (
            GTK_TOGGLE_TOOL_BUTTON (priv->camera_button), FALSE);

          if (priv->video_preview == NULL)
            {
              create_video_preview (self);
              add_video_preview_to_pipeline (self);
            }
        }

      g_object_unref (call);
    }
}

static gboolean
empathy_call_window_bus_message (GstBus *bus, GstMessage *message,
  gpointer user_data)
{
  EmpathyCallWindow *self = EMPATHY_CALL_WINDOW (user_data);
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GstState newstate;

  empathy_call_handler_bus_message (priv->handler, bus, message);

  switch (GST_MESSAGE_TYPE (message))
    {
      case GST_MESSAGE_STATE_CHANGED:
        if (GST_MESSAGE_SRC (message) == GST_OBJECT (priv->video_input))
          {
            gst_message_parse_state_changed (message, NULL, &newstate, NULL);
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
                empathy_call_window_remove_video_input (self);
              gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
            }
          else
            {
              empathy_call_window_disconnected (self, TRUE);
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
empathy_call_window_members_changed_cb (TpyCallChannel *call,
    GHashTable *members,
    EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  GHashTableIter iter;
  gpointer key, value;
  gboolean held = FALSE;

  g_hash_table_iter_init (&iter, members);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      if (GPOINTER_TO_INT (value) & TPY_CALL_MEMBER_FLAG_HELD)
        {
          /* This assumes this is a 1-1 call, otherwise one participant
           * putting the call on hold wouldn't mean the call is on hold
           * for everyone. */
          held = TRUE;
          break;
        }
    }

  if (held)
    priv->call_state = HELD;
  else if (priv->call_state == HELD)
    priv->call_state = CONNECTED;
}

static void
call_handler_notify_call_cb (EmpathyCallHandler *handler,
    GParamSpec *spec,
    EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  TpyCallChannel *call;

  g_object_get (priv->handler, "call-channel", &call, NULL);
  if (call == NULL)
    return;

/* FIXME
  tp_g_signal_connect_object (call, "audio-stream-error",
      G_CALLBACK (empathy_call_window_audio_stream_error), self, 0);
  tp_g_signal_connect_object (call, "video-stream-error",
      G_CALLBACK (empathy_call_window_video_stream_error), self, 0);
*/

  tp_g_signal_connect_object (call, "members-changed",
      G_CALLBACK (empathy_call_window_members_changed_cb), self, 0);

  g_object_unref (call);
}

static void
empathy_call_window_realized_cb (GtkWidget *widget, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
  TpyCallChannel *call;
  gint width;

  /* Make the hangup button twice as wide */
  width = gtk_widget_get_allocated_width (priv->hangup_button);
  gtk_widget_set_size_request (priv->hangup_button, width * 2, -1);

  g_signal_connect (priv->handler, "state-changed",
    G_CALLBACK (empathy_call_window_state_changed_cb), window);
  g_signal_connect (priv->handler, "conference-added",
    G_CALLBACK (empathy_call_window_conference_added_cb), window);
  g_signal_connect (priv->handler, "conference-removed",
    G_CALLBACK (empathy_call_window_conference_removed_cb), window);
  g_signal_connect (priv->handler, "closed",
    G_CALLBACK (empathy_call_window_channel_closed_cb), window);
  g_signal_connect (priv->handler, "src-pad-added",
    G_CALLBACK (empathy_call_window_src_added_cb), window);
  g_signal_connect (priv->handler, "sink-pad-added",
    G_CALLBACK (empathy_call_window_sink_added_cb), window);
  g_signal_connect (priv->handler, "sink-pad-removed",
    G_CALLBACK (empathy_call_window_sink_removed_cb), window);

  g_object_get (priv->handler, "call-channel", &call, NULL);
  if (call != NULL)
    {
      call_handler_notify_call_cb (priv->handler, NULL, window);
      g_object_unref (call);
    }
  else
    {
      /* call-channel doesn't exist yet, we'll connect signals once it has been
       * set */
      g_signal_connect (priv->handler, "notify::call-channel",
        G_CALLBACK (call_handler_notify_call_cb), window);
    }

  gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
}

static gboolean
empathy_call_window_delete_cb (GtkWidget *widget, GdkEvent*event,
  EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

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
show_controls (EmpathyCallWindow *window, gboolean set_fullscreen)
{
  GtkWidget *menu;
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  menu = gtk_ui_manager_get_widget (priv->ui_manager,
            "/menubar1");

  if (set_fullscreen)
    {
      gtk_widget_hide (priv->dtmf_panel);
      gtk_widget_hide (menu);
      gtk_widget_hide (priv->toolbar);
    }
  else
    {
      if (priv->dialpad_was_visible_before_fs)
        gtk_widget_show (priv->dtmf_panel);

      gtk_widget_show (menu);
      gtk_widget_show (priv->toolbar);

      gtk_window_resize (GTK_WINDOW (window), priv->original_width_before_fs,
          priv->original_height_before_fs);
    }
}

static void
show_borders (EmpathyCallWindow *window, gboolean set_fullscreen)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  gtk_container_set_border_width (GTK_CONTAINER (priv->content_hbox),
      set_fullscreen ? 0 : CONTENT_HBOX_BORDER_WIDTH);
  gtk_box_set_spacing (GTK_BOX (priv->content_hbox),
      set_fullscreen ? 0 : CONTENT_HBOX_SPACING);

  if (priv->video_output != NULL)
    {
#if 0
      gtk_box_set_child_packing (GTK_BOX (priv->content_hbox),
          priv->video_output, TRUE, TRUE,
          set_fullscreen ? 0 : CONTENT_HBOX_CHILDREN_PACKING_PADDING,
          GTK_PACK_START);
#endif
    }
}

static gboolean
empathy_call_window_state_event_cb (GtkWidget *widget,
  GdkEventWindowState *event, EmpathyCallWindow *window)
{
  if (event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
      EmpathyCallWindowPriv *priv = GET_PRIV (window);
      gboolean set_fullscreen = event->new_window_state &
        GDK_WINDOW_STATE_FULLSCREEN;

      if (set_fullscreen)
        {
          gboolean dialpad_was_visible;
          GtkAllocation allocation;
          gint original_width, original_height;

          gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
          original_width = allocation.width;
          original_height = allocation.height;

          g_object_get (priv->dtmf_panel,
              "visible", &dialpad_was_visible,
              NULL);

          priv->dialpad_was_visible_before_fs = dialpad_was_visible;
          priv->original_width_before_fs = original_width;
          priv->original_height_before_fs = original_height;

          if (priv->video_output_motion_handler_id == 0 &&
                priv->video_output != NULL)
            {
              priv->video_output_motion_handler_id = g_signal_connect (
                  G_OBJECT (priv->video_container), "motion-notify-event",
                  G_CALLBACK (empathy_call_window_video_output_motion_notify),
                  window);
            }
        }
      else
        {
          disconnect_video_output_motion_handler (window);
        }

      empathy_call_window_fullscreen_set_fullscreen (priv->fullscreen,
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
empathy_call_window_show_dialpad (EmpathyCallWindow *window,
    gboolean active)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
  int w, h, dialpad_width;
  GtkAllocation allocation;

  gtk_widget_get_allocation (GTK_WIDGET (window), &allocation);
  w = allocation.width;
  h = allocation.height;

  gtk_widget_get_preferred_width (priv->dtmf_panel, &dialpad_width, NULL);

  if (active)
    {
      gtk_widget_show (priv->dtmf_panel);
      w += dialpad_width;
    }
  else
    {
      w -= dialpad_width;
      gtk_widget_hide (priv->dtmf_panel);
    }

  if (w > 0 && h > 0)
    gtk_window_resize (GTK_WINDOW (window), w, h);
}

static void
empathy_call_window_set_send_video (EmpathyCallWindow *window,
  CameraState state)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);
  TpyCallChannel *call;

  priv->sending_video = (state == CAMERA_STATE_ON);

  if (state == CAMERA_STATE_ON)
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

  g_object_get (priv->handler, "call-channel", &call, NULL);
  DEBUG ("%s sending video", priv->sending_video ? "start": "stop");
  tpy_call_channel_send_video (call, priv->sending_video);
  g_object_unref (call);
}

static void
empathy_call_window_mic_toggled_cb (GtkToggleToolButton *toggle,
  EmpathyCallWindow *self)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (self);
  gboolean active;

  active = (gtk_toggle_tool_button_get_active (toggle));

  /* We don't want the settings callback to react to this change to avoid
   * a loop. */
  g_signal_handlers_block_by_func (priv->settings,
      empathy_call_window_prefs_volume_changed_cb, self);

  if (active)
    {
      g_settings_set_double (priv->settings, EMPATHY_PREFS_CALL_SOUND_VOLUME,
          priv->volume * 100);
    }
  else
    {
      /* TODO, Instead of setting the input volume to 0 we should probably
       * stop sending but this would cause the audio call to drop if both
       * sides mute at the same time on certain CMs AFAIK. Need to revisit this
       * in the future. GNOME #574574
       */
      g_settings_set_double (priv->settings, EMPATHY_PREFS_CALL_SOUND_VOLUME,
          0);
    }

    g_signal_handlers_unblock_by_func (priv->settings,
      empathy_call_window_prefs_volume_changed_cb, self);
}

static void
empathy_call_window_hangup_cb (gpointer object,
    EmpathyCallWindow *self)
{
  empathy_call_handler_stop_call (self->priv->handler);

  empathy_call_window_disconnected (self, TRUE);
}

static void
empathy_call_window_restart_call (EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  /* Remove error info bars */
  gtk_container_forall (GTK_CONTAINER (priv->errors_vbox),
      (GtkCallback) gtk_widget_destroy, NULL);

  create_video_output_widget (window);

  /* While the call was disconnected, the input volume might have changed.
   * However, since the audio_input source was destroyed, its volume has not
   * been updated during that time. That's why we manually update it here */
  empathy_call_window_mic_volume_changed (window);

  priv->outgoing = TRUE;
  empathy_call_window_set_state_connecting (window);

  if (priv->pipeline_playing)
    start_call (window);
  else
    /* call will be started when the pipeline is ready */
    priv->start_call_when_playing = TRUE;

  empathy_call_window_setup_avatars (window, priv->handler);

  empathy_call_window_show_hangup_button (window, TRUE);
}

static void
empathy_call_window_dialpad_cb (GtkToggleToolButton *button,
    EmpathyCallWindow *window)
{
  gboolean active;

  active = gtk_toggle_tool_button_get_active (button);

  empathy_call_window_show_dialpad (window, active);
}

static void
empathy_call_window_fullscreen_cb (gpointer object,
                                   EmpathyCallWindow *window)
{
  empathy_call_window_fullscreen_toggle (window);
}

static void
empathy_call_window_fullscreen_toggle (EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen)
    gtk_window_unfullscreen (GTK_WINDOW (window));
  else
    gtk_window_fullscreen (GTK_WINDOW (window));
}

static gboolean
empathy_call_window_video_button_press_cb (GtkWidget *video_preview,
  GdkEventButton *event, EmpathyCallWindow *window)
{
  if (event->button == 3 && event->type == GDK_BUTTON_PRESS)
    {
      empathy_call_window_video_menu_popup (window, event->button);
      return TRUE;
    }

  return FALSE;
}

static gboolean
empathy_call_window_key_press_cb (GtkWidget *video_output,
  GdkEventKey *event, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen && event->keyval == GDK_KEY_Escape)
    {
      /* Since we are in fullscreen mode, toggling will bring us back to
         normal mode. */
      empathy_call_window_fullscreen_toggle (window);
      return TRUE;
    }

  return FALSE;
}

static gboolean
empathy_call_window_video_output_motion_notify (GtkWidget *widget,
    GdkEventMotion *event, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  if (priv->is_fullscreen)
    {
      empathy_call_window_fullscreen_show_popup (priv->fullscreen);

      /* Show the bottom toolbar */
      empathy_call_window_motion_notify_cb (NULL, NULL, window);
      return TRUE;
    }
  return FALSE;
}

static void
empathy_call_window_video_menu_popup (EmpathyCallWindow *window,
  guint button)
{
  GtkWidget *menu;
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  menu = gtk_ui_manager_get_widget (priv->ui_manager,
            "/video-popup");
  gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
      button, gtk_get_current_event_time ());
  gtk_menu_shell_select_first (GTK_MENU_SHELL (menu), FALSE);
}

static void
empathy_call_window_status_message (EmpathyCallWindow *self,
  gchar *message)
{
  gtk_label_set_label (GTK_LABEL (self->priv->status_label), message);
}

static void
empathy_call_window_volume_changed_cb (GtkScaleButton *button,
  gdouble value, EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  if (priv->audio_output == NULL)
    return;

  empathy_audio_sink_set_volume (EMPATHY_GST_AUDIO_SINK (priv->audio_output),
    value);
}

GtkUIManager *
empathy_call_window_get_ui_manager (EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  return priv->ui_manager;
}

EmpathyGstAudioSrc *
empathy_call_window_get_audio_src (EmpathyCallWindow *window)
{
  EmpathyCallWindowPriv *priv = GET_PRIV (window);

  return (EmpathyGstAudioSrc *) priv->audio_input;
}

EmpathyGstVideoSrc *
empathy_call_window_get_video_src (EmpathyCallWindow *self)
{
  return EMPATHY_GST_VIDEO_SRC (self->priv->video_input);
}

/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * GtkAction code based on gnome-terminal's TerminalTabsMenu object.
 * Thanks guys!
 */

#include <config.h>

#include <gtk/gtk.h>

#include <libempathy/empathy-camera-monitor.h>
#include <libempathy/empathy-gsettings.h>

#include "empathy-camera-menu.h"

#define DEBUG_FLAG EMPATHY_DEBUG_VOIP
#include <libempathy/empathy-debug.h>

struct _EmpathyCameraMenuPrivate
{
  /* Borrowed ref; the call window actually owns us. */
  EmpathyCallWindow *window;

  /* Given away ref; the call window's UI manager now owns this. */
  GtkActionGroup *action_group;

  /* An invisible radio action so new cameras are always in the
   * same radio group. */
  GtkAction *anchor_action;

  /* The merge ID used with the UI manager. We need to keep this
   * around so in _clean we can remove all the items we've added
   * before and start again. */
  guint ui_id;

  /* TRUE if we're in _update and so calling _set_active. */
  gboolean in_update;

  /* Queue of GtkRadioActions. */
  GQueue *cameras;

  EmpathyCameraMonitor *camera_monitor;

  GSettings *settings;
};

G_DEFINE_TYPE (EmpathyCameraMenu, empathy_camera_menu, G_TYPE_OBJECT);

enum
{
  PROP_WINDOW = 1,
};

static void empathy_camera_menu_update (EmpathyCameraMenu *self);

static void
empathy_camera_menu_init (EmpathyCameraMenu *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_CAMERA_MENU, EmpathyCameraMenuPrivate);
}

static void
empathy_camera_menu_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyCameraMenu *self = EMPATHY_CAMERA_MENU (object);

  switch (property_id)
    {
      case PROP_WINDOW:
        self->priv->window = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_camera_menu_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyCameraMenu *self = EMPATHY_CAMERA_MENU (object);

  switch (property_id)
    {
      case PROP_WINDOW:
        g_value_set_object (value, self->priv->window);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_camera_menu_clean (EmpathyCameraMenu *self)
{
  GtkUIManager *ui_manager;

  if (self->priv->ui_id == 0)
    return;

  ui_manager = empathy_call_window_get_ui_manager (self->priv->window);

  gtk_ui_manager_remove_ui (ui_manager, self->priv->ui_id);
  gtk_ui_manager_ensure_update (ui_manager);
  self->priv->ui_id = 0;
}

static void
empathy_camera_menu_activate_cb (GtkAction *action,
    EmpathyCameraMenu *self)
{
  EmpathyGstVideoSrc *video;
  const gchar *device;
  gchar *current_device;

  if (self->priv->in_update)
    return;

  video = empathy_call_window_get_video_src (self->priv->window);

  device = gtk_action_get_name (action);
  current_device = empathy_video_src_dup_device (video);

  /* Don't change the device if it's the currently used one */
  if (!tp_strdiff (device, current_device))
    goto out;

  empathy_call_window_change_webcam (self->priv->window, device);

 out:
  g_free (current_device);
}

static void
empathy_camera_menu_update (EmpathyCameraMenu *self)
{
  GList *l;
  GtkUIManager *ui_manager;
  EmpathyGstVideoSrc *video;
  gchar *current_camera;

  ui_manager = empathy_call_window_get_ui_manager (self->priv->window);

  video = empathy_call_window_get_video_src (self->priv->window);
  current_camera = empathy_video_src_dup_device (video);

  empathy_camera_menu_clean (self);
  self->priv->ui_id = gtk_ui_manager_new_merge_id (ui_manager);

  for (l = self->priv->cameras->head; l != NULL; l = g_list_next (l))
    {
      GtkRadioAction *action = l->data;
      const gchar *name = gtk_action_get_name (GTK_ACTION (action));

      if (!tp_strdiff (current_camera, name))
        {
          self->priv->in_update = TRUE;
          gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
          self->priv->in_update = FALSE;
        }

      gtk_ui_manager_add_ui (ui_manager, self->priv->ui_id,
          /* TODO: this should probably be passed from the call
           * window, seeing that it's a reference to
           * empathy-call-window.ui. */
          "/menubar1/edit/menucamera",
          name, name, GTK_UI_MANAGER_MENUITEM, FALSE);
    }

  g_free (current_camera);
}

static void
empathy_camera_menu_add_camera (EmpathyCameraMenu *self,
    EmpathyCamera *camera)
{
  GtkRadioAction *action;
  GSList *group;

  action = gtk_radio_action_new (camera->device, camera->name, NULL, NULL, 0);
  gtk_action_group_add_action (self->priv->action_group, GTK_ACTION (action));

  group = gtk_radio_action_get_group (
      GTK_RADIO_ACTION (self->priv->anchor_action));
  gtk_radio_action_set_group (GTK_RADIO_ACTION (action), group);

  g_queue_push_tail (self->priv->cameras, action);

  g_signal_connect (action, "activate",
      G_CALLBACK (empathy_camera_menu_activate_cb), self);
}

static void
empathy_camera_menu_camera_added_cb (EmpathyCameraMonitor *monitor,
    EmpathyCamera *camera,
    EmpathyCameraMenu *self)
{
  empathy_camera_menu_add_camera (self, camera);
  empathy_camera_menu_update (self);
}

static void
empathy_camera_menu_camera_removed_cb (EmpathyCameraMonitor *monitor,
    EmpathyCamera *camera,
    EmpathyCameraMenu *self)
{
  GList *l;

  for (l = self->priv->cameras->head; l != NULL; l = g_list_next (l))
    {
      GtkAction *action = l->data;
      const gchar *device;

      device = gtk_action_get_name (action);

      if (tp_strdiff (device, camera->device))
        continue;

      g_signal_handlers_disconnect_by_func (action,
          G_CALLBACK (empathy_camera_menu_activate_cb), self);

      gtk_action_group_remove_action (self->priv->action_group,
          action);
      g_queue_remove (self->priv->cameras, action);
      break;
    }

  empathy_camera_menu_update (self);
}

static void
empathy_camera_menu_prefs_camera_changed_cb (GSettings *settings,
    gchar *key,
    EmpathyCameraMenu *self)
{
  gchar *device = g_settings_get_string (settings, key);
  GtkRadioAction *action = NULL;
  gboolean found = FALSE;
  GList *l;

  for (l = self->priv->cameras->head; l != NULL; l = g_list_next (l))
    {
      const gchar *name;

      action = l->data;
      name = gtk_action_get_name (GTK_ACTION (action));

      if (!tp_strdiff (device, name))
        {
          found = TRUE;
          break;
        }
    }

  /* If the selected camera isn't found, we connect the first
   * available one */
  if (!found && self->priv->cameras->head != NULL)
    action = self->priv->cameras->head->data;

  if (action != NULL &&
      !gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)))
    {
      g_signal_handlers_block_by_func (settings,
          empathy_camera_menu_prefs_camera_changed_cb, self);
      gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
      g_signal_handlers_unblock_by_func (settings,
          empathy_camera_menu_prefs_camera_changed_cb, self);
    }

  g_free (device);
}

static void
empathy_camera_menu_get_cameras (EmpathyCameraMenu *self)
{
  const GList *cameras;

  cameras = empathy_camera_monitor_get_cameras (self->priv->camera_monitor);

  for (; cameras != NULL; cameras = g_list_next (cameras))
    {
      EmpathyCamera *camera = cameras->data;

      empathy_camera_menu_add_camera (self, camera);
    }

  empathy_camera_menu_update (self);

  /* Do as if the gsettings key had changed, so we select the key that
   * was last set. */
  empathy_camera_menu_prefs_camera_changed_cb (self->priv->settings,
      EMPATHY_PREFS_CALL_CAMERA_DEVICE, self);
}

static void
empathy_camera_menu_constructed (GObject *obj)
{
  EmpathyCameraMenu *self = EMPATHY_CAMERA_MENU (obj);
  GtkUIManager *ui_manager;

  g_assert (EMPATHY_IS_CALL_WINDOW (self->priv->window));

  ui_manager = empathy_call_window_get_ui_manager (self->priv->window);

  g_assert (GTK_IS_UI_MANAGER (ui_manager));

  /* Okay let's go go go. */

  self->priv->action_group = gtk_action_group_new ("EmpathyCameraMenu");
  gtk_ui_manager_insert_action_group (ui_manager, self->priv->action_group, -1);
  /* the UI manager now owns this */
  g_object_unref (self->priv->action_group);

  self->priv->anchor_action = g_object_new (GTK_TYPE_RADIO_ACTION,
      "name", "EmpathyCameraMenuAnchorAction",
      NULL);
  gtk_action_group_add_action (self->priv->action_group,
      self->priv->anchor_action);
  g_object_unref (self->priv->anchor_action);

  self->priv->camera_monitor = empathy_camera_monitor_new ();

  tp_g_signal_connect_object (self->priv->camera_monitor, "added",
      G_CALLBACK (empathy_camera_menu_camera_added_cb), self, 0);
  tp_g_signal_connect_object (self->priv->camera_monitor, "removed",
      G_CALLBACK (empathy_camera_menu_camera_removed_cb), self, 0);

  self->priv->settings = g_settings_new (EMPATHY_PREFS_CALL_SCHEMA);
  g_signal_connect (self->priv->settings,
      "changed::"EMPATHY_PREFS_CALL_CAMERA_DEVICE,
      G_CALLBACK (empathy_camera_menu_prefs_camera_changed_cb), self);

  self->priv->cameras = g_queue_new ();

  empathy_camera_menu_get_cameras (self);
}

static void
empathy_camera_menu_dispose (GObject *obj)
{
  EmpathyCameraMenu *self = EMPATHY_CAMERA_MENU (obj);

  tp_clear_pointer (&self->priv->cameras, g_queue_free);

  tp_clear_object (&self->priv->camera_monitor);
  tp_clear_object (&self->priv->settings);

  G_OBJECT_CLASS (empathy_camera_menu_parent_class)->dispose (obj);
}

static void
empathy_camera_menu_class_init (EmpathyCameraMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = empathy_camera_menu_set_property;
  object_class->get_property = empathy_camera_menu_get_property;
  object_class->constructed = empathy_camera_menu_constructed;
  object_class->dispose = empathy_camera_menu_dispose;

  g_object_class_install_property (object_class, PROP_WINDOW,
      g_param_spec_object ("window", "window", "window",
          EMPATHY_TYPE_CALL_WINDOW,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (object_class, sizeof (EmpathyCameraMenuPrivate));
}

EmpathyCameraMenu *
empathy_camera_menu_new (EmpathyCallWindow *window)
{
  return g_object_new (EMPATHY_TYPE_CAMERA_MENU,
      "window", window,
      NULL);
}

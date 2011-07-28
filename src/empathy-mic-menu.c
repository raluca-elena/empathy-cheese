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

#include "empathy-mic-menu.h"

struct _EmpathyMicMenuPrivate
{
  /* Borrowed ref; the call window actually owns us. */
  EmpathyCallWindow *window;

  /* Given away ref; the call window's UI manager now owns this. */
  GtkActionGroup *action_group;

  /* An invisible radio action so new microphones are always in the
   * same radio group. */
  GtkAction *anchor_action;

  /* The merge ID used with the UI manager. We need to keep this
   * around so in _clean we can remove all the items we've added
   * before and start again. */
  guint ui_id;

  /* TRUE if we're in _update and so calling _set_active. */
  gboolean in_update;

  /* Queue of GtkRadioActions. */
  GQueue *microphones;
};

G_DEFINE_TYPE (EmpathyMicMenu, empathy_mic_menu, G_TYPE_OBJECT);

enum
{
  PROP_WINDOW = 1,
};

static void empathy_mic_menu_update (EmpathyMicMenu *self);

static void
empathy_mic_menu_init (EmpathyMicMenu *self)
{
  EmpathyMicMenuPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_MIC_MENU, EmpathyMicMenuPrivate);

  self->priv = priv;
}

static void
empathy_mic_menu_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyMicMenu *self = EMPATHY_MIC_MENU (object);
  EmpathyMicMenuPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_WINDOW:
        priv->window = g_value_get_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_mic_menu_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyMicMenu *self = EMPATHY_MIC_MENU (object);
  EmpathyMicMenuPrivate *priv = self->priv;

  switch (property_id)
    {
      case PROP_WINDOW:
        g_value_set_object (value, priv->window);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_mic_menu_clean (EmpathyMicMenu *self)
{
  EmpathyMicMenuPrivate *priv = self->priv;
  GtkUIManager *ui_manager;

  if (priv->ui_id == 0)
    return;

  ui_manager = empathy_call_window_get_ui_manager (priv->window);

  gtk_ui_manager_remove_ui (ui_manager, priv->ui_id);
  gtk_ui_manager_ensure_update (ui_manager);
  priv->ui_id = 0;
}

static void
empathy_mic_menu_change_mic_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyGstAudioSrc *audio = EMPATHY_GST_AUDIO_SRC (source_object);
  EmpathyMicMenu *self = user_data;
  GError *error = NULL;

  if (!empathy_audio_src_change_microphone_finish (audio, result, &error))
    {
      g_debug ("Failed to change microphone: %s", error->message);
      g_clear_error (&error);

      /* We call update here because if this change operation failed
       * and we don't update the menu items, it'll point to the wrong
       * device. We don't want to call it if the change was successful
       * because we'll get the notify::microphone signal fired in a
       * bit and the current value hasn't changed so it'd keep jumping
       * between these values like there's no tomorrow, etc. */
      empathy_mic_menu_update (self);
    }
}

static void
empathy_mic_menu_activate_cb (GtkToggleAction *action,
    EmpathyMicMenu *self)
{
  EmpathyMicMenuPrivate *priv = self->priv;
  EmpathyGstAudioSrc *audio;
  gint value;

  if (priv->in_update)
    return;

  audio = empathy_call_window_get_audio_src (priv->window);

  g_object_get (action, "value", &value, NULL);

  empathy_audio_src_change_microphone_async (audio, value,
      empathy_mic_menu_change_mic_cb, self);
}

static void
empathy_mic_menu_update (EmpathyMicMenu *self)
{
  EmpathyMicMenuPrivate *priv = self->priv;
  GList *l;
  GtkUIManager *ui_manager;
  EmpathyGstAudioSrc *audio;
  guint current_mic;

  ui_manager = empathy_call_window_get_ui_manager (priv->window);

  audio = empathy_call_window_get_audio_src (priv->window);
  current_mic = empathy_audio_src_get_microphone (audio);

  empathy_mic_menu_clean (self);
  priv->ui_id = gtk_ui_manager_new_merge_id (ui_manager);

  for (l = priv->microphones->head; l != NULL; l = l->next)
    {
      GtkRadioAction *action = l->data;
      const gchar *name = gtk_action_get_name (GTK_ACTION (action));
      gint value;

      g_object_get (action, "value", &value, NULL);

      if (value == (gint) current_mic)
        {
          priv->in_update = TRUE;
          gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
          priv->in_update = FALSE;
        }

      gtk_ui_manager_add_ui (ui_manager, priv->ui_id,
          /* TODO: this should probably be passed from the call
           * window, seeing that it's a reference to
           * empathy-call-window.ui. */
          "/menubar1/edit/menumicrophone",
          name, name, GTK_UI_MANAGER_MENUITEM, FALSE);
    }
}

static void
empathy_mic_menu_add_microphone (EmpathyMicMenu *self,
    const gchar *name,
    const gchar *description,
    guint source_idx)
{
  EmpathyMicMenuPrivate *priv = self->priv;
  GtkRadioAction *action;
  GSList *group;

  action = gtk_radio_action_new (name, description, NULL, NULL, source_idx);
  gtk_action_group_add_action_with_accel (priv->action_group,
      GTK_ACTION (action), NULL);

  group = gtk_radio_action_get_group (GTK_RADIO_ACTION (priv->anchor_action));
  gtk_radio_action_set_group (GTK_RADIO_ACTION (action), group);

  g_queue_push_tail (priv->microphones, action);

  g_signal_connect (action, "activate",
      G_CALLBACK (empathy_mic_menu_activate_cb), self);
}

static void
empathy_mic_menu_notify_microphone_cb (EmpathyGstAudioSrc *audio,
    GParamSpec *pspec,
    EmpathyMicMenu *self)
{
  empathy_mic_menu_update (self);
}

static void
empathy_mic_menu_microphone_added_cb (EmpathyGstAudioSrc *audio,
    guint source_idx,
    const gchar *name,
    const gchar *description,
    EmpathyMicMenu *self)
{
  empathy_mic_menu_add_microphone (self, name, description, source_idx);

  empathy_mic_menu_update (self);
}

static void
empathy_mic_menu_microphone_removed_cb (EmpathyGstAudioSrc *audio,
    guint source_idx,
    EmpathyMicMenu *self)
{
  EmpathyMicMenuPrivate *priv = self->priv;
  GList *l;

  for (l = priv->microphones->head; l != NULL; l = l->next)
    {
      GtkRadioAction *action = l->data;
      gint value;

      g_object_get (action, "value", &value, NULL);

      if (value != (gint) source_idx)
        {
          action = NULL;
          continue;
        }

      g_signal_handlers_disconnect_by_func (action,
          G_CALLBACK (empathy_mic_menu_activate_cb), self);

      gtk_action_group_remove_action (priv->action_group, GTK_ACTION (action));
      g_queue_remove (priv->microphones, action);
      break;
    }

  empathy_mic_menu_update (self);
}

static void
empathy_mic_menu_get_microphones_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyGstAudioSrc *audio = EMPATHY_GST_AUDIO_SRC (source_object);
  EmpathyMicMenu *self = user_data;
  GError *error = NULL;
  const GList *mics = NULL;

  mics = empathy_audio_src_get_microphones_finish (audio, result, &error);

  if (error != NULL)
    {
      g_debug ("Failed to get microphone list: %s", error->message);
      g_clear_error (&error);
      return;
    }

  for (; mics != NULL; mics = mics->next)
    {
      EmpathyAudioSrcMicrophone *mic = mics->data;

      empathy_mic_menu_add_microphone (self, mic->name,
          mic->description, mic->index);
    }

  empathy_mic_menu_update (self);
}

static void
empathy_mic_menu_constructed (GObject *obj)
{
  EmpathyMicMenu *self = EMPATHY_MIC_MENU (obj);
  EmpathyMicMenuPrivate *priv = self->priv;
  GtkUIManager *ui_manager;
  EmpathyGstAudioSrc *audio;

  g_assert (EMPATHY_IS_CALL_WINDOW (priv->window));

  ui_manager = empathy_call_window_get_ui_manager (priv->window);
  audio = empathy_call_window_get_audio_src (priv->window);

  g_assert (GTK_IS_UI_MANAGER (ui_manager));
  g_assert (EMPATHY_IS_GST_AUDIO_SRC (audio));

  /* Okay let's go go go. */

  priv->action_group = gtk_action_group_new ("EmpathyMicMenu");
  gtk_ui_manager_insert_action_group (ui_manager, priv->action_group, -1);
  /* the UI manager now owns this */
  g_object_unref (priv->action_group);

  priv->anchor_action = g_object_new (GTK_TYPE_RADIO_ACTION,
      "name", "EmpathyMicMenuAnchorAction",
      NULL);
  gtk_action_group_add_action (priv->action_group, priv->anchor_action);
  g_object_unref (priv->anchor_action);

  tp_g_signal_connect_object (audio, "notify::microphone",
      G_CALLBACK (empathy_mic_menu_notify_microphone_cb), self, 0);
  tp_g_signal_connect_object (audio, "microphone-added",
      G_CALLBACK (empathy_mic_menu_microphone_added_cb), self, 0);
  tp_g_signal_connect_object (audio, "microphone-removed",
      G_CALLBACK (empathy_mic_menu_microphone_removed_cb), self, 0);

  priv->microphones = g_queue_new ();

  empathy_audio_src_get_microphones_async (audio,
      empathy_mic_menu_get_microphones_cb, self);
}

static void
empathy_mic_menu_dispose (GObject *obj)
{
  EmpathyMicMenu *self = EMPATHY_MIC_MENU (obj);
  EmpathyMicMenuPrivate *priv = self->priv;

  if (priv->microphones != NULL)
    g_queue_free (priv->microphones);
  priv->microphones = NULL;

  G_OBJECT_CLASS (empathy_mic_menu_parent_class)->dispose (obj);
}

static void
empathy_mic_menu_class_init (EmpathyMicMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = empathy_mic_menu_set_property;
  object_class->get_property = empathy_mic_menu_get_property;
  object_class->constructed = empathy_mic_menu_constructed;
  object_class->dispose = empathy_mic_menu_dispose;

  g_object_class_install_property (object_class, PROP_WINDOW,
      g_param_spec_object ("window", "window", "window",
          EMPATHY_TYPE_CALL_WINDOW,
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (object_class, sizeof (EmpathyMicMenuPrivate));
}

EmpathyMicMenu *
empathy_mic_menu_new (EmpathyCallWindow *window)
{
  return g_object_new (EMPATHY_TYPE_MIC_MENU,
      "window", window,
      NULL);
}

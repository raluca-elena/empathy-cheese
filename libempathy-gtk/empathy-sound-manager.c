/*
 * empathy-sound-manager.c - Various sound related utility functions.
 * Copyright (C) 2009 Collabora Ltd.
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

#include <config.h>

#include "empathy-sound-manager.h"

#include <canberra-gtk.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-utils.h>

typedef struct {
  EmpathySound sound_id;
  const char * event_ca_id;
  const char * event_ca_description;
  const char * key;
} EmpathySoundEntry;

typedef struct {
  GtkWidget *widget;
  gint sound_id;
  guint play_interval;
  guint replay_timeout_id;
  EmpathySoundManager *self;
} EmpathyRepeatableSound;

/* NOTE: these entries MUST be in the same order than EmpathySound enum */
static EmpathySoundEntry sound_entries[LAST_EMPATHY_SOUND] = {
  { EMPATHY_SOUND_MESSAGE_INCOMING, "message-new-instant",
    N_("Received an instant message"), EMPATHY_PREFS_SOUNDS_INCOMING_MESSAGE } ,
  { EMPATHY_SOUND_MESSAGE_OUTGOING, "message-sent-instant",
    N_("Sent an instant message"), EMPATHY_PREFS_SOUNDS_OUTGOING_MESSAGE } ,
  { EMPATHY_SOUND_CONVERSATION_NEW, "message-new-instant",
    N_("Incoming chat request"), EMPATHY_PREFS_SOUNDS_NEW_CONVERSATION },
  { EMPATHY_SOUND_CONTACT_CONNECTED, "service-login",
    N_("Contact connected"), EMPATHY_PREFS_SOUNDS_CONTACT_LOGIN },
  { EMPATHY_SOUND_CONTACT_DISCONNECTED, "service-logout",
    N_("Contact disconnected"), EMPATHY_PREFS_SOUNDS_CONTACT_LOGOUT },
  { EMPATHY_SOUND_ACCOUNT_CONNECTED, "service-login",
    N_("Connected to server"), EMPATHY_PREFS_SOUNDS_SERVICE_LOGIN },
  { EMPATHY_SOUND_ACCOUNT_DISCONNECTED, "service-logout",
    N_("Disconnected from server"), EMPATHY_PREFS_SOUNDS_SERVICE_LOGOUT },
  { EMPATHY_SOUND_PHONE_INCOMING, "phone-incoming-call",
    N_("Incoming voice call"), NULL },
  { EMPATHY_SOUND_PHONE_OUTGOING, "phone-outgoing-calling",
    N_("Outgoing voice call"), NULL },
  { EMPATHY_SOUND_PHONE_HANGUP, "phone-hangup",
    N_("Voice call ended"), NULL },
};

G_DEFINE_TYPE (EmpathySoundManager, empathy_sound_manager, G_TYPE_OBJECT)

struct _EmpathySoundManagerPrivate
{
  /* An hash table containing currently repeating sounds. The format is the
   * following:
   * Key: An EmpathySound
   * Value : The EmpathyRepeatableSound associated with that EmpathySound. */
  GHashTable *repeating_sounds;
  GSettings *gsettings_sound;
};

static void
empathy_sound_manager_dispose (GObject *object)
{
  EmpathySoundManager *self = (EmpathySoundManager *) object;

  tp_clear_pointer (&self->priv->repeating_sounds, g_hash_table_unref);
  tp_clear_object (&self->priv->gsettings_sound);

  G_OBJECT_CLASS (empathy_sound_manager_parent_class)->dispose (object);
}

static void
empathy_sound_manager_class_init (EmpathySoundManagerClass *cls)
{
  GObjectClass *object_class = G_OBJECT_CLASS (cls);

  object_class->dispose = empathy_sound_manager_dispose;

  g_type_class_add_private (cls, sizeof (EmpathySoundManagerPrivate));
}

static void
empathy_sound_widget_destroyed_cb (GtkWidget *widget,
    gpointer user_data)
{
  EmpathyRepeatableSound *repeatable_sound = user_data;

  /* The sound must be stopped... If it is waiting for replay, remove
   * it from hash table to cancel. Otherwise playing_finished_cb will be
   * called with an error. */
  if (repeatable_sound->replay_timeout_id != 0)
    {
      g_hash_table_remove (repeatable_sound->self->priv->repeating_sounds,
          GINT_TO_POINTER (repeatable_sound->sound_id));
    }
}

static void
repeating_sounds_item_delete (gpointer data)
{
  EmpathyRepeatableSound *repeatable_sound = data;

  if (repeatable_sound->replay_timeout_id != 0)
    g_source_remove (repeatable_sound->replay_timeout_id);

  g_signal_handlers_disconnect_by_func (repeatable_sound->widget,
      empathy_sound_widget_destroyed_cb, repeatable_sound);

  g_object_unref (repeatable_sound->self);

  g_slice_free (EmpathyRepeatableSound, repeatable_sound);
}

static void
empathy_sound_manager_init (EmpathySoundManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_SOUND_MANAGER, EmpathySoundManagerPrivate);

  self->priv->repeating_sounds = g_hash_table_new_full (NULL, NULL,
      NULL, repeating_sounds_item_delete);

  self->priv->gsettings_sound = g_settings_new (EMPATHY_PREFS_SOUNDS_SCHEMA);
}

EmpathySoundManager *
empathy_sound_manager_dup_singleton (void)
{
  static EmpathySoundManager *manager = NULL;

  if (manager != NULL)
      return g_object_ref (manager);

  manager = g_object_new (EMPATHY_TYPE_SOUND_MANAGER, NULL);

  g_object_add_weak_pointer (G_OBJECT (manager), (gpointer *) &manager);
  return manager;
}

static gboolean
empathy_sound_pref_is_enabled (EmpathySoundManager *self,
    EmpathySound sound_id)
{
  EmpathySoundEntry *entry;

  entry = &(sound_entries[sound_id]);
  g_return_val_if_fail (entry->sound_id == sound_id, FALSE);

  if (entry->key == NULL)
    return TRUE;

  if (! g_settings_get_boolean (self->priv->gsettings_sound,
      EMPATHY_PREFS_SOUNDS_ENABLED))
    return FALSE;

  if (!empathy_check_available_state ())
    {
      if (g_settings_get_boolean (self->priv->gsettings_sound,
            EMPATHY_PREFS_SOUNDS_DISABLED_AWAY))
        return FALSE;
    }

  return g_settings_get_boolean (self->priv->gsettings_sound, entry->key);
}

/**
 * empathy_sound_manager_stop:
 * @self: a #EmpathySoundManager
 * @sound_id: The #EmpathySound to stop playing.
 *
 * Stop playing a sound. If it has been stated in loop with
 * empathy_sound_start_playing(), it will also stop replaying.
 */
void
empathy_sound_manager_stop (EmpathySoundManager *self,
    EmpathySound sound_id)
{
  EmpathySoundEntry *entry;
  EmpathyRepeatableSound *repeatable_sound;

  g_return_if_fail (sound_id < LAST_EMPATHY_SOUND);

  entry = &(sound_entries[sound_id]);
  g_return_if_fail (entry->sound_id == sound_id);

  repeatable_sound = g_hash_table_lookup (self->priv->repeating_sounds,
      GINT_TO_POINTER (sound_id));
  if (repeatable_sound != NULL)
    {
      /* The sound must be stopped... If it is waiting for replay, remove
       * it from hash table to cancel. Otherwise we'll cancel the sound
       * being played. */
      if (repeatable_sound->replay_timeout_id != 0)
        {
          g_hash_table_remove (self->priv->repeating_sounds,
              GINT_TO_POINTER (sound_id));
          return;
        }
    }

  ca_context_cancel (ca_gtk_context_get (), entry->sound_id);
}

static gboolean
empathy_sound_play_internal (GtkWidget *widget, EmpathySound sound_id,
  ca_finish_callback_t callback, gpointer user_data)
{
  EmpathySoundEntry *entry;
  ca_context *c;
  ca_proplist *p = NULL;

  entry = &(sound_entries[sound_id]);
  g_return_val_if_fail (entry->sound_id == sound_id, FALSE);

  c = ca_gtk_context_get ();
  ca_context_cancel (c, entry->sound_id);

  DEBUG ("Play sound \"%s\" (%s)",
         entry->event_ca_id,
         entry->event_ca_description);

  if (ca_proplist_create (&p) < 0)
    goto failed;

  if (ca_proplist_sets (p, CA_PROP_EVENT_ID, entry->event_ca_id) < 0)
    goto failed;

  if (ca_proplist_sets (p, CA_PROP_EVENT_DESCRIPTION,
          gettext (entry->event_ca_description)) < 0)
    goto failed;

  if (ca_gtk_proplist_set_for_widget (p, widget) < 0)
    goto failed;

  ca_context_play_full (ca_gtk_context_get (), entry->sound_id, p, callback,
      user_data);

  ca_proplist_destroy (p);

  return TRUE;

failed:
  if (p != NULL)
    ca_proplist_destroy (p);

  return FALSE;
}

/**
 * empathy_sound_manager_play_full:
 * @self: a #EmpathySoundManager
 * @widget: The #GtkWidget from which the sound is originating.
 * @sound_id: The #EmpathySound to play.
 * @callback: The #ca_finish_callback_t function that will be called when the
 *            sound  has stopped playing.
 * @user_data: user data to pass to the function.
 *
 * Plays a sound.
 *
 * Returns %TRUE if the sound has successfully started playing, otherwise
 * returning %FALSE and @callback won't be called.
 *
 * This function returns %FALSE if the sound is already playing in loop using
 * %empathy_sound_start_playing.
 *
 * This function returns %FALSE if the sound is disabled in empathy preferences.
 *
 * Return value: %TRUE if the sound has successfully started playing, %FALSE
 *               otherwise.
 */
gboolean
empathy_sound_manager_play_full (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id,
    ca_finish_callback_t callback,
    gpointer user_data)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (sound_id < LAST_EMPATHY_SOUND, FALSE);

  if (!empathy_sound_pref_is_enabled (self, sound_id))
    return FALSE;

  /* The sound might already be playing repeatedly. If it's the case, we
   * immediadely return since there's no need to make it play again */
  if (g_hash_table_lookup (self->priv->repeating_sounds,
        GINT_TO_POINTER (sound_id)) != NULL)
    return FALSE;

  return empathy_sound_play_internal (widget, sound_id, callback, user_data);
}

/**
 * empathy_sound_manager_play:
 * @self: a #EmpathySoundManager
 * @widget: The #GtkWidget from which the sound is originating.
 * @sound_id: The #EmpathySound to play.
 *
 * Plays a sound. See %empathy_sound_manager_play_full for details.'
 *
 * Return value: %TRUE if the sound has successfully started playing, %FALSE
 *               otherwise.
 */
gboolean
empathy_sound_manager_play (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id)
{
  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (sound_id < LAST_EMPATHY_SOUND, FALSE);

  return empathy_sound_manager_play_full (self, widget, sound_id, NULL, NULL);
}

static void playing_finished_cb (ca_context *c, guint id, int error_code,
  gpointer user_data);

static gboolean
playing_timeout_cb (gpointer data)
{
  EmpathyRepeatableSound *repeatable_sound = data;
  gboolean playing;

  repeatable_sound->replay_timeout_id = 0;

  playing = empathy_sound_play_internal (repeatable_sound->widget,
      repeatable_sound->sound_id, playing_finished_cb, data);

  if (!playing)
    {
      DEBUG ("Failed to replay sound, stop repeating");
      g_hash_table_remove (repeatable_sound->self->priv->repeating_sounds,
          GINT_TO_POINTER (repeatable_sound->sound_id));
    }

  return FALSE;
}

static void
playing_finished_cb (ca_context *c, guint id, int error_code,
  gpointer user_data)
{
  EmpathyRepeatableSound *repeatable_sound = user_data;

  if (error_code != CA_SUCCESS)
    {
      DEBUG ("Error: %s", ca_strerror (error_code));
      g_hash_table_remove (repeatable_sound->self->priv->repeating_sounds,
          GINT_TO_POINTER (repeatable_sound->sound_id));
      return;
    }

  repeatable_sound->replay_timeout_id = g_timeout_add (
      repeatable_sound->play_interval, playing_timeout_cb, user_data);
}

/**
 * empathy_sound_manager_start_playing:
 * @self: a #EmpathySoundManager
 * @widget: The #GtkWidget from which the sound is originating.
 * @sound_id: The #EmpathySound to play.
 * @timeout_before_replay: The amount of time, in milliseconds, between two
 *                         consecutive play.
 *
 * Start playing a sound in loop. To stop the sound, call empathy_call_stop ()
 * by passing it the same @sound_id. Note that if you start playing a sound
 * multiple times, you'll have to call %empathy_sound_stop the same number of
 * times.
 *
 * Return value: %TRUE if the sound has successfully started playing.
 */
gboolean
empathy_sound_manager_start_playing (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id,
    guint timeout_before_replay)
{
  EmpathyRepeatableSound *repeatable_sound;
  gboolean playing = FALSE;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
  g_return_val_if_fail (sound_id < LAST_EMPATHY_SOUND, FALSE);

  if (!empathy_sound_pref_is_enabled (self, sound_id))
    return FALSE;

  if (g_hash_table_lookup (self->priv->repeating_sounds,
               GINT_TO_POINTER (sound_id)) != NULL)
    {
      /* The sound is already playing in loop. No need to continue. */
      return FALSE;
    }

  repeatable_sound = g_slice_new0 (EmpathyRepeatableSound);
  repeatable_sound->widget = widget;
  repeatable_sound->sound_id = sound_id;
  repeatable_sound->play_interval = timeout_before_replay;
  repeatable_sound->replay_timeout_id = 0;
  repeatable_sound->self = g_object_ref (self);

  g_hash_table_insert (self->priv->repeating_sounds, GINT_TO_POINTER (sound_id),
      repeatable_sound);

  g_signal_connect (G_OBJECT (widget), "destroy",
      G_CALLBACK (empathy_sound_widget_destroyed_cb),
      repeatable_sound);

  playing = empathy_sound_play_internal (widget, sound_id, playing_finished_cb,
        repeatable_sound);

  if (!playing)
      g_hash_table_remove (self->priv->repeating_sounds,
          GINT_TO_POINTER (sound_id));

  return playing;
}

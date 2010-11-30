/*
 * empathy-sound-manager.h - Various sound related utility functions.
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


#ifndef __EMPATHY_SOUND_MANAGER_H__
#define __EMPATHY_SOUND_MANAGER_H__

#include <gtk/gtk.h>

#include <canberra-gtk.h>

G_BEGIN_DECLS

/* NOTE: Keep this sync with sound_entries in empathy-sound-manager.c */
typedef enum {
  EMPATHY_SOUND_MESSAGE_INCOMING = 0,
  EMPATHY_SOUND_MESSAGE_OUTGOING,
  EMPATHY_SOUND_CONVERSATION_NEW,
  EMPATHY_SOUND_CONTACT_CONNECTED,
  EMPATHY_SOUND_CONTACT_DISCONNECTED,
  EMPATHY_SOUND_ACCOUNT_CONNECTED,
  EMPATHY_SOUND_ACCOUNT_DISCONNECTED,
  EMPATHY_SOUND_PHONE_INCOMING,
  EMPATHY_SOUND_PHONE_OUTGOING,
  EMPATHY_SOUND_PHONE_HANGUP,
  LAST_EMPATHY_SOUND,
} EmpathySound;

#define EMPATHY_TYPE_SOUND_MANAGER         (empathy_sound_manager_get_type ())
#define EMPATHY_SOUND_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_SOUND_MANAGER, EmpathySoundManager))
#define EMPATHY_SOUND_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_SOUND_MANAGER, EmpathySoundManagerClass))
#define EMPATHY_IS_SOUND_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_SOUND_MANAGER))
#define EMPATHY_IS_SOUND_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_SOUND_MANAGER))
#define EMPATHY_SOUND_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_SOUND_MANAGER, EmpathySoundManagerClass))

typedef struct _EmpathySoundManager EmpathySoundManager;
typedef struct _EmpathySoundManagerClass EmpathySoundManagerClass;
typedef struct _EmpathySoundManagerPrivate EmpathySoundManagerPrivate;

struct _EmpathySoundManager
{
  GObject parent;
  EmpathySoundManagerPrivate *priv;
};

struct _EmpathySoundManagerClass
{
  GObjectClass parent_class;
};

GType empathy_sound_manager_get_type (void) G_GNUC_CONST;

EmpathySoundManager * empathy_sound_manager_dup_singleton (void);

gboolean empathy_sound_manager_play (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id);

void empathy_sound_manager_stop (EmpathySoundManager *self,
    EmpathySound sound_id);

gboolean empathy_sound_manager_start_playing (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id,
    guint timeout_before_replay);

gboolean empathy_sound_manager_play_full (EmpathySoundManager *self,
    GtkWidget *widget,
    EmpathySound sound_id,
    ca_finish_callback_t callback,
    gpointer user_data);

G_END_DECLS

#endif /* #ifndef __EMPATHY_SOUND_MANAGER_H__ */

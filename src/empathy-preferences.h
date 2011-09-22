/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003-2007 Imendio AB
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA  02110-1301  USA
 *
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_PREFERENCES_H__
#define __EMPATHY_PREFERENCES_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_PREFERENCES         (empathy_preferences_get_type ())
#define EMPATHY_PREFERENCES(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_PREFERENCES, EmpathyPreferences))
#define EMPATHY_PREFERENCES_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_PREFERENCES, EmpathyPreferencesClass))
#define EMPATHY_IS_PREFERENCES(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_PREFERENCES))
#define EMPATHY_IS_PREFERENCES_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_PREFERENCES))
#define EMPATHY_PREFERENCES_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_PREFERENCES, EmpathyPreferencesClass))

typedef struct _EmpathyPreferences EmpathyPreferences;
typedef struct _EmpathyPreferencesClass EmpathyPreferencesClass;
typedef struct _EmpathyPreferencesPriv EmpathyPreferencesPriv;

struct _EmpathyPreferences {
	GtkDialog parent;
	gpointer priv;
};

struct _EmpathyPreferencesClass {
	GtkDialogClass parent_class;
};

/* Keep this enum and the array in empathy-preferences.c in sync */
typedef enum
{
  EMPATHY_PREFERENCES_TAB_GENERAL,
  EMPATHY_PREFERENCES_TAB_NOTIFICATIONS,
  EMPATHY_PREFERENCES_TAB_SOUNDS,
  EMPATHY_PREFERENCES_TAB_CALLS,
  EMPATHY_PREFERENCES_TAB_LOCATION,
  EMPATHY_PREFERENCES_TAB_SPELL,
  EMPATHY_PREFERENCES_TAB_THEMES,
} EmpathyPreferencesTab;

GType empathy_preferences_get_type (void);

GtkWidget *empathy_preferences_new (GtkWindow *parent,
                                    gboolean  shell_running);

void empathy_preferences_show_tab (EmpathyPreferences *self,
    const gchar *tab);

const gchar *
empathy_preferences_tab_to_string (EmpathyPreferencesTab tab);

G_END_DECLS

#endif /* __EMPATHY_PREFERENCES_H__ */

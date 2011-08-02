/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_MAIN_WINDOW_H__
#define __EMPATHY_MAIN_WINDOW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_MAIN_WINDOW         (empathy_main_window_get_type ())
#define EMPATHY_MAIN_WINDOW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_MAIN_WINDOW, EmpathyMainWindow))
#define EMPATHY_MAIN_WINDOW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_MAIN_WINDOW, EmpathyMainWindowClass))
#define EMPATHY_IS_MAIN_WINDOW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_MAIN_WINDOW))
#define EMPATHY_IS_MAIN_WINDOW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_MAIN_WINDOW))
#define EMPATHY_MAIN_WINDOW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_MAIN_WINDOW, EmpathyMainWindowClass))

typedef struct _EmpathyMainWindow EmpathyMainWindow;
typedef struct _EmpathyMainWindowClass EmpathyMainWindowClass;
typedef struct _EmpathyMainWindowPriv EmpathyMainWindowPriv;

struct _EmpathyMainWindow {
	GtkWindow parent;
	gpointer priv;
};

struct _EmpathyMainWindowClass {
	GtkWindowClass parent_class;
};

GType empathy_main_window_get_type (void);

GtkWidget *empathy_main_window_dup (void);

void empathy_main_window_show_preferences (EmpathyMainWindow *window);

G_END_DECLS

#endif /* __EMPATHY_MAIN_WINDOW_H__ */

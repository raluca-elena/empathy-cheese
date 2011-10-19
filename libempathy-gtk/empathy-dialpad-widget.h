/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_DIALPAD_WIDGET_H__
#define __EMPATHY_DIALPAD_WIDGET_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_DIALPAD_WIDGET	(empathy_dialpad_widget_get_type ())
#define EMPATHY_DIALPAD_WIDGET(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_DIALPAD_WIDGET, EmpathyDialpadWidget))
#define EMPATHY_DIALPAD_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_DIALPAD_WIDGET, EmpathyDialpadWidgetClass))
#define EMPATHY_IS_DIALPAD_WIDGET(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_DIALPAD_WIDGET))
#define EMPATHY_IS_DIALPAD_WIDGET_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_DIALPAD_WIDGET))
#define EMPATHY_DIALPAD_WIDGET_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_DIALPAD_WIDGET, EmpathyDialpadWidgetClass))

typedef struct _EmpathyDialpadWidget EmpathyDialpadWidget;
typedef struct _EmpathyDialpadWidgetClass EmpathyDialpadWidgetClass;
typedef struct _EmpathyDialpadWidgetPrivate EmpathyDialpadWidgetPrivate;

struct _EmpathyDialpadWidget
{
  GtkBox parent;

  EmpathyDialpadWidgetPrivate *priv;
};

struct _EmpathyDialpadWidgetClass
{
  GtkBoxClass parent_class;
};

GType empathy_dialpad_widget_get_type (void);
GtkWidget *empathy_dialpad_widget_new (void);

G_END_DECLS

#endif

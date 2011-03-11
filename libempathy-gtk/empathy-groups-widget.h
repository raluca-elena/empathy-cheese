/*
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 */

#ifndef __EMPATHY_GROUPS_WIDGET_H__
#define __EMPATHY_GROUPS_WIDGET_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_GROUPS_WIDGET (empathy_groups_widget_get_type ())
#define EMPATHY_GROUPS_WIDGET(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_GROUPS_WIDGET, EmpathyGroupsWidget))
#define EMPATHY_GROUPS_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_GROUPS_WIDGET, EmpathyGroupsWidgetClass))
#define EMPATHY_IS_GROUPS_WIDGET(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_GROUPS_WIDGET))
#define EMPATHY_IS_GROUPS_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_GROUPS_WIDGET))
#define EMPATHY_GROUPS_WIDGET_GET_CLASS(o) ( \
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_GROUPS_WIDGET, \
        EmpathyGroupsWidgetClass))

typedef struct {
	GtkBox parent;

	/*<private>*/
	gpointer priv;
} EmpathyGroupsWidget;

typedef struct {
	GtkBoxClass parent_class;
} EmpathyGroupsWidgetClass;

GType empathy_groups_widget_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_groups_widget_new (FolksGroupDetails *group_details);

FolksGroupDetails * empathy_groups_widget_get_group_details (
    EmpathyGroupsWidget *self);
void empathy_groups_widget_set_group_details (EmpathyGroupsWidget *self,
    FolksGroupDetails *group_details);

G_END_DECLS

#endif /*  __EMPATHY_GROUPS_WIDGET_H__ */

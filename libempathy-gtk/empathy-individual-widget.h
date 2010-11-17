/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_WIDGET_H__
#define __EMPATHY_INDIVIDUAL_WIDGET_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

#include "empathy-contact-widget.h"

G_BEGIN_DECLS

/**
 * EmpathyIndividualWidgetFlags:
 * @EMPATHY_INDIVIDUAL_WIDGET_NONE: Don't show any widgets to edit any
 * details of the individual, or to display any extended details of the
 * individual. This should be the option for widgets that merely display basic
 * information about an individual.
 * @EMPATHY_INDIVIDUAL_WIDGET_EDIT_ALIAS: Show a #GtkEntry allowing changes to
 * the individual's alias.
 * @EMPATHY_INDIVIDUAL_WIDGET_EDIT_FAVOURITE: Show a checkbutton
 * allowing changes to the individual's favourite status.
 * @EMPATHY_INDIVIDUAL_WIDGET_EDIT_GROUPS: Show a widget to change the groups
 * the individual is in.
 * @EMPATHY_INDIVIDUAL_WIDGET_FOR_TOOLTIP: Make widgets more designed for a
 * tooltip. For example, make labels not selectable.
 * @EMPATHY_INDIVIDUAL_WIDGET_SHOW_LOCATION: Display the individual's current
 * location textually or using a map (if compiled with libchamplain support).
 * @EMPATHY_INDIVIDUAL_WIDGET_SHOW_DETAILS: Fetch and display extended vCard
 * details of the individual, if the individual has them.
 * @EMPATHY_INDIVIDUAL_WIDGET_SHOW_PERSONAS: List the #FolksPersona<!-- -->s
 * contained in the individual.
 *
 * Flags used when creating an #EmpathyIndividualWidget to specify which
 * features should be available.
 */
typedef enum
{
  EMPATHY_INDIVIDUAL_WIDGET_NONE = 0,
  EMPATHY_INDIVIDUAL_WIDGET_EDIT_ALIAS   = 1 << 0,
  EMPATHY_INDIVIDUAL_WIDGET_EDIT_FAVOURITE = 1 << 1,
  EMPATHY_INDIVIDUAL_WIDGET_EDIT_GROUPS  = 1 << 2,
  EMPATHY_INDIVIDUAL_WIDGET_FOR_TOOLTIP  = 1 << 3,
  EMPATHY_INDIVIDUAL_WIDGET_SHOW_LOCATION  = 1 << 4,
  EMPATHY_INDIVIDUAL_WIDGET_SHOW_DETAILS = 1 << 5,
  EMPATHY_INDIVIDUAL_WIDGET_SHOW_PERSONAS = 1 << 6,
  EMPATHY_INDIVIDUAL_WIDGET_SHOW_CLIENT_TYPES = 1 << 7,
} EmpathyIndividualWidgetFlags;

#define EMPATHY_TYPE_INDIVIDUAL_WIDGET (empathy_individual_widget_get_type ())
#define EMPATHY_INDIVIDUAL_WIDGET(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_INDIVIDUAL_WIDGET, EmpathyIndividualWidget))
#define EMPATHY_INDIVIDUAL_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_INDIVIDUAL_WIDGET, EmpathyIndividualWidgetClass))
#define EMPATHY_IS_INDIVIDUAL_WIDGET(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_INDIVIDUAL_WIDGET))
#define EMPATHY_IS_INDIVIDUAL_WIDGET_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_INDIVIDUAL_WIDGET))
#define EMPATHY_INDIVIDUAL_WIDGET_GET_CLASS(o) ( \
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_WIDGET, \
        EmpathyIndividualWidgetClass))

typedef struct {
	GtkBox parent;

	/*<private>*/
	gpointer priv;
} EmpathyIndividualWidget;

typedef struct {
	GtkBoxClass parent_class;
} EmpathyIndividualWidgetClass;

GType empathy_individual_widget_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_individual_widget_new (FolksIndividual *individual,
    EmpathyIndividualWidgetFlags flags);

FolksIndividual * empathy_individual_widget_get_individual (
    EmpathyIndividualWidget *self);
void empathy_individual_widget_set_individual (EmpathyIndividualWidget *self,
    FolksIndividual *individual);

G_END_DECLS

#endif /*  __EMPATHY_INDIVIDUAL_WIDGET_H__ */

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_LINKER_H__
#define __EMPATHY_INDIVIDUAL_LINKER_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_INDIVIDUAL_LINKER (empathy_individual_linker_get_type ())
#define EMPATHY_INDIVIDUAL_LINKER(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_INDIVIDUAL_LINKER, EmpathyIndividualLinker))
#define EMPATHY_INDIVIDUAL_LINKER_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_INDIVIDUAL_LINKER, EmpathyIndividualLinkerClass))
#define EMPATHY_IS_INDIVIDUAL_LINKER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_INDIVIDUAL_LINKER))
#define EMPATHY_IS_INDIVIDUAL_LINKER_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_INDIVIDUAL_LINKER))
#define EMPATHY_INDIVIDUAL_LINKER_GET_CLASS(o) ( \
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_LINKER, \
        EmpathyIndividualLinkerClass))

typedef struct {
	GtkBox parent;

	/*<private>*/
	gpointer priv;
} EmpathyIndividualLinker;

typedef struct {
	GtkBoxClass parent_class;
} EmpathyIndividualLinkerClass;

GType empathy_individual_linker_get_type (void) G_GNUC_CONST;
GtkWidget * empathy_individual_linker_new (FolksIndividual *start_individual);

FolksIndividual * empathy_individual_linker_get_start_individual (
    EmpathyIndividualLinker *self);
void empathy_individual_linker_set_start_individual (
    EmpathyIndividualLinker *self,
    FolksIndividual *individual);

GeeSet * empathy_individual_linker_get_linked_personas (
    EmpathyIndividualLinker *self);

gboolean empathy_individual_linker_get_has_changed (
    EmpathyIndividualLinker *self);

void empathy_individual_linker_set_search_text (EmpathyIndividualLinker *self,
    const gchar *search_text);

G_END_DECLS

#endif /* __EMPATHY_INDIVIDUAL_LINKER_H__ */

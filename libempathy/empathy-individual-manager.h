/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_MANAGER_H__
#define __EMPATHY_INDIVIDUAL_MANAGER_H__

#include <glib.h>
#include <folks/folks.h>

#include "empathy-contact.h"

G_BEGIN_DECLS
#define EMPATHY_TYPE_INDIVIDUAL_MANAGER         (empathy_individual_manager_get_type ())
#define EMPATHY_INDIVIDUAL_MANAGER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_MANAGER, EmpathyIndividualManager))
#define EMPATHY_INDIVIDUAL_MANAGER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INDIVIDUAL_MANAGER, EmpathyIndividualManagerClass))
#define EMPATHY_IS_INDIVIDUAL_MANAGER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_MANAGER))
#define EMPATHY_IS_INDIVIDUAL_MANAGER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_MANAGER))
#define EMPATHY_INDIVIDUAL_MANAGER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_MANAGER, EmpathyIndividualManagerClass))

typedef enum
{
  EMPATHY_INDIVIDUAL_MANAGER_NO_FLAGS = 0,
  EMPATHY_INDIVIDUAL_MANAGER_CAN_ADD = 1 << 0,
  EMPATHY_INDIVIDUAL_MANAGER_CAN_REMOVE = 1 << 1,
  EMPATHY_INDIVIDUAL_MANAGER_CAN_ALIAS = 1 << 2,
  EMPATHY_INDIVIDUAL_MANAGER_CAN_GROUP = 1 << 3,
  EMPATHY_INDIVIDUAL_MANAGER_CAN_BLOCK = 1 << 4,
} EmpathyIndividualManagerFlags;

typedef struct _EmpathyIndividualManager EmpathyIndividualManager;
typedef struct _EmpathyIndividualManagerClass EmpathyIndividualManagerClass;

struct _EmpathyIndividualManager
{
  GObject parent;
  gpointer priv;
};

struct _EmpathyIndividualManagerClass
{
  GObjectClass parent_class;
};

GType empathy_individual_manager_get_type (void) G_GNUC_CONST;

gboolean empathy_individual_manager_initialized (void);

EmpathyIndividualManager *empathy_individual_manager_dup_singleton (void);

GList *empathy_individual_manager_get_members (
    EmpathyIndividualManager *manager);

FolksIndividual *empathy_individual_manager_lookup_member (
    EmpathyIndividualManager *manager,
    const gchar *id);

void empathy_individual_manager_add_from_contact (
    EmpathyIndividualManager *manager,
    EmpathyContact *contact);

void empathy_individual_manager_remove (EmpathyIndividualManager *manager,
    FolksIndividual *individual,
    const gchar *message);

void empathy_individual_manager_remove_group (EmpathyIndividualManager *manager,
    const gchar *group);

void empathy_individual_manager_link_personas (EmpathyIndividualManager *self,
    GList *personas);

void empathy_individual_manager_unlink_individual (
    EmpathyIndividualManager *self,
    FolksIndividual *individual);

gboolean empathy_individual_manager_supports_blocking (
    EmpathyIndividualManager *self,
    FolksIndividual *individual);

void empathy_individual_manager_set_blocked (EmpathyIndividualManager *self,
    FolksIndividual *individual,
    gboolean blocked);

G_END_DECLS
#endif /* __EMPATHY_INDIVIDUAL_MANAGER_H__ */

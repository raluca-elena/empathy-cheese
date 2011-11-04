/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#ifndef __EMPATHY_INDIVIDUAL_STORE_H__
#define __EMPATHY_INDIVIDUAL_STORE_H__

#include <gtk/gtk.h>

#include <libempathy/empathy-individual-manager.h>

G_BEGIN_DECLS
#define EMPATHY_TYPE_INDIVIDUAL_STORE         (empathy_individual_store_get_type ())
#define EMPATHY_INDIVIDUAL_STORE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_STORE, EmpathyIndividualStore))
#define EMPATHY_INDIVIDUAL_STORE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INDIVIDUAL_STORE, EmpathyIndividualStoreClass))
#define EMPATHY_IS_INDIVIDUAL_STORE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_STORE))
#define EMPATHY_IS_INDIVIDUAL_STORE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_STORE))
#define EMPATHY_INDIVIDUAL_STORE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_STORE, EmpathyIndividualStoreClass))

typedef struct _EmpathyIndividualStore EmpathyIndividualStore;
typedef struct _EmpathyIndividualStoreClass EmpathyIndividualStoreClass;
typedef struct _EmpathyIndividualStorePriv EmpathyIndividualStorePriv;

typedef enum
{
  EMPATHY_INDIVIDUAL_STORE_SORT_STATE,
  EMPATHY_INDIVIDUAL_STORE_SORT_NAME
} EmpathyIndividualStoreSort;

typedef enum
{
  EMPATHY_INDIVIDUAL_STORE_COL_ICON_STATUS,
  EMPATHY_INDIVIDUAL_STORE_COL_PIXBUF_AVATAR,
  EMPATHY_INDIVIDUAL_STORE_COL_PIXBUF_AVATAR_VISIBLE,
  EMPATHY_INDIVIDUAL_STORE_COL_NAME,
  EMPATHY_INDIVIDUAL_STORE_COL_PRESENCE_TYPE,
  EMPATHY_INDIVIDUAL_STORE_COL_STATUS,
  EMPATHY_INDIVIDUAL_STORE_COL_COMPACT,
  EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL,
  EMPATHY_INDIVIDUAL_STORE_COL_IS_GROUP,
  EMPATHY_INDIVIDUAL_STORE_COL_IS_ACTIVE,
  EMPATHY_INDIVIDUAL_STORE_COL_IS_ONLINE,
  EMPATHY_INDIVIDUAL_STORE_COL_IS_SEPARATOR,
  EMPATHY_INDIVIDUAL_STORE_COL_CAN_AUDIO_CALL,
  EMPATHY_INDIVIDUAL_STORE_COL_CAN_VIDEO_CALL,
  EMPATHY_INDIVIDUAL_STORE_COL_IS_FAKE_GROUP,
  EMPATHY_INDIVIDUAL_STORE_COL_CLIENT_TYPES,
  EMPATHY_INDIVIDUAL_STORE_COL_EVENT_COUNT,
  EMPATHY_INDIVIDUAL_STORE_COL_COUNT,
} EmpathyIndividualStoreCol;

#define EMPATHY_INDIVIDUAL_STORE_UNGROUPED _("Ungrouped")
#define EMPATHY_INDIVIDUAL_STORE_FAVORITE  _("Favorite People")
#define EMPATHY_INDIVIDUAL_STORE_PEOPLE_NEARBY _("People Nearby")

struct _EmpathyIndividualStore
{
  GtkTreeStore parent;
  EmpathyIndividualStorePriv *priv;
};

struct _EmpathyIndividualStoreClass
{
  GtkTreeStoreClass parent_class;
};

GType
empathy_individual_store_get_type (void) G_GNUC_CONST;

EmpathyIndividualStore *empathy_individual_store_new (
    EmpathyIndividualManager *manager);

EmpathyIndividualManager *empathy_individual_store_get_manager (
    EmpathyIndividualStore *store);

gboolean empathy_individual_store_get_show_avatars (
    EmpathyIndividualStore *store);

void empathy_individual_store_set_show_avatars (EmpathyIndividualStore *store,
    gboolean show_avatars);

gboolean empathy_individual_store_get_show_groups (
    EmpathyIndividualStore *store);

void empathy_individual_store_set_show_groups (EmpathyIndividualStore *store,
    gboolean show_groups);

gboolean empathy_individual_store_get_is_compact (
    EmpathyIndividualStore *store);

void empathy_individual_store_set_is_compact (EmpathyIndividualStore *store,
    gboolean is_compact);

gboolean empathy_individual_store_get_show_protocols (
    EmpathyIndividualStore *store);

void empathy_individual_store_set_show_protocols (
    EmpathyIndividualStore *store,
    gboolean show_protocols);

EmpathyIndividualStoreSort empathy_individual_store_get_sort_criterium (
    EmpathyIndividualStore *store);

void empathy_individual_store_set_sort_criterium (
    EmpathyIndividualStore *store,
    EmpathyIndividualStoreSort sort_criterium);

gboolean empathy_individual_store_row_separator_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer data);

gchar *empathy_individual_store_get_parent_group (GtkTreeModel *model,
    GtkTreePath *path,
    gboolean *path_is_group,
    gboolean *is_fake_group);

GdkPixbuf *empathy_individual_store_get_individual_status_icon (
    EmpathyIndividualStore *store,
    FolksIndividual *individual);

void individual_store_add_individual_and_connect (EmpathyIndividualStore *self,
    FolksIndividual *individual);

void individual_store_remove_individual_and_disconnect (
    EmpathyIndividualStore *self,
    FolksIndividual *individual);

G_END_DECLS
#endif /* __EMPATHY_INDIVIDUAL_STORE_H__ */

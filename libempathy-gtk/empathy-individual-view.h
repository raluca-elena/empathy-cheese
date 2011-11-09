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

#ifndef __EMPATHY_INDIVIDUAL_VIEW_H__
#define __EMPATHY_INDIVIDUAL_VIEW_H__

#include <gtk/gtk.h>

#include <folks/folks.h>

#include <libempathy/empathy-enum-types.h>

#include "empathy-live-search.h"
#include "empathy-individual-menu.h"
#include "empathy-individual-store.h"

G_BEGIN_DECLS
#define EMPATHY_TYPE_INDIVIDUAL_VIEW         (empathy_individual_view_get_type ())
#define EMPATHY_INDIVIDUAL_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualView))
#define EMPATHY_INDIVIDUAL_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualViewClass))
#define EMPATHY_IS_INDIVIDUAL_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW))
#define EMPATHY_IS_INDIVIDUAL_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INDIVIDUAL_VIEW))
#define EMPATHY_INDIVIDUAL_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_VIEW, EmpathyIndividualViewClass))
typedef struct _EmpathyIndividualView EmpathyIndividualView;
typedef struct _EmpathyIndividualViewClass EmpathyIndividualViewClass;

typedef enum
{
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_NONE = 0,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_SAVE = 1 << 0,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_RENAME = 1 << 1,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_REMOVE = 1 << 2,
  /* NOTE: For this to behave as expected, FEATURE_INDIVIDUAL_DRAG and
   * FEATURE_INDIVIDUAL_DROP should also be specified. */
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_GROUPS_CHANGE = 1 << 3,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_REMOVE = 1 << 4,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_DROP = 1 << 5,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_DRAG = 1 << 6,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_TOOLTIP = 1 << 7,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_INDIVIDUAL_CALL = 1 << 8,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_PERSONA_DROP = 1 << 9,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_FILE_DROP = 1 << 10,
  EMPATHY_INDIVIDUAL_VIEW_FEATURE_ALL = (1 << 11) - 1,
} EmpathyIndividualViewFeatureFlags;

struct _EmpathyIndividualView
{
  GtkTreeView parent;
  gpointer priv;
};

struct _EmpathyIndividualViewClass
{
  GtkTreeViewClass parent_class;

  void (* drag_individual_received) (EmpathyIndividualView *self,
      GdkDragAction action,
      FolksIndividual *individual,
      const gchar *new_group,
      const gchar *old_group);

  void (* drag_persona_received) (EmpathyIndividualView *self,
      GdkDragAction action,
      FolksPersona *persona,
      FolksIndividual *individual);
};

GType empathy_individual_view_get_type (void) G_GNUC_CONST;

EmpathyIndividualView *empathy_individual_view_new (
    EmpathyIndividualStore *store,
    EmpathyIndividualViewFeatureFlags view_features,
    EmpathyIndividualFeatureFlags individual_features);

FolksIndividual *empathy_individual_view_dup_selected (
    EmpathyIndividualView *view);

GtkWidget *empathy_individual_view_get_individual_menu (
    EmpathyIndividualView *view);

GtkWidget *empathy_individual_view_get_group_menu (EmpathyIndividualView *view);

void empathy_individual_view_set_live_search (EmpathyIndividualView *view,
    EmpathyLiveSearch *search);

gboolean empathy_individual_view_get_show_offline (
    EmpathyIndividualView *view);

void empathy_individual_view_set_show_offline (
    EmpathyIndividualView *view,
    gboolean show_offline);

gboolean empathy_individual_view_get_show_untrusted (
    EmpathyIndividualView *self);

void empathy_individual_view_set_show_untrusted (EmpathyIndividualView *self,
    gboolean show_untrusted);

void empathy_individual_view_set_show_uninteresting (
    EmpathyIndividualView *view,
    gboolean show_uninteresting);

gboolean empathy_individual_view_is_searching (
    EmpathyIndividualView *view);

EmpathyIndividualStore *empathy_individual_view_get_store (
    EmpathyIndividualView *self);
void empathy_individual_view_set_store (EmpathyIndividualView *self,
    EmpathyIndividualStore *store);

void empathy_individual_view_start_search (EmpathyIndividualView *self);

void empathy_individual_view_set_custom_filter (EmpathyIndividualView *self,
    GtkTreeModelFilterVisibleFunc filter,
    gpointer data);

void empathy_individual_view_refilter (EmpathyIndividualView *self);

void empathy_individual_view_select_first (EmpathyIndividualView *self);

G_END_DECLS
#endif /* __EMPATHY_INDIVIDUAL_VIEW_H__ */

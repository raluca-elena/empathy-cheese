/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008-2010 Collabora Ltd.
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

#ifndef __EMPATHY_INDIVIDUAL_MENU_H__
#define __EMPATHY_INDIVIDUAL_MENU_H__

#include <gtk/gtk.h>

#include "empathy-individual-store.h"

G_BEGIN_DECLS

typedef enum {
	EMPATHY_INDIVIDUAL_FEATURE_NONE = 0,
	EMPATHY_INDIVIDUAL_FEATURE_CHAT = 1 << 0,
	EMPATHY_INDIVIDUAL_FEATURE_CALL = 1 << 1,
	EMPATHY_INDIVIDUAL_FEATURE_LOG = 1 << 2,
	EMPATHY_INDIVIDUAL_FEATURE_EDIT = 1 << 3,
	EMPATHY_INDIVIDUAL_FEATURE_INFO = 1 << 4,
	EMPATHY_INDIVIDUAL_FEATURE_FAVOURITE = 1 << 5,
	EMPATHY_INDIVIDUAL_FEATURE_LINK = 1 << 6,
	EMPATHY_INDIVIDUAL_FEATURE_SMS = 1 << 7,
	EMPATHY_INDIVIDUAL_FEATURE_CALL_PHONE = 1 << 8,
	EMPATHY_INDIVIDUAL_FEATURE_ADD_CONTACT = 1 << 9,
	EMPATHY_INDIVIDUAL_FEATURE_ALL = (1 << 10) - 1,
} EmpathyIndividualFeatureFlags;

#define EMPATHY_TYPE_INDIVIDUAL_MENU (empathy_individual_menu_get_type ())
#define EMPATHY_INDIVIDUAL_MENU(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), \
    EMPATHY_TYPE_INDIVIDUAL_MENU, EmpathyIndividualMenu))
#define EMPATHY_INDIVIDUAL_MENU_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), \
    EMPATHY_TYPE_INDIVIDUAL_MENU, EmpathyIndividualMenuClass))
#define EMPATHY_IS_INDIVIDUAL_MENU(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
    EMPATHY_TYPE_INDIVIDUAL_MENU))
#define EMPATHY_IS_INDIVIDUAL_MENU_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), \
    EMPATHY_TYPE_INDIVIDUAL_MENU))
#define EMPATHY_INDIVIDUAL_MENU_GET_CLASS(o) ( \
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INDIVIDUAL_MENU, \
        EmpathyIndividualMenuClass))

typedef struct {
	GtkMenu parent;

	/*<private>*/
	gpointer priv;
} EmpathyIndividualMenu;

typedef struct {
	GtkMenuClass parent_class;
} EmpathyIndividualMenuClass;

GType empathy_individual_menu_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_individual_menu_new (FolksIndividual *individual,
    EmpathyIndividualFeatureFlags features,
    EmpathyIndividualStore *store);
GtkWidget * empathy_individual_chat_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_sms_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_audio_call_menu_item_new (
    FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_video_call_menu_item_new (
    FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_log_menu_item_new  (FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_info_menu_item_new (FolksIndividual *individual);
GtkWidget * empathy_individual_edit_menu_item_new (FolksIndividual *individual);
GtkWidget * empathy_individual_link_menu_item_new (FolksIndividual *individual);
GtkWidget * empathy_individual_invite_menu_item_new (
    FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_file_transfer_menu_item_new (
    FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_share_my_desktop_menu_item_new (
    FolksIndividual *individual,
    EmpathyContact *contact);
GtkWidget * empathy_individual_favourite_menu_item_new (
    FolksIndividual *individual);
GtkWidget * empathy_individual_add_menu_item_new (EmpathyIndividualMenu *self,
    FolksIndividual *individual);

G_END_DECLS

#endif /* __EMPATHY_INDIVIDUAL_MENU_H__ */


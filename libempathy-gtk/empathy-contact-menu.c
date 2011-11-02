/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008 Collabora Ltd.
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
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <telepathy-logger/log-manager.h>

#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-contact-manager.h>

#include "empathy-contact-menu.h"
#include "empathy-images.h"
#include "empathy-log-window.h"
#include "empathy-contact-dialogs.h"
#include "empathy-ui-utils.h"
#include "empathy-share-my-desktop.h"
#include "empathy-call-utils.h"

static GtkWidget *empathy_contact_block_menu_item_new (EmpathyContact *);

GtkWidget *
empathy_contact_menu_new (EmpathyContact             *contact,
			  EmpathyContactFeatureFlags  features)
{
	GtkWidget    *menu;
	GtkMenuShell *shell;
	GtkWidget    *item;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	if (features == EMPATHY_CONTACT_FEATURE_NONE) {
		return NULL;
	}

	menu = gtk_menu_new ();
	shell = GTK_MENU_SHELL (menu);

	/* Add Contact */
	item = empathy_contact_add_menu_item_new (contact);
	if (item) {
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Chat */
	if (features & EMPATHY_CONTACT_FEATURE_CHAT) {
		item = empathy_contact_chat_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	if (features & EMPATHY_CONTACT_FEATURE_CALL) {
		/* Audio Call */
		item = empathy_contact_audio_call_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);

		/* Video Call */
		item = empathy_contact_video_call_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Invite */
	item = empathy_contact_invite_menu_item_new (contact);
	gtk_menu_shell_append (shell, item);
	gtk_widget_show (item);

	/* File transfer */
	if (features & EMPATHY_CONTACT_FEATURE_FT) {
		item = empathy_contact_file_transfer_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Share my desktop */
	/* FIXME we should add the "Share my desktop" menu item if Vino is
	a registered handler in MC5 */
	item = empathy_contact_share_my_desktop_menu_item_new (contact);
	gtk_menu_shell_append (shell, item);
	gtk_widget_show (item);

	/* Separator */
	if (features & (EMPATHY_CONTACT_FEATURE_EDIT |
			EMPATHY_CONTACT_FEATURE_INFO)) {
		item = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Edit */
	if (features & EMPATHY_CONTACT_FEATURE_EDIT) {
		item = empathy_contact_edit_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Log */
	if (features & EMPATHY_CONTACT_FEATURE_LOG) {
		item = empathy_contact_log_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Info */
	if (features & EMPATHY_CONTACT_FEATURE_INFO) {
		item = empathy_contact_info_menu_item_new (contact);
		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	/* Separator & Block */
	if (features & EMPATHY_CONTACT_FEATURE_BLOCK &&
	    (item = empathy_contact_block_menu_item_new (contact)) != NULL) {
		GtkWidget *sep;

		sep = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (shell, sep);
		gtk_widget_show (sep);

		gtk_menu_shell_append (shell, item);
		gtk_widget_show (item);
	}

	return menu;
}

static void
empathy_contact_add_menu_item_activated (GtkMenuItem *item,
	EmpathyContact *contact)
{
	GtkWidget *toplevel;

	toplevel = gtk_widget_get_toplevel (GTK_WIDGET (item));
        if (!gtk_widget_is_toplevel (toplevel) || !GTK_IS_WINDOW (toplevel)) {
		toplevel = NULL;
	}

	empathy_new_contact_dialog_show_with_contact (GTK_WINDOW (toplevel),
						      contact);
}

GtkWidget *
empathy_contact_add_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;
	EmpathyContactManager *manager;
	TpConnection *connection;
	GList *l, *members;
	gboolean found = FALSE;
	EmpathyContactListFlags flags;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	if (!empathy_contact_manager_initialized ()) {
		return NULL;
	}

	manager = empathy_contact_manager_dup_singleton ();
	connection = empathy_contact_get_connection (contact);

	flags = empathy_contact_manager_get_flags_for_connection (manager,
			connection);

	if (!(flags & EMPATHY_CONTACT_LIST_CAN_ADD)) {
		return NULL;
	}

	members = empathy_contact_list_get_members (EMPATHY_CONTACT_LIST (manager));
	for (l = members; l; l = l->next) {
		if (!found && empathy_contact_equal (l->data, contact)) {
			found = TRUE;
			/* we keep iterating so that we don't leak contact
			 * refs */
		}

		g_object_unref (l->data);
	}
	g_list_free (members);
	g_object_unref (manager);

	if (found) {
		return NULL;
	}

	item = gtk_image_menu_item_new_with_mnemonic (_("_Add Contact…"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_ADD,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

	g_signal_connect (item, "activate",
			G_CALLBACK (empathy_contact_add_menu_item_activated),
			contact);

	return item;
}

static void
empathy_contact_block_menu_item_toggled (GtkCheckMenuItem *item,
					 EmpathyContact   *contact)
{
	static guint block_signal = 0;
	gboolean blocked, abusive;
	TpContact *tp_contact;

	if (block_signal > 0)
		return;

	blocked = gtk_check_menu_item_get_active (item);

	if (blocked) {
		/* confirm the user really wishes to block the contact */
		GtkWidget *parent;
		GdkPixbuf *avatar;

		/* gtk_menu_get_attach_widget () doesn't behave properly here
		 * for some reason */
		parent = g_object_get_data (
			G_OBJECT (gtk_widget_get_parent (GTK_WIDGET (item))),
			"window");

		avatar = empathy_pixbuf_avatar_from_contact_scaled (contact, 48, 48);

		if (!empathy_block_contact_dialog_show (GTK_WINDOW (parent),
					contact, avatar, &abusive))
			return;
	}

	tp_contact = empathy_contact_get_tp_contact (contact);

	if (blocked)
		tp_contact_block_async (tp_contact, abusive, NULL, NULL);
	else
		tp_contact_unblock_async (tp_contact, NULL, NULL);

	/* update the toggle with the blocked status */
	block_signal++;
	gtk_check_menu_item_set_active (item, blocked);
	block_signal--;
}

static GtkWidget *
empathy_contact_block_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	EmpathyContactManager *manager;
	TpConnection *connection;
	TpContact *tp_contact;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	manager = empathy_contact_manager_dup_singleton ();

	if (!empathy_contact_manager_initialized ()) {
		return NULL;
	}

	connection = empathy_contact_get_connection (contact);

	if (!tp_proxy_has_interface_by_id (connection,
		TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_BLOCKING))
		return NULL;

	item = gtk_check_menu_item_new_with_mnemonic (_("_Block Contact"));

	tp_contact = empathy_contact_get_tp_contact (contact);

	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
			tp_contact_is_blocked (tp_contact));

	g_signal_connect (item, "toggled",
			G_CALLBACK (empathy_contact_block_menu_item_toggled),
			contact);

	return item;
}

static void
empathy_contact_chat_menu_item_activated (GtkMenuItem *item,
	EmpathyContact *contact)
{
	empathy_chat_with_contact (contact, empathy_get_current_action_time ());
}

GtkWidget *
empathy_contact_chat_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (_("_Chat"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_MESSAGE,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_set_sensitive (item, !empathy_contact_is_user (contact));
	gtk_widget_show (image);

	g_signal_connect (item, "activate",
				  G_CALLBACK (empathy_contact_chat_menu_item_activated),
				  contact);

	return item;
}

static void
empathy_contact_audio_call_menu_item_activated (GtkMenuItem *item,
	EmpathyContact *contact)
{
	empathy_call_new_with_streams (empathy_contact_get_id (contact),
		empathy_contact_get_account (contact),
		TRUE, FALSE,
		empathy_get_current_action_time ());
}

GtkWidget *
empathy_contact_audio_call_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Audio Call"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_set_sensitive (item, empathy_contact_can_voip_audio (contact) &&
					!empathy_contact_is_user (contact));
	gtk_widget_show (image);

	g_signal_connect (item, "activate",
				  G_CALLBACK (empathy_contact_audio_call_menu_item_activated),
				  contact);

	return item;
}

static void
empathy_contact_video_call_menu_item_activated (GtkMenuItem *item,
	EmpathyContact *contact)
{
	empathy_call_new_with_streams (empathy_contact_get_id (contact),
		empathy_contact_get_account (contact),
		TRUE, TRUE,
		empathy_get_current_action_time ());
}

GtkWidget *
empathy_contact_video_call_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Video Call"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VIDEO_CALL,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_set_sensitive (item, empathy_contact_can_voip_video (contact) &&
					!empathy_contact_is_user (contact));
	gtk_widget_show (image);

	g_signal_connect (item, "activate",
				  G_CALLBACK (empathy_contact_video_call_menu_item_activated),
				  contact);

	return item;
}

static void
contact_log_menu_item_activate_cb (EmpathyContact *contact)
{
	empathy_log_window_show (empathy_contact_get_account (contact),
				 empathy_contact_get_id (contact),
				 FALSE, NULL);
}

GtkWidget *
empathy_contact_log_menu_item_new (EmpathyContact *contact)
{
	TplLogManager     *manager;
	TplEntity         *entity;
	gboolean           have_log;
	GtkWidget         *item;
	GtkWidget         *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	manager = tpl_log_manager_dup_singleton ();
	entity = tpl_entity_new_from_tp_contact (empathy_contact_get_tp_contact (contact),
						 TPL_ENTITY_CONTACT);

	have_log = tpl_log_manager_exists (manager,
					   empathy_contact_get_account (contact),
					   entity,
					   TPL_EVENT_MASK_TEXT);

	g_object_unref (entity);
	g_object_unref (manager);

	item = gtk_image_menu_item_new_with_mnemonic (_("_Previous Conversations"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_LOG,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_set_sensitive (item, have_log);
	gtk_widget_show (image);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (contact_log_menu_item_activate_cb),
				  contact);

	return item;
}

GtkWidget *
empathy_contact_file_transfer_menu_item_new (EmpathyContact *contact)
{
	GtkWidget         *item;
	GtkWidget         *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (_("Send File"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_DOCUMENT_SEND,
					      GTK_ICON_SIZE_MENU);
	gtk_widget_set_sensitive (item, empathy_contact_can_send_files (contact) &&
					!empathy_contact_is_user (contact));
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_show (image);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (empathy_send_file_with_file_chooser),
				  contact);

	return item;
}

GtkWidget *
empathy_contact_share_my_desktop_menu_item_new (EmpathyContact *contact)
{
	GtkWidget         *item;
	GtkWidget         *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (_("Share My Desktop"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_NETWORK,
					      GTK_ICON_SIZE_MENU);
	gtk_widget_set_sensitive (item, empathy_contact_can_use_rfb_stream_tube (
		contact));
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_show (image);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (empathy_share_my_desktop_share_with_contact),
				  contact);

	return item;
}

static void
contact_info_menu_item_activate_cb (EmpathyContact *contact)
{
	empathy_contact_information_dialog_show (contact, NULL);
}

GtkWidget *
empathy_contact_info_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (_("Infor_mation"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_CONTACT_INFORMATION,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_show (image);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (contact_info_menu_item_activate_cb),
				  contact);

	return item;
}

static void
contact_edit_menu_item_activate_cb (EmpathyContact *contact)
{
	empathy_contact_edit_dialog_show (contact, NULL);
}

GtkWidget *
empathy_contact_edit_menu_item_new (EmpathyContact *contact)
{
	EmpathyContactManager *manager;
	GtkWidget *item;
	GtkWidget *image;
	gboolean enable = FALSE;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	if (empathy_contact_manager_initialized ()) {
		TpConnection *connection;
		EmpathyContactListFlags flags;

		manager = empathy_contact_manager_dup_singleton ();
		connection = empathy_contact_get_connection (contact);
		flags = empathy_contact_manager_get_flags_for_connection (
				manager, connection);

		enable = (flags & EMPATHY_CONTACT_LIST_CAN_ALIAS ||
		          flags & EMPATHY_CONTACT_LIST_CAN_GROUP);

		g_object_unref (manager);
	}

	item = gtk_image_menu_item_new_with_mnemonic (
						     C_("Edit contact (contextual menu)",
						        "_Edit"));
	image = gtk_image_new_from_icon_name (GTK_STOCK_EDIT,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_widget_show (image);

	gtk_widget_set_sensitive (item, enable);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (contact_edit_menu_item_activate_cb),
				  contact);

	return item;
}

typedef struct  {
	EmpathyContact *contact;
	EmpathyChatroom *chatroom;
} RoomSubMenuData;

static RoomSubMenuData *
room_sub_menu_data_new (EmpathyContact *contact,
			EmpathyChatroom *chatroom)
{
	RoomSubMenuData *data;

	data = g_slice_new (RoomSubMenuData);
	data->contact = g_object_ref (contact);
	data->chatroom = g_object_ref (chatroom);
	return data;
}

static void
room_sub_menu_data_free (RoomSubMenuData *data)
{
	g_object_unref (data->contact);
	g_object_unref (data->chatroom);
	g_slice_free (RoomSubMenuData, data);
}

static void
room_sub_menu_activate_cb (GtkWidget *item,
			   RoomSubMenuData *data)
{
	EmpathyTpChat *chat;

	chat = empathy_chatroom_get_tp_chat (data->chatroom);
	if (chat == NULL) {
		/* channel was invalidated. Ignoring */
		return;
	}

	/* send invitation */
	empathy_contact_list_add (EMPATHY_CONTACT_LIST (chat), data->contact,
		_("Inviting you to this room"));
}

static GtkWidget *
create_room_sub_menu (EmpathyContact *contact,
                      EmpathyChatroom *chatroom)
{
	GtkWidget *item;
	RoomSubMenuData *data;

	item = gtk_menu_item_new_with_label (empathy_chatroom_get_name (chatroom));
	data = room_sub_menu_data_new (contact, chatroom);
	g_signal_connect_data (item, "activate",
			       G_CALLBACK (room_sub_menu_activate_cb), data,
			       (GClosureNotify) room_sub_menu_data_free, 0);

	return item;
}

GtkWidget *
empathy_contact_invite_menu_item_new (EmpathyContact *contact)
{
	GtkWidget *item;
	GtkWidget *image;
	GtkWidget *room_item;
	EmpathyChatroomManager *mgr;
	GList *rooms, *l;
	GtkWidget *submenu = NULL;

	g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

	item = gtk_image_menu_item_new_with_mnemonic (_("_Invite to Chat Room"));
	image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_GROUP_MESSAGE,
					      GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

	if (empathy_contact_is_user (contact)) {
		gtk_widget_set_sensitive (item, FALSE);
		gtk_widget_show (image);
		return item;
	}

	mgr = empathy_chatroom_manager_dup_singleton (NULL);
	rooms = empathy_chatroom_manager_get_chatrooms (mgr,
		empathy_contact_get_account (contact));

	for (l = rooms; l != NULL; l = g_list_next (l)) {
		EmpathyChatroom *chatroom = l->data;

		if (empathy_chatroom_get_tp_chat (chatroom) != NULL) {
			if (G_UNLIKELY (submenu == NULL))
				submenu = gtk_menu_new ();

			room_item = create_room_sub_menu (contact, chatroom);
			gtk_menu_shell_append ((GtkMenuShell *) submenu, room_item);
			gtk_widget_show (room_item);
		}
	}

	if (submenu) {
		gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	} else {
		gtk_widget_set_sensitive (item, FALSE);
	}

	gtk_widget_show (image);

	g_object_unref (mgr);
	g_list_free (rooms);

	return item;
}


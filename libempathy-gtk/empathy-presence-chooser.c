/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-connectivity.h>
#include <libempathy/empathy-presence-manager.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-status-presets.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-ui-utils.h"
#include "empathy-images.h"
#include "empathy-presence-chooser.h"
#include "empathy-status-preset-dialog.h"

/**
 * SECTION:empathy-presence-chooser
 * @title:EmpathyPresenceChooser
 * @short_description: A widget used to change presence
 * @include: libempathy-gtk/empathy-presence-chooser.h
 *
 * #EmpathyPresenceChooser is a widget which extends #GtkComboBoxEntry
 * to change presence.
 */

/**
 * EmpathyAccountChooser:
 * @parent: parent object
 *
 * Widget which extends #GtkComboBoxEntry to change presence.
 */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyPresenceChooser)

/* For custom message dialog */
enum {
	COL_ICON,
	COL_LABEL,
	COL_PRESENCE,
	COL_COUNT
};

/* For combobox's model */
enum {
	COL_STATUS_TEXT,
	COL_STATE_ICON_NAME,
	COL_STATE,
	COL_DISPLAY_MARKUP,
	COL_STATUS_CUSTOMISABLE,
	COL_TYPE,
	N_COLUMNS
};

typedef enum  {
	ENTRY_TYPE_BUILTIN,
	ENTRY_TYPE_SAVED,
	ENTRY_TYPE_CUSTOM,
	ENTRY_TYPE_SEPARATOR,
	ENTRY_TYPE_EDIT_CUSTOM,
} PresenceChooserEntryType;

typedef struct {
	EmpathyPresenceManager *presence_mgr;
	EmpathyConnectivity *connectivity;

	gboolean     editing_status;
	int          block_set_editing;
	int          block_changed;
	guint        focus_out_idle_source;

	TpConnectionPresenceType state;
	PresenceChooserEntryType previous_type;

	TpAccountManager *account_manager;
	GdkPixbuf *not_favorite_pixbuf;
} EmpathyPresenceChooserPriv;

/* States to be listed in the menu.
 * Each state has a boolean telling if it can have custom message */
static struct { TpConnectionPresenceType state;
         gboolean customisable;
} states[] = { { TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE } ,
			 { TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE },
			 { TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE },
			 { TP_CONNECTION_PRESENCE_TYPE_HIDDEN, FALSE },
			 { TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE},
			 { TP_CONNECTION_PRESENCE_TYPE_UNSET, },
			};

static void            presence_chooser_constructed            (GObject                    *object);
static void            presence_chooser_finalize               (GObject                    *object);
static void            presence_chooser_presence_changed_cb    (EmpathyPresenceChooser      *chooser);
static void            presence_chooser_menu_add_item          (GtkWidget                  *menu,
								const gchar                *str,
								TpConnectionPresenceType                  state);
static void            presence_chooser_noncustom_activate_cb  (GtkWidget                  *item,
								gpointer                    user_data);
static void            presence_chooser_set_state              (TpConnectionPresenceType                  state,
								const gchar                *status);
static void            presence_chooser_custom_activate_cb     (GtkWidget                  *item,
								gpointer                    user_data);

G_DEFINE_TYPE (EmpathyPresenceChooser, empathy_presence_chooser, GTK_TYPE_COMBO_BOX);

static void
empathy_presence_chooser_class_init (EmpathyPresenceChooserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = presence_chooser_constructed;
	object_class->finalize = presence_chooser_finalize;

	g_type_class_add_private (object_class, sizeof (EmpathyPresenceChooserPriv));
}

static void
presence_chooser_create_model (EmpathyPresenceChooser *self)
{
	GtkListStore *store;
	char *custom_message;
	int i;

	store = gtk_list_store_new (N_COLUMNS,
				    G_TYPE_STRING,    /* COL_STATUS_TEXT */
				    G_TYPE_STRING,    /* COL_STATE_ICON_NAME */
				    G_TYPE_UINT,      /* COL_STATE */
				    G_TYPE_STRING,    /* COL_DISPLAY_MARKUP */
				    G_TYPE_BOOLEAN,   /* COL_STATUS_CUSTOMISABLE */
				    G_TYPE_INT);      /* COL_TYPE */

	custom_message = g_strdup_printf ("<i>%s</i>", _("Custom Message…"));

	for (i = 0; states[i].state != TP_CONNECTION_PRESENCE_TYPE_UNSET; i++) {
		GList       *list, *l;
		const char *status, *icon_name;

		status = empathy_presence_get_default_message (states[i].state);
		icon_name = empathy_icon_name_for_presence (states[i].state);

		gtk_list_store_insert_with_values (store, NULL, -1,
			COL_STATUS_TEXT, status,
			COL_STATE_ICON_NAME, icon_name,
			COL_STATE, states[i].state,
			COL_DISPLAY_MARKUP, status,
			COL_STATUS_CUSTOMISABLE, states[i].customisable,
			COL_TYPE, ENTRY_TYPE_BUILTIN,
			-1);

		if (states[i].customisable) {
			/* Set custom messages if wanted */
			list = empathy_status_presets_get (states[i].state, -1);
			list = g_list_sort (list, (GCompareFunc) g_utf8_collate);
			for (l = list; l; l = l->next) {
				gtk_list_store_insert_with_values (store,
					NULL, -1,
					COL_STATUS_TEXT, l->data,
					COL_STATE_ICON_NAME, icon_name,
					COL_STATE, states[i].state,
					COL_DISPLAY_MARKUP, l->data,
					COL_STATUS_CUSTOMISABLE, TRUE,
					COL_TYPE, ENTRY_TYPE_SAVED,
					-1);
			}
			g_list_free (list);

			gtk_list_store_insert_with_values (store, NULL, -1,
				COL_STATUS_TEXT, _("Custom Message…"),
				COL_STATE_ICON_NAME, icon_name,
				COL_STATE, states[i].state,
				COL_DISPLAY_MARKUP, custom_message,
				COL_STATUS_CUSTOMISABLE, TRUE,
				COL_TYPE, ENTRY_TYPE_CUSTOM,
				-1);
		}

	}

	/* add a separator */
	gtk_list_store_insert_with_values (store, NULL, -1,
			COL_TYPE, ENTRY_TYPE_SEPARATOR,
			-1);

	gtk_list_store_insert_with_values (store, NULL, -1,
		COL_STATUS_TEXT, _("Edit Custom Messages…"),
		COL_STATE_ICON_NAME, GTK_STOCK_EDIT,
		COL_DISPLAY_MARKUP, _("Edit Custom Messages…"),
		COL_TYPE, ENTRY_TYPE_EDIT_CUSTOM,
		-1);

	g_free (custom_message);

	gtk_combo_box_set_model (GTK_COMBO_BOX (self), GTK_TREE_MODEL (store));
	g_object_unref (store);
}

static void
presence_chooser_popup_shown_cb (GObject *self,
                                 GParamSpec *pspec,
				 gpointer user_data)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	gboolean shown;

	g_object_get (self, "popup-shown", &shown, NULL);
	if (!shown) {
		return;
	}

	/* see presence_chooser_entry_focus_out_cb () for what this does */
	if (priv->focus_out_idle_source != 0) {
		g_source_remove (priv->focus_out_idle_source);
		priv->focus_out_idle_source = 0;
	}

	presence_chooser_create_model (EMPATHY_PRESENCE_CHOOSER (self));
}

static PresenceChooserEntryType
presence_chooser_get_entry_type (EmpathyPresenceChooser *self)
{
	GtkTreeIter iter;
	PresenceChooserEntryType type = -1;

	if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self), &iter)) {
		type = ENTRY_TYPE_CUSTOM;
	}
	else {
		GtkTreeModel *model;

		model = gtk_combo_box_get_model (GTK_COMBO_BOX (self));
		gtk_tree_model_get (model, &iter,
				    COL_TYPE, &type,
				    -1);
	}

	return type;
}

static TpConnectionPresenceType
get_state_and_status (EmpathyPresenceChooser *self,
	gchar **status)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	TpConnectionPresenceType state;
	gchar *tmp;

	state = tp_account_manager_get_most_available_presence (
		priv->account_manager, NULL, &tmp);
	if (EMP_STR_EMPTY (tmp)) {
		/* no message, use the default message */
		g_free (tmp);
		tmp = g_strdup (empathy_presence_get_default_message (state));
	}

	if (status != NULL)
		*status = tmp;
	else
		g_free (tmp);

	return state;
}

static gboolean
presence_chooser_is_preset (EmpathyPresenceChooser *self)
{
	TpConnectionPresenceType state;
	char *status;
	GList *presets, *l;
	gboolean match = FALSE;

	state = get_state_and_status (self, &status);

	presets = empathy_status_presets_get (state, -1);
	for (l = presets; l; l = l->next) {
		char *preset = (char *) l->data;

		if (!tp_strdiff (status, preset)) {
			match = TRUE;
			break;
		}
	}

	g_list_free (presets);

	DEBUG ("is_preset(%i, %s) = %i", state, status, match);

	g_free (status);
	return match;
}

static void
presence_chooser_set_favorite_icon (EmpathyPresenceChooser *self)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	GtkWidget *entry;
	PresenceChooserEntryType type;

	entry = gtk_bin_get_child (GTK_BIN (self));
	type = presence_chooser_get_entry_type (self);

	if (type == ENTRY_TYPE_CUSTOM || type == ENTRY_TYPE_SAVED) {
		if (presence_chooser_is_preset (self)) {
			/* saved entries can be removed from the list */
			gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry),
				           GTK_ENTRY_ICON_SECONDARY,
					   "emblem-favorite");
			gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 _("Click to remove this status as a favorite"));
		}
		else if (priv->not_favorite_pixbuf != NULL) {
			/* custom entries can be favorited */
			gtk_entry_set_icon_from_pixbuf (GTK_ENTRY (entry),
				           GTK_ENTRY_ICON_SECONDARY,
					   priv->not_favorite_pixbuf);
			gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 _("Click to make this status a favorite"));
		}
	}
	else {
		/* built-in entries cannot be favorited */
		gtk_entry_set_icon_from_stock (GTK_ENTRY (entry),
				           GTK_ENTRY_ICON_SECONDARY,
					   NULL);
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry),
					 GTK_ENTRY_ICON_SECONDARY,
					 NULL);
	}
}

static void
presence_chooser_set_status_editing (EmpathyPresenceChooser *self,
                                     gboolean editing)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	GtkWidget *entry;

	if (priv->block_set_editing) {
		return;
	}

	entry = gtk_bin_get_child (GTK_BIN (self));
	if (editing) {
		priv->editing_status = TRUE;

		gtk_entry_set_icon_from_stock (GTK_ENTRY (entry),
					       GTK_ENTRY_ICON_SECONDARY,
					       GTK_STOCK_OK);
		gtk_entry_set_icon_tooltip_text (GTK_ENTRY (entry),
						 GTK_ENTRY_ICON_SECONDARY,
						 _("Set status"));
		gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
					      GTK_ENTRY_ICON_PRIMARY,
					      FALSE);
	} else {
		GtkWidget *window;

		presence_chooser_set_favorite_icon (self);
		gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
					      GTK_ENTRY_ICON_PRIMARY,
					      TRUE);

		/* attempt to get the toplevel for this widget */
		window = gtk_widget_get_toplevel (GTK_WIDGET (self));
		if (gtk_widget_is_toplevel (window) && GTK_IS_WINDOW (window)) {
			/* unset the focus */
			gtk_window_set_focus (GTK_WINDOW (window), NULL);
		}

		/* see presence_chooser_entry_focus_out_cb ()
		 * for what this does */
		if (priv->focus_out_idle_source != 0) {
			g_source_remove (priv->focus_out_idle_source);
			priv->focus_out_idle_source = 0;
		}

		gtk_editable_set_position (GTK_EDITABLE (entry), 0);

		priv->editing_status = FALSE;
	}
}

static void
mc_set_custom_state (EmpathyPresenceChooser *self)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	GtkWidget *entry;
	const char *status;

	entry = gtk_bin_get_child (GTK_BIN (self));
	/* update the status with MC */
	status = gtk_entry_get_text (GTK_ENTRY (entry));

	DEBUG ("Sending state to MC-> %d (%s)", priv->state, status);

	empathy_presence_manager_set_presence (priv->presence_mgr, priv->state, status);
}

static void
ui_set_custom_state (EmpathyPresenceChooser *self,
		     TpConnectionPresenceType state,
		     const char *status)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	GtkWidget *entry;
	const char *icon_name;
	const gchar *status_tooltip;

	entry = gtk_bin_get_child (GTK_BIN (self));

	priv->block_set_editing++;
	priv->block_changed++;

	icon_name = empathy_icon_name_for_presence (state);
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry),
					   GTK_ENTRY_ICON_PRIMARY,
					   icon_name);
	status_tooltip = status == NULL ? "" : status;
	gtk_entry_set_text (GTK_ENTRY (entry), status_tooltip);
	gtk_widget_set_tooltip_text (GTK_WIDGET (entry), status_tooltip);
	presence_chooser_set_favorite_icon (self);

	priv->block_changed--;
	priv->block_set_editing--;
}

static void
presence_chooser_reset_status (EmpathyPresenceChooser *self)
{
	/* recover the status that was unset */
	presence_chooser_set_status_editing (self, FALSE);
	presence_chooser_presence_changed_cb (self);
}

static void
presence_chooser_entry_icon_release_cb (EmpathyPresenceChooser *self,
                                        GtkEntryIconPosition    icon_pos,
                                        GdkEvent               *event,
					GtkEntry               *entry)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);

	if (priv->editing_status) {
		presence_chooser_set_status_editing (self, FALSE);
		mc_set_custom_state (self);
	}
	else {
		TpConnectionPresenceType state;
		char *status;

		state = get_state_and_status (self, &status);

		if (!empathy_status_presets_is_valid (state)) {
			/* It doesn't make sense to add such presence as favorite */
			g_free (status);
			return;
		}

		if (presence_chooser_is_preset (self)) {
			/* remove the entry */
			DEBUG ("REMOVING PRESET (%i, %s)", state, status);
			empathy_status_presets_remove (state, status);
		}
		else {
			/* save the entry */
			DEBUG ("SAVING PRESET (%i, %s)", state, status);
			empathy_status_presets_set_last (state, status);
		}

		/* update the icon */
		presence_chooser_set_favorite_icon (self);
		g_free (status);
	}
}

static void
presence_chooser_entry_activate_cb (EmpathyPresenceChooser *self,
                                    GtkEntry               *entry)
{
	presence_chooser_set_status_editing (self, FALSE);
	mc_set_custom_state (self);
}

static gboolean
presence_chooser_entry_key_press_event_cb (EmpathyPresenceChooser *self,
                                           GdkEventKey            *event,
					   GtkWidget              *entry)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);

	if (priv->editing_status && event->keyval == GDK_KEY_Escape) {
		/* the user pressed Escape, undo the editing */
		presence_chooser_reset_status (self);
		return TRUE;
	}
	else if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_Down) {
		/* ignore */
		return TRUE;
	}

	return FALSE; /* send this event elsewhere */
}

static gboolean
presence_chooser_entry_button_press_event_cb (EmpathyPresenceChooser *self,
                                              GdkEventButton         *event,
					      GtkWidget              *entry)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);

	if (!priv->editing_status &&
	    event->button == 1 &&
	    !gtk_widget_has_focus (entry)) {
		gtk_widget_grab_focus (entry);
		gtk_editable_select_region (GTK_EDITABLE (entry), 0, -1);

		return TRUE;
	}

	return FALSE;
}

static void
presence_chooser_entry_changed_cb (EmpathyPresenceChooser *self,
				   GtkEntry               *entry)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);

	if (priv->block_changed){
		return;
	}

	/* the combo is being edited to a custom entry */
	if (!priv->editing_status) {
		presence_chooser_set_status_editing (self, TRUE);
	}
}

static void
presence_chooser_changed_cb (GtkComboBox *self, gpointer user_data)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);
	GtkTreeIter iter;
	char *icon_name;
	TpConnectionPresenceType new_state;
	gboolean customisable = TRUE;
	PresenceChooserEntryType type = -1;
	GtkWidget *entry;
	GtkTreeModel *model;

	if (priv->block_changed ||
	    !gtk_combo_box_get_active_iter (self, &iter)) {
		return;
	}

	model = gtk_combo_box_get_model (self);
	gtk_tree_model_get (model, &iter,
			    COL_STATE_ICON_NAME, &icon_name,
			    COL_STATE, &new_state,
			    COL_STATUS_CUSTOMISABLE, &customisable,
			    COL_TYPE, &type,
			    -1);

	entry = gtk_bin_get_child (GTK_BIN (self));

	/* some types of status aren't editable, set the editability of the
	 * entry appropriately. Unless we're just about to reset it anyway,
	 * in which case, don't fiddle with it */
	if (type != ENTRY_TYPE_EDIT_CUSTOM) {
		gtk_editable_set_editable (GTK_EDITABLE (entry), customisable);
		priv->state = new_state;
	}

	if (type == ENTRY_TYPE_EDIT_CUSTOM) {
		GtkWidget *window, *dialog;

		presence_chooser_reset_status (EMPATHY_PRESENCE_CHOOSER (self));

		/* attempt to get the toplevel for this widget */
		window = gtk_widget_get_toplevel (GTK_WIDGET (self));
		if (!gtk_widget_is_toplevel (window) || !GTK_IS_WINDOW (window)) {
			window = NULL;
		}

		dialog = empathy_status_preset_dialog_new (GTK_WINDOW (window));
		gtk_dialog_run (GTK_DIALOG (dialog));
		gtk_widget_destroy (dialog);
	}
	else if (type == ENTRY_TYPE_CUSTOM) {
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry),
						   GTK_ENTRY_ICON_PRIMARY,
						   icon_name);

		/* preseed the status */
		if (priv->previous_type == ENTRY_TYPE_BUILTIN) {
			/* if their previous entry was a builtin, don't
			 * preseed */
			gtk_entry_set_text (GTK_ENTRY (entry), "");
		} else {
			/* else preseed the text of their currently entered
			 * status message */
			char *status;

			get_state_and_status (EMPATHY_PRESENCE_CHOOSER (self),
				&status);
			gtk_entry_set_text (GTK_ENTRY (entry), status);
			g_free (status);
		}

		/* grab the focus */
		gtk_widget_grab_focus (entry);
	} else {
		char *status;

		/* just in case we were setting a new status when
		 * things were changed */
		presence_chooser_set_status_editing (
			EMPATHY_PRESENCE_CHOOSER (self),
			FALSE);
		gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry),
					   GTK_ENTRY_ICON_PRIMARY,
					   icon_name);

		gtk_tree_model_get (model, &iter,
				    COL_STATUS_TEXT, &status,
				    -1);

		empathy_presence_manager_set_presence (priv->presence_mgr, priv->state, status);

		g_free (status);
	}

	if (type != ENTRY_TYPE_EDIT_CUSTOM) {
		priv->previous_type = type;
	}
	g_free (icon_name);
}

static gboolean
combo_row_separator_func (GtkTreeModel	*model,
			  GtkTreeIter	*iter,
			  gpointer	 data)
{
	PresenceChooserEntryType type;

	gtk_tree_model_get (model, iter,
			    COL_TYPE, &type,
			    -1);

	return (type == ENTRY_TYPE_SEPARATOR);
}

static gboolean
presence_chooser_entry_focus_out_idle_cb (gpointer user_data)
{
	EmpathyPresenceChooser *chooser;
	GtkWidget *entry;

	DEBUG ("Autocommiting status message");

	chooser = EMPATHY_PRESENCE_CHOOSER (user_data);
	entry = gtk_bin_get_child (GTK_BIN (chooser));

	presence_chooser_entry_activate_cb (chooser, GTK_ENTRY (entry));

	return FALSE;
}

static gboolean
presence_chooser_entry_focus_out_cb (EmpathyPresenceChooser *chooser,
                                     GdkEventFocus *event,
				     GtkEntry *entry)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (chooser);

	if (priv->editing_status) {
		/* this seems a bit evil and maybe it will be fragile,
		 * someone should think of a better way to do it.
		 *
		 * The entry has focused out, but we don't know where the focus
		 * has gone. If it goes to the combo box, we don't want to
		 * do anything. If it's gone anywhere else, we want to commit
		 * the result.
		 *
		 * Thus we install this idle handler and store its source.
		 * If the source is scheduled when the popup handler runs,
		 * it will remove it, else the callback will commit the result.
		 */
		priv->focus_out_idle_source = g_idle_add (
			presence_chooser_entry_focus_out_idle_cb,
			chooser);
	}

	gtk_editable_set_position (GTK_EDITABLE (entry), 0);

	return FALSE;
}

static void
update_sensitivity_am_prepared_cb (GObject *source_object,
				   GAsyncResult *result,
				   gpointer user_data)
{
	TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
	EmpathyPresenceChooser *chooser = user_data;
	EmpathyPresenceChooserPriv *priv = GET_PRIV (chooser);
	gboolean sensitive = FALSE;
	GList *accounts, *l;
	GError *error = NULL;

	if (!tp_proxy_prepare_finish (manager, result, &error)) {
		DEBUG ("Failed to prepare account manager: %s", error->message);
		g_error_free (error);
		return;
	}

	accounts = tp_account_manager_get_valid_accounts (manager);

	for (l = accounts ; l != NULL ; l = g_list_next (l)) {
		TpAccount *a = TP_ACCOUNT (l->data);

		if (tp_account_is_enabled (a)) {
			sensitive = TRUE;
			break;
		}
	}

	g_list_free (accounts);

	if (!empathy_connectivity_is_online (priv->connectivity))
		sensitive = FALSE;

	gtk_widget_set_sensitive (GTK_WIDGET (chooser), sensitive);

	presence_chooser_presence_changed_cb (chooser);
}

static void
presence_chooser_update_sensitivity (EmpathyPresenceChooser *chooser)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (chooser);

	tp_proxy_prepare_async (priv->account_manager, NULL,
					  update_sensitivity_am_prepared_cb,
					  chooser);
}

static void
presence_chooser_account_manager_account_validity_changed_cb (
	TpAccountManager *manager,
	TpAccount *account,
	gboolean valid,
	EmpathyPresenceChooser *chooser)
{
	presence_chooser_update_sensitivity (chooser);
}

static void
presence_chooser_account_manager_account_changed_cb (
	TpAccountManager *manager,
	TpAccount *account,
	EmpathyPresenceChooser *chooser)
{
	presence_chooser_update_sensitivity (chooser);
}

static void
presence_chooser_connectivity_state_change (EmpathyConnectivity *connectivity,
					    gboolean new_online,
					    EmpathyPresenceChooser *chooser)
{
	presence_chooser_update_sensitivity (chooser);
}

/* Create a greyed version of the 'favorite' icon */
static GdkPixbuf *
create_not_favorite_pixbuf (void)
{
	GdkPixbuf *favorite, *result;

	favorite = empathy_pixbuf_from_icon_name ("emblem-favorite",
						  GTK_ICON_SIZE_MENU);

	if (favorite == NULL)
		return NULL;

	result = gdk_pixbuf_copy (favorite);
	gdk_pixbuf_saturate_and_pixelate (favorite, result, 1.0, TRUE);

	g_object_unref (favorite);
	return result;
}

static void
icon_theme_changed_cb (GtkIconTheme *icon_theme,
		       EmpathyPresenceChooser *self)
{
	EmpathyPresenceChooserPriv *priv = GET_PRIV (self);

	/* Theme has changed, recreate the not-favorite icon */
	if (priv->not_favorite_pixbuf != NULL)
		g_object_unref (priv->not_favorite_pixbuf);
	priv->not_favorite_pixbuf = create_not_favorite_pixbuf ();

	/* Update the icon */
	presence_chooser_set_favorite_icon (self);
}

static void
empathy_presence_chooser_init (EmpathyPresenceChooser *chooser)
{
	EmpathyPresenceChooserPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (chooser,
		EMPATHY_TYPE_PRESENCE_CHOOSER, EmpathyPresenceChooserPriv);

	chooser->priv = priv;

	/* Create the not-favorite icon */
	priv->not_favorite_pixbuf = create_not_favorite_pixbuf ();
}

static void
presence_chooser_constructed (GObject *object)
{
	EmpathyPresenceChooser *chooser = EMPATHY_PRESENCE_CHOOSER (object);
	EmpathyPresenceChooserPriv *priv = chooser->priv;
	GtkWidget *entry;
	GtkCellRenderer *renderer;
	const gchar *status_tooltip;

	tp_g_signal_connect_object (gtk_icon_theme_get_default (), "changed",
				     G_CALLBACK (icon_theme_changed_cb),
				     chooser, 0);

	presence_chooser_create_model (chooser);

	gtk_combo_box_set_entry_text_column (GTK_COMBO_BOX (chooser),
					     COL_STATUS_TEXT);
	gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (chooser),
					      combo_row_separator_func,
					      NULL, NULL);

	entry = gtk_bin_get_child (GTK_BIN (chooser));
	gtk_entry_set_icon_activatable (GTK_ENTRY (entry),
					GTK_ENTRY_ICON_PRIMARY,
					FALSE);

	g_signal_connect_swapped (entry, "icon-release",
		G_CALLBACK (presence_chooser_entry_icon_release_cb),
		chooser);
	g_signal_connect_swapped (entry, "activate",
		G_CALLBACK (presence_chooser_entry_activate_cb),
		chooser);
	g_signal_connect_swapped (entry, "key-press-event",
		G_CALLBACK (presence_chooser_entry_key_press_event_cb),
		chooser);
	g_signal_connect_swapped (entry, "button-press-event",
		G_CALLBACK (presence_chooser_entry_button_press_event_cb),
		chooser);

	gtk_cell_layout_clear (GTK_CELL_LAYOUT (chooser));

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (chooser), renderer, FALSE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (chooser), renderer,
					"icon-name", COL_STATE_ICON_NAME,
					NULL);
	g_object_set (renderer, "stock-size", GTK_ICON_SIZE_MENU, NULL);

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (chooser), renderer, TRUE);
	gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (chooser), renderer,
					"markup", COL_DISPLAY_MARKUP,
					NULL);
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

	g_signal_connect (chooser, "notify::popup-shown",
			G_CALLBACK (presence_chooser_popup_shown_cb), NULL);
	g_signal_connect (chooser, "changed",
			G_CALLBACK (presence_chooser_changed_cb), NULL);
	g_signal_connect_swapped (entry, "changed",
			G_CALLBACK (presence_chooser_entry_changed_cb),
			chooser);
	g_signal_connect_swapped (entry, "focus-out-event",
			G_CALLBACK (presence_chooser_entry_focus_out_cb),
			chooser);

	priv->presence_mgr = empathy_presence_manager_dup_singleton ();

	priv->account_manager = tp_account_manager_dup ();
	g_signal_connect_swapped (priv->account_manager,
		"most-available-presence-changed",
		G_CALLBACK (presence_chooser_presence_changed_cb),
		chooser);

	tp_g_signal_connect_object (priv->account_manager, "account-validity-changed",
		G_CALLBACK (presence_chooser_account_manager_account_validity_changed_cb),
		chooser, 0);
	tp_g_signal_connect_object (priv->account_manager, "account-removed",
		G_CALLBACK (presence_chooser_account_manager_account_changed_cb),
		chooser, 0);
	tp_g_signal_connect_object (priv->account_manager, "account-enabled",
		G_CALLBACK (presence_chooser_account_manager_account_changed_cb),
		chooser, 0);
	tp_g_signal_connect_object (priv->account_manager, "account-disabled",
		G_CALLBACK (presence_chooser_account_manager_account_changed_cb),
		chooser, 0);

	status_tooltip = gtk_entry_get_text (GTK_ENTRY (entry));
	gtk_widget_set_tooltip_text (GTK_WIDGET (chooser), status_tooltip);

	priv->connectivity = empathy_connectivity_dup_singleton ();
	tp_g_signal_connect_object (priv->connectivity,
		"state-change",
		G_CALLBACK (presence_chooser_connectivity_state_change),
		chooser, 0);

	presence_chooser_update_sensitivity (chooser);
}

static void
presence_chooser_finalize (GObject *object)
{
	EmpathyPresenceChooserPriv *priv;

	priv = GET_PRIV (object);

	if (priv->focus_out_idle_source) {
		g_source_remove (priv->focus_out_idle_source);
	}

	if (priv->account_manager != NULL)
		g_object_unref (priv->account_manager);

	g_signal_handlers_disconnect_by_func (priv->presence_mgr,
					      presence_chooser_presence_changed_cb,
					      object);
	g_object_unref (priv->presence_mgr);

	g_object_unref (priv->connectivity);
	if (priv->not_favorite_pixbuf != NULL)
		g_object_unref (priv->not_favorite_pixbuf);

	G_OBJECT_CLASS (empathy_presence_chooser_parent_class)->finalize (object);
}

/**
 * empathy_presence_chooser_new:
 *
 * Creates a new #EmpathyPresenceChooser widget.
 *
 * Return value: A new #EmpathyPresenceChooser widget
 */
GtkWidget *
empathy_presence_chooser_new (void)
{
	/* FIXME, why can't this go in init ()? */
	return g_object_new (EMPATHY_TYPE_PRESENCE_CHOOSER,
		"has-entry", TRUE,
		NULL);
}

static void
presence_chooser_presence_changed_cb (EmpathyPresenceChooser *chooser)
{
	EmpathyPresenceChooserPriv *priv;
	TpConnectionPresenceType    state;
	gchar                      *status;
	GtkTreeModel               *model;
	GtkTreeIter                 iter;
	gboolean valid, match_state = FALSE, match = FALSE;
	GtkWidget                  *entry;

	priv = GET_PRIV (chooser);

	if (priv->editing_status) {
		return;
	}

	state = get_state_and_status (chooser, &status);
	priv->state = state;

	/* An unset presence here doesn't make any sense. Force it to appear as
	 * offline. */
	if (state == TP_CONNECTION_PRESENCE_TYPE_UNSET) {
		state = TP_CONNECTION_PRESENCE_TYPE_OFFLINE;
	}

	/* look through the model and attempt to find a matching state */
	model = gtk_combo_box_get_model (GTK_COMBO_BOX (chooser));
	for (valid = gtk_tree_model_get_iter_first (model, &iter);
	     valid;
	     valid = gtk_tree_model_iter_next (model, &iter)) {
		int m_type;
		TpConnectionPresenceType m_state;
		char *m_status;

		gtk_tree_model_get (model, &iter,
				COL_STATE, &m_state,
				COL_TYPE, &m_type,
				-1);

		if (m_type == ENTRY_TYPE_CUSTOM ||
		    m_type == ENTRY_TYPE_SEPARATOR ||
		    m_type == ENTRY_TYPE_EDIT_CUSTOM) {
			continue;
		}
		else if (!match_state && state == m_state) {
			/* we are now in the section that can contain our
			 * match */
			match_state = TRUE;
		}
		else if (match_state && state != m_state) {
			/* we have passed the section that can contain our
			 * match */
			break;
		}

		gtk_tree_model_get (model, &iter,
				COL_STATUS_TEXT, &m_status,
				-1);

		match = !tp_strdiff (status, m_status);

		g_free (m_status);

		if (match) break;

	}

	if (match) {
		priv->block_changed++;
		gtk_combo_box_set_active_iter (GTK_COMBO_BOX (chooser), &iter);
		presence_chooser_set_favorite_icon (chooser);
		priv->block_changed--;
	}
	else {
		ui_set_custom_state (chooser, state, status);
	}

	entry = gtk_bin_get_child (GTK_BIN (chooser));
	gtk_entry_set_icon_from_icon_name (GTK_ENTRY (entry),
	      GTK_ENTRY_ICON_PRIMARY,
	      empathy_icon_name_for_presence (state));
	gtk_widget_set_tooltip_text (GTK_WIDGET (entry), status);

	entry = gtk_bin_get_child (GTK_BIN (chooser));
	gtk_editable_set_editable (GTK_EDITABLE (entry),
	    state != TP_CONNECTION_PRESENCE_TYPE_OFFLINE);

	g_free (status);
}

/**
 * empathy_presence_chooser_create_menu:
 *
 * Creates a new #GtkMenu allowing users to change their presence from a menu.
 *
 * Return value: a new #GtkMenu for changing presence in a menu.
 */
GtkWidget *
empathy_presence_chooser_create_menu (void)
{
	const gchar *status;
	GtkWidget   *menu;
	GtkWidget   *item;
	GtkWidget   *image;
	guint        i;

	menu = gtk_menu_new ();

	for (i = 0; states[i].state != TP_CONNECTION_PRESENCE_TYPE_UNSET; i++) {
		GList       *list, *l;

		status = empathy_presence_get_default_message (states[i].state);
		presence_chooser_menu_add_item (menu,
						status,
						states[i].state);

		if (states[i].customisable) {
			/* Set custom messages if wanted */
			list = empathy_status_presets_get (states[i].state, 5);
			for (l = list; l; l = l->next) {
				presence_chooser_menu_add_item (menu,
								l->data,
								states[i].state);
			}
			g_list_free (list);
		}

	}

	/* Separator */
	item = gtk_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	/* Custom messages */
	item = gtk_image_menu_item_new_with_label (_("Custom messages…"));
	image = gtk_image_new_from_stock (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (image);
	gtk_widget_show (item);

	g_signal_connect (item,
			  "activate",
			  G_CALLBACK (presence_chooser_custom_activate_cb),
			  NULL);

	return menu;
}

static void
presence_chooser_menu_add_item (GtkWidget   *menu,
				const gchar *str,
				TpConnectionPresenceType state)
{
	GtkWidget   *item;
	GtkWidget   *image;
	const gchar *icon_name;

	item = gtk_image_menu_item_new_with_label (str);
	icon_name = empathy_icon_name_for_presence (state);

	g_signal_connect (item, "activate",
			  G_CALLBACK (presence_chooser_noncustom_activate_cb),
			  NULL);

	image = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	gtk_widget_show (image);

	gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
	gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);
	gtk_widget_show (item);

	g_object_set_data_full (G_OBJECT (item),
				"status", g_strdup (str),
				(GDestroyNotify) g_free);

	g_object_set_data (G_OBJECT (item), "state", GINT_TO_POINTER (state));

	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
}

static void
presence_chooser_noncustom_activate_cb (GtkWidget *item,
					gpointer   user_data)
{
	TpConnectionPresenceType state;
	const gchar *status;

	status = g_object_get_data (G_OBJECT (item), "status");
	state = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (item), "state"));

	presence_chooser_set_state (state, status);
}

static void
presence_chooser_set_state (TpConnectionPresenceType state,
			    const gchar *status)
{
	EmpathyPresenceManager *presence_mgr;

	presence_mgr = empathy_presence_manager_dup_singleton ();
	empathy_presence_manager_set_presence (presence_mgr, state, status);
	g_object_unref (presence_mgr);
}

static void
presence_chooser_custom_activate_cb (GtkWidget *item,
				     gpointer   user_data)
{
	GtkWidget *dialog;

	dialog = empathy_status_preset_dialog_new (NULL);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

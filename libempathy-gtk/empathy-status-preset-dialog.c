/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * empathy-status-preset-dialog.c
 *
 * EmpathyStatusPresetDialog - a dialog for adding and removing preset status
 * messages.
 *
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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */
/**
 * SECTION:empathy-status-preset-dialog
 * @title: EmpathyStatusPresetDialog
 * @short_description: a dialog for editing the saved status messages
 * @include: libempathy-gtk/empathy-status-preset-dialog.h
 *
 * #EmpathyStatusPresetDialog is a dialog allowing the user to add/remove/edit
 * their saved status messages.
 */

#include "config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-status-presets.h>

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#include "empathy-ui-utils.h"
#include "empathy-status-preset-dialog.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyStatusPresetDialog)

G_DEFINE_TYPE (EmpathyStatusPresetDialog, empathy_status_preset_dialog, GTK_TYPE_DIALOG);

static TpConnectionPresenceType states[] = {
	TP_CONNECTION_PRESENCE_TYPE_AVAILABLE,
	TP_CONNECTION_PRESENCE_TYPE_BUSY,
	TP_CONNECTION_PRESENCE_TYPE_AWAY,
};

typedef struct _EmpathyStatusPresetDialogPriv EmpathyStatusPresetDialogPriv;
struct _EmpathyStatusPresetDialogPriv
{
	/* block status_preset_dialog_add_combo_changed () when > 0 */
	int block_add_combo_changed;

	GtkWidget *presets_treeview;
	GtkTreeViewColumn *column;
	GtkCellRenderer *text_cell;

	GtkTreeIter selected_iter;
	gboolean add_combo_changed;
	char *saved_status;
};

enum
{
	PRESETS_STORE_STATE,
	PRESETS_STORE_ICON_NAME,
	PRESETS_STORE_STATUS,
	PRESETS_STORE_N_COLS
};

enum
{
	ADD_COMBO_STATE,
	ADD_COMBO_ICON_NAME,
	ADD_COMBO_STATUS,
	ADD_COMBO_DEFAULT_TEXT,
	ADD_COMBO_N_COLS
};

static void
empathy_status_preset_dialog_finalize (GObject *self)
{
	EmpathyStatusPresetDialogPriv *priv = GET_PRIV (self);

	g_free (priv->saved_status);

	G_OBJECT_CLASS (empathy_status_preset_dialog_parent_class)->finalize (self);
}

static void
empathy_status_preset_dialog_class_init (EmpathyStatusPresetDialogClass *class)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS (class);
	gobject_class->finalize = empathy_status_preset_dialog_finalize;

	g_type_class_add_private (gobject_class,
			sizeof (EmpathyStatusPresetDialogPriv));
}

static void
status_preset_dialog_presets_update (EmpathyStatusPresetDialog *self)
{
	EmpathyStatusPresetDialogPriv *priv = GET_PRIV (self);
	GtkListStore *store;
	guint i;

	store = GTK_LIST_STORE (gtk_tree_view_get_model (
				GTK_TREE_VIEW (priv->presets_treeview)));

	gtk_list_store_clear (store);

	for (i = 0; i < G_N_ELEMENTS (states); i++) {
		GList *presets, *l;
		const char *icon_name;

		icon_name = empathy_icon_name_for_presence (states[i]);
		presets = empathy_status_presets_get (states[i], -1);
		presets = g_list_sort (presets, (GCompareFunc) g_utf8_collate);

		for (l = presets; l; l = l->next) {
			char *preset = (char *) l->data;

			gtk_list_store_insert_with_values (store,
					NULL, -1,
					PRESETS_STORE_STATE, states[i],
					PRESETS_STORE_ICON_NAME, icon_name,
					PRESETS_STORE_STATUS, preset,
					-1);
		}

		g_list_free (presets);
	}
}

static void
status_preset_dialog_status_edited (GtkCellRendererText *renderer,
				    char *path_str,
				    char *new_status,
				    EmpathyStatusPresetDialog *self)
{
	EmpathyStatusPresetDialogPriv *priv = GET_PRIV (self);
	GtkTreeModel *model;
	GtkTreePath *path;
	GtkTreeIter iter;
	TpConnectionPresenceType state;
	char *old_status;
	gboolean valid;

	if (strlen (new_status) == 0) {
		/* status is empty, ignore */
		return;
	}

	model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->presets_treeview));
	path = gtk_tree_path_new_from_string (path_str);
	valid = gtk_tree_model_get_iter (model, &iter, path);
	gtk_tree_path_free (path);

	if (!valid) return;

	gtk_tree_model_get (model, &iter,
			PRESETS_STORE_STATE, &state,
			PRESETS_STORE_STATUS, &old_status,
			-1);

	if (!strcmp (old_status, new_status)) {
		/* statuses are the same */
		g_free (old_status);
		return;
	}

	DEBUG ("EDITED STATUS (%s) -> (%s)\n", old_status, new_status);

	empathy_status_presets_remove (state, old_status);
	empathy_status_presets_set_last (state, new_status);

	g_free (old_status);

	status_preset_dialog_presets_update (self);
}

static void
status_preset_dialog_setup_presets_treeview (EmpathyStatusPresetDialog *self)
{
	EmpathyStatusPresetDialogPriv *priv = GET_PRIV (self);
	GtkWidget *treeview = priv->presets_treeview;
	GtkListStore *store;
	GtkTreeViewColumn *column;
	GtkCellRenderer *renderer;

	store = gtk_list_store_new (PRESETS_STORE_N_COLS,
			G_TYPE_UINT,		/* PRESETS_STORE_STATE */
			G_TYPE_STRING,		/* PRESETS_STORE_ICON_NAME */
			G_TYPE_STRING);		/* PRESETS_STORE_STATUS */

	gtk_tree_view_set_model (GTK_TREE_VIEW (treeview),
				 GTK_TREE_MODEL (store));
	g_object_unref (store);

	status_preset_dialog_presets_update (self);

	column = gtk_tree_view_column_new ();
	priv->column = column;
	gtk_tree_view_append_column (GTK_TREE_VIEW (treeview), column);

	renderer = gtk_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_add_attribute (column, renderer,
			"icon-name", PRESETS_STORE_ICON_NAME);

	renderer = gtk_cell_renderer_text_new ();
	priv->text_cell = renderer;
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_add_attribute (column, renderer,
			"text", PRESETS_STORE_STATUS);
	g_object_set (renderer,
			"editable", TRUE,
			NULL);
	g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);

	g_signal_connect (renderer, "edited",
			G_CALLBACK (status_preset_dialog_status_edited), self);
}

static void
status_preset_dialog_preset_selection_changed (GtkTreeSelection *selection,
					       GtkWidget *remove_button)
{
	/* update the sensitivity of the Remove button */
	gtk_widget_set_sensitive (remove_button,
			gtk_tree_selection_count_selected_rows (selection) != 0);
}

static void
foreach_removed_status (GtkTreeModel *model,
			GtkTreePath *path,
			GtkTreeIter *iter,
			gpointer data)
{
	TpConnectionPresenceType state;
	char *status;

	gtk_tree_model_get (model, iter,
			PRESETS_STORE_STATE, &state,
			PRESETS_STORE_STATUS, &status,
			-1);

	DEBUG ("REMOVE PRESET (%i, %s)\n", state, status);
	empathy_status_presets_remove (state, status);

	g_free (status);
}

static void
status_preset_dialog_preset_remove (GtkButton *button,
				    EmpathyStatusPresetDialog *self)
{
	EmpathyStatusPresetDialogPriv *priv = GET_PRIV (self);
	GtkTreeSelection *selection;

	selection = gtk_tree_view_get_selection (
			GTK_TREE_VIEW (priv->presets_treeview));
	gtk_tree_selection_selected_foreach (selection, foreach_removed_status, NULL);
	status_preset_dialog_presets_update (self);
}

static void
empathy_status_preset_dialog_init (EmpathyStatusPresetDialog *self)
{
	EmpathyStatusPresetDialogPriv *priv = self->priv =
		G_TYPE_INSTANCE_GET_PRIVATE (self,
			EMPATHY_TYPE_STATUS_PRESET_DIALOG,
			EmpathyStatusPresetDialogPriv);
	GtkBuilder *gui;
	GtkWidget *toplevel_vbox, *presets_sw, *remove_toolbar, *remove_button;
	GtkTreeSelection *selection;
	char *filename;
	GtkStyleContext *context;

	gtk_window_set_title (GTK_WINDOW (self),
			_("Edit Custom Messages"));
	gtk_dialog_add_button (GTK_DIALOG (self),
			GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);
	gtk_window_set_resizable (GTK_WINDOW (self), FALSE);

	filename = empathy_file_lookup ("empathy-status-preset-dialog.ui",
			"libempathy-gtk");
	gui = empathy_builder_get_file (filename,
			"toplevel-vbox", &toplevel_vbox,
			"presets-sw", &presets_sw,
			"presets-treeview", &priv->presets_treeview,
			"remove-toolbar", &remove_toolbar,
			"remove-button", &remove_button,
			NULL);
	g_free (filename);

	/* join the remove toolbar to the treeview */
	context = gtk_widget_get_style_context (presets_sw);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_BOTTOM);
	context = gtk_widget_get_style_context (remove_toolbar);
	gtk_style_context_set_junction_sides (context, GTK_JUNCTION_TOP);

	selection = gtk_tree_view_get_selection (
		GTK_TREE_VIEW (priv->presets_treeview));
	g_signal_connect (selection,
			"changed",
			G_CALLBACK (status_preset_dialog_preset_selection_changed),
			remove_button);
	gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

	empathy_builder_connect (gui, self,
			"remove-button", "clicked", status_preset_dialog_preset_remove,
			NULL);

	status_preset_dialog_setup_presets_treeview (self);

	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self))),
	    toplevel_vbox, TRUE, TRUE, 0);

	g_object_unref (gui);
}

/**
 * empathy_status_preset_dialog_new:
 * @parent: the parent window of this dialog (or NULL)
 *
 * Creates a new #EmpathyStatusPresetDialog that allows the user to
 * add/remove/edit their saved status messages.
 *
 * Returns: the newly constructed dialog.
 */
GtkWidget *
empathy_status_preset_dialog_new (GtkWindow *parent)
{
	GtkWidget *self = g_object_new (EMPATHY_TYPE_STATUS_PRESET_DIALOG,
			NULL);

	if (parent) {
		gtk_window_set_transient_for (GTK_WINDOW (self), parent);
	}

	return self;
}

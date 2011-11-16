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

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>

#include <folks/folks.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-contact-manager.h>

#include "empathy-groups-widget.h"
#include "empathy-ui-utils.h"

/**
 * SECTION:empathy-groups-widget
 * @title:EmpathyGroupsWidget
 * @short_description: A widget used to edit the groups of a #FolksGroupDetails
 * @include: libempathy-gtk/empathy-groups-widget.h
 *
 * #EmpathyGroupsWidget is a widget which lists the groups of a
 * #FolksGroupDetails (i.e. a #FolksPersona or a #FolksIndividual) and allows
 * them to be added and removed.
 */

/**
 * EmpathyGroupsWidget:
 * @parent: parent object
 *
 * Widget which displays and allows editing of the groups of a
 * #FolksGroupDetails (i.e. a #FolksPersona or #FolksIndividual).
 */

/* Delay before updating the widget when the id entry changed (seconds) */
#define ID_CHANGED_TIMEOUT 1

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyGroupsWidget)

typedef struct
{
  /* The object we're actually changing the groups of */
  FolksGroupDetails *group_details; /* owned */
  GtkListStore *group_store; /* owned */

  GtkWidget *add_group_entry; /* child widget */
  GtkWidget *add_group_button; /* child widget */
} EmpathyGroupsWidgetPriv;

enum {
  PROP_GROUP_DETAILS = 1,
};

enum {
  COL_NAME,
  COL_ENABLED,
  COL_EDITABLE
};
#define NUM_COLUMNS COL_EDITABLE + 1

G_DEFINE_TYPE (EmpathyGroupsWidget, empathy_groups_widget, GTK_TYPE_BOX);

typedef struct
{
  EmpathyGroupsWidget *widget;
  const gchar *name;
  gboolean found;
  GtkTreeIter found_iter;
} FindNameData;

static gboolean
model_find_name_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    FindNameData *data)
{
  gchar *name;

  gtk_tree_model_get (model, iter,
      COL_NAME, &name,
      -1);

  if (name != NULL && strcmp (data->name, name) == 0)
    {
      data->found = TRUE;
      data->found_iter = *iter;

      g_free (name);

      return TRUE;
    }

  g_free (name);

  return FALSE;
}

static gboolean
model_find_name (EmpathyGroupsWidget *self,
    const gchar *name,
    GtkTreeIter *iter)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  FindNameData data;

  if (EMP_STR_EMPTY (name))
    return FALSE;

  data.widget = self;
  data.name = name;
  data.found = FALSE;

  gtk_tree_model_foreach (GTK_TREE_MODEL (priv->group_store),
      (GtkTreeModelForeachFunc) model_find_name_foreach, &data);

  if (data.found == TRUE)
    {
      *iter = data.found_iter;
      return TRUE;
    }

  return FALSE;
}

static void
populate_data (EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  EmpathyContactManager *manager;
  GtkTreeIter iter;
  GeeSet *member_groups;
  GList *all_groups, *l;

  /* Remove the old groups */
  gtk_list_store_clear (priv->group_store);

  /* FIXME: We have to get the whole group list from EmpathyContactManager, as
   * libfolks hasn't grown API to get the whole group list yet. (bgo#627398) */
  manager = empathy_contact_manager_dup_singleton ();
  all_groups = empathy_contact_list_get_all_groups (
      EMPATHY_CONTACT_LIST (manager));
  g_object_unref (manager);

  /* Get the list of groups that this #FolksGroupDetails is currently in */
  member_groups = folks_group_details_get_groups (priv->group_details);

  for (l = all_groups; l != NULL; l = l->next)
    {
      const gchar *group_str = l->data;
      gboolean enabled;

      enabled = gee_collection_contains (GEE_COLLECTION (member_groups),
          group_str);

      gtk_list_store_append (priv->group_store, &iter);
      gtk_list_store_set (priv->group_store, &iter,
          COL_NAME, group_str,
          COL_EDITABLE, TRUE,
          COL_ENABLED, enabled,
          -1);

      g_free (l->data);
    }

  g_list_free (all_groups);
}

static void
add_group_entry_changed_cb (GtkEditable *editable,
    EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  GtkTreeIter iter;
  const gchar *group;

  group = gtk_entry_get_text (GTK_ENTRY (priv->add_group_entry));

  if (model_find_name (self, group, &iter))
    {
      gtk_widget_set_sensitive (GTK_WIDGET (priv->add_group_button), FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (GTK_WIDGET (priv->add_group_button),
          !EMP_STR_EMPTY (group));
    }
}

static void
add_group_entry_activate_cb (GtkEntry *entry,
    EmpathyGroupsWidget  *self)
{
  gtk_widget_activate (GTK_WIDGET (GET_PRIV (self)->add_group_button));
}

static void
change_group_cb (FolksGroupDetails *group_details,
    GAsyncResult *async_result,
    EmpathyGroupsWidget *self)
{
  GError *error = NULL;

  folks_group_details_change_group_finish (group_details, async_result, &error);

  if (error != NULL)
    {
      g_warning ("Failed to change group: %s", error->message);
      g_clear_error (&error);
    }
}

static void
add_group_button_clicked_cb (GtkButton *button,
   EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  GtkTreeIter iter;
  const gchar *group;

  group = gtk_entry_get_text (GTK_ENTRY (priv->add_group_entry));

  gtk_list_store_append (priv->group_store, &iter);
  gtk_list_store_set (priv->group_store, &iter,
      COL_NAME, group,
      COL_ENABLED, TRUE,
      -1);

  folks_group_details_change_group (priv->group_details, group, TRUE,
      (GAsyncReadyCallback) change_group_cb, self);
}

static void
cell_toggled_cb (GtkCellRendererToggle *cell,
    const gchar *path_string,
    EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  GtkTreePath *path;
  GtkTreeIter iter;
  gboolean was_enabled;
  gchar *group;

  path = gtk_tree_path_new_from_string (path_string);

  gtk_tree_model_get_iter (GTK_TREE_MODEL (priv->group_store), &iter,
      path);
  gtk_tree_model_get (GTK_TREE_MODEL (priv->group_store), &iter,
      COL_ENABLED, &was_enabled,
      COL_NAME, &group,
      -1);

  gtk_list_store_set (priv->group_store, &iter,
      COL_ENABLED, !was_enabled,
      -1);

  gtk_tree_path_free (path);

  if (group != NULL)
    {
      folks_group_details_change_group (priv->group_details, group,
          !was_enabled, (GAsyncReadyCallback) change_group_cb, self);
      g_free (group);
    }
}


static void
group_details_group_changed_cb (FolksGroupDetails *groups,
    const gchar *group,
    gboolean is_member,
    EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (self);
  GtkTreeIter iter;

  if (model_find_name (self, group, &iter) == TRUE)
    {
      gtk_list_store_set (priv->group_store, &iter,
          COL_ENABLED, is_member,
          -1);
    }
}

static void
set_up (EmpathyGroupsWidget *self)
{
  EmpathyGroupsWidgetPriv *priv;
  GtkWidget *label, *alignment;
  GtkBox *vbox, *hbox;
  GtkTreeView *tree_view;
  GtkTreeSelection *selection;
  GtkTreeViewColumn *column;
  GtkCellRenderer *renderer;
  guint col_offset;
  GtkScrolledWindow *scrolled_window;
  gchar *markup;

  priv = GET_PRIV (self);

  /* Set up ourself */
  gtk_orientable_set_orientation (GTK_ORIENTABLE (self),
      GTK_ORIENTATION_VERTICAL);
  gtk_box_set_spacing (GTK_BOX (self), 6);

  /* Create our child widgets */
  label = gtk_label_new (NULL);
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);

  markup = g_strdup_printf ("<b>%s</b>", _("Groups"));
  gtk_label_set_markup (GTK_LABEL (label), markup);
  g_free (markup);

  gtk_box_pack_start (GTK_BOX (self), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  alignment = gtk_alignment_new (0.5, 0.5, 1.0, 1.0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment), 0, 0, 12, 0);

  vbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_VERTICAL, 6));

  label = gtk_label_new (_("Select the groups you want this contact to appear "
      "in.  Note that you can select more than one group or no groups."));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);

  gtk_box_pack_start (vbox, label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  hbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12));

  priv->add_group_entry = gtk_entry_new ();
  g_signal_connect (priv->add_group_entry, "changed",
      (GCallback) add_group_entry_changed_cb, self);
  g_signal_connect (priv->add_group_entry, "activate",
      (GCallback) add_group_entry_activate_cb, self);

  gtk_box_pack_start (hbox, priv->add_group_entry, TRUE, TRUE, 0);
  gtk_widget_show (priv->add_group_entry);

  priv->add_group_button = gtk_button_new_with_mnemonic (_("_Add Group"));
  gtk_widget_set_sensitive (priv->add_group_button, FALSE);
  gtk_widget_set_receives_default (priv->add_group_button, TRUE);
  g_signal_connect (priv->add_group_button, "clicked",
      (GCallback) add_group_button_clicked_cb, self);

  gtk_box_pack_start (hbox, priv->add_group_button, FALSE, FALSE, 0);
  gtk_widget_show (priv->add_group_button);

  gtk_box_pack_start (vbox, GTK_WIDGET (hbox), FALSE, FALSE, 0);
  gtk_widget_show (GTK_WIDGET (hbox));

  scrolled_window = GTK_SCROLLED_WINDOW (gtk_scrolled_window_new (NULL, NULL));
  gtk_scrolled_window_set_policy (scrolled_window, GTK_POLICY_NEVER,
      GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (scrolled_window, GTK_SHADOW_IN);
  gtk_widget_set_size_request (GTK_WIDGET (scrolled_window), -1, 100);

  priv->group_store = gtk_list_store_new (NUM_COLUMNS,
      G_TYPE_STRING,   /* name */
      G_TYPE_BOOLEAN,  /* enabled */
      G_TYPE_BOOLEAN); /* editable */

  tree_view = GTK_TREE_VIEW (gtk_tree_view_new_with_model (
      GTK_TREE_MODEL (priv->group_store)));
  gtk_tree_view_set_headers_visible (tree_view, FALSE);
  gtk_tree_view_set_enable_search (tree_view, FALSE);

  selection = gtk_tree_view_get_selection (tree_view);
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

  renderer = gtk_cell_renderer_toggle_new ();
  g_signal_connect (renderer, "toggled", (GCallback) cell_toggled_cb, self);

  column = gtk_tree_view_column_new_with_attributes (
      C_("verb in a column header displaying group names", "Select"), renderer,
      "active", COL_ENABLED,
      NULL);

  gtk_tree_view_column_set_sizing (column, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (column, 50);
  gtk_tree_view_append_column (tree_view, column);

  renderer = gtk_cell_renderer_text_new ();
  col_offset = gtk_tree_view_insert_column_with_attributes (tree_view,
      -1, _("Group"),
      renderer,
      "text", COL_NAME,
      /* "editable", COL_EDITABLE, */
      NULL);

  column = gtk_tree_view_get_column (tree_view, col_offset - 1);
  gtk_tree_view_column_set_sort_column_id (column, COL_NAME);
  gtk_tree_view_column_set_resizable (column, FALSE);
  gtk_tree_view_column_set_clickable (GTK_TREE_VIEW_COLUMN (column), TRUE);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->group_store),
      COL_NAME, GTK_SORT_ASCENDING);

  gtk_container_add (GTK_CONTAINER (scrolled_window), GTK_WIDGET (tree_view));
  gtk_widget_show (GTK_WIDGET (tree_view));

  gtk_box_pack_start (vbox, GTK_WIDGET (scrolled_window), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (scrolled_window));

  gtk_container_add (GTK_CONTAINER (alignment), GTK_WIDGET (vbox));
  gtk_widget_show (GTK_WIDGET (vbox));

  gtk_box_pack_start (GTK_BOX (self), alignment, TRUE, TRUE, 0);
  gtk_widget_show (alignment);
}

static void
empathy_groups_widget_init (EmpathyGroupsWidget *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_GROUPS_WIDGET, EmpathyGroupsWidgetPriv);

  set_up (self);
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyGroupsWidgetPriv *priv;

  priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_GROUP_DETAILS:
        g_value_set_object (value, priv->group_details);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (param_id)
    {
      case PROP_GROUP_DETAILS:
        empathy_groups_widget_set_group_details (EMPATHY_GROUPS_WIDGET (object),
            g_value_get_object (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyGroupsWidgetPriv *priv = GET_PRIV (object);

  empathy_groups_widget_set_group_details (EMPATHY_GROUPS_WIDGET (object),
      NULL);
  tp_clear_object (&priv->group_store);

  G_OBJECT_CLASS (empathy_groups_widget_parent_class)->dispose (object);
}

static void
empathy_groups_widget_class_init (EmpathyGroupsWidgetClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;

  /**
   * EmpathyGroupsWidget:group_details:
   *
   * The #FolksGroupDetails whose group membership is to be edited by the
   * #EmpathyGroupsWidget.
   */
  g_object_class_install_property (object_class, PROP_GROUP_DETAILS,
      g_param_spec_object ("group-details",
          "Group Details",
          "The #FolksGroupDetails whose groups are being edited.",
          FOLKS_TYPE_GROUP_DETAILS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class, sizeof (EmpathyGroupsWidgetPriv));
}

/**
 * empathy_groups_widget_new:
 * @group_details: a #FolksGroupDetails, or %NULL
 *
 * Creates a new #EmpathyGroupsWidget to edit the groups of the given
 * @group_details.
 *
 * Return value: a new #EmpathyGroupsWidget
 */
GtkWidget *
empathy_groups_widget_new (FolksGroupDetails *group_details)
{
  g_return_val_if_fail (
      group_details == NULL || FOLKS_IS_GROUP_DETAILS (group_details),
      NULL);

  return GTK_WIDGET (g_object_new (EMPATHY_TYPE_GROUPS_WIDGET,
      "group-details", group_details,
      NULL));
}

/**
 * empathy_groups_widget_get_group_details:
 * @self: an #EmpathyGroupsWidget
 *
 * Get the #FolksGroupDetails whose group membership is being edited by the
 * #EmpathyGroupsWidget.
 *
 * Returns: the #FolksGroupDetails associated with @widget, or %NULL
 */
FolksGroupDetails *
empathy_groups_widget_get_group_details (EmpathyGroupsWidget *self)
{
  g_return_val_if_fail (EMPATHY_IS_GROUPS_WIDGET (self), NULL);

  return GET_PRIV (self)->group_details;
}

/**
 * empathy_groups_widget_set_group_details:
 * @self: an #EmpathyGroupsWidget
 * @group_details: the #FolksGroupDetails whose membership is to be edited, or
 * %NULL
 *
 * Change the #FolksGroupDetails whose group membership is to be edited by the
 * #EmpathyGroupsWidget.
 */
void
empathy_groups_widget_set_group_details (EmpathyGroupsWidget *self,
    FolksGroupDetails *group_details)
{
  EmpathyGroupsWidgetPriv *priv;

  g_return_if_fail (EMPATHY_IS_GROUPS_WIDGET (self));
  g_return_if_fail (
      group_details == NULL || FOLKS_IS_GROUP_DETAILS (group_details));

  priv = GET_PRIV (self);

  if (group_details == priv->group_details)
    return;

  if (priv->group_details != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->group_details,
          group_details_group_changed_cb, self);
    }

  tp_clear_object (&priv->group_details);

  if (group_details != NULL)
    {
      priv->group_details = g_object_ref (group_details);

      g_signal_connect (priv->group_details, "group-changed",
          (GCallback) group_details_group_changed_cb, self);

      populate_data (self);
    }

  g_object_notify (G_OBJECT (self), "group-details");
}

/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include "config.h"

#include "empathy-account-selector-dialog.h"

enum
{
  PROP_ACCOUNTS = 1
};

struct _EmpathyAccountSelectorDialogPrivate
{
  GList *accounts;

  GtkWidget *treeview;
  GtkListStore *model;
};

enum
{
  COL_ACCOUNT,
  COL_ICON,
  COL_NAME,
  NUM_COL
};

G_DEFINE_TYPE (EmpathyAccountSelectorDialog, empathy_account_selector_dialog, \
    GTK_TYPE_DIALOG)

static void
empathy_account_selector_dialog_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountSelectorDialog *self = (EmpathyAccountSelectorDialog *) object;

  switch (property_id)
    {
      case PROP_ACCOUNTS:
        {
          GList *list;

          list = g_value_get_pointer (value);

          self->priv->accounts = g_list_copy (list);
          g_list_foreach (self->priv->accounts, (GFunc) g_object_ref, NULL);
          break;
        }
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_account_selector_dialog_constructed (GObject *obj)
{
  EmpathyAccountSelectorDialog *self = (EmpathyAccountSelectorDialog *) obj;
  GList *l;

  for (l = self->priv->accounts; l != NULL; l = g_list_next (l))
    {
      TpAccount *account = l->data;

      gtk_list_store_insert_with_values (GTK_LIST_STORE (self->priv->model),
          NULL, -1,
          COL_ACCOUNT, account,
          COL_ICON, tp_account_get_icon_name (account),
          COL_NAME, tp_account_get_display_name (account),
          -1);
    }

  G_OBJECT_CLASS (empathy_account_selector_dialog_parent_class)->constructed (
      obj);
}

static void
empathy_account_selector_dialog_dispose (GObject *obj)
{
  EmpathyAccountSelectorDialog *self = (EmpathyAccountSelectorDialog *) obj;

  g_list_free_full (self->priv->accounts, g_object_unref);
  self->priv->accounts = NULL;

  tp_clear_object (&self->priv->model);

  G_OBJECT_CLASS (empathy_account_selector_dialog_parent_class)->dispose (obj);
}

static void
empathy_account_selector_dialog_class_init (
    EmpathyAccountSelectorDialogClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *spec;

  oclass->set_property = empathy_account_selector_dialog_set_property;
  oclass->constructed = empathy_account_selector_dialog_constructed;
  oclass->dispose = empathy_account_selector_dialog_dispose;

  spec = g_param_spec_pointer ("accounts", "accounts", "GList of TpAccount",
      G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY | G_PARAM_WRITABLE);
  g_object_class_install_property (oclass, PROP_ACCOUNTS, spec);

  g_type_class_add_private (klass,
      sizeof (EmpathyAccountSelectorDialogPrivate));
}

static void
empathy_account_selector_dialog_init (EmpathyAccountSelectorDialog *self)
{
  GtkWidget *box;
  GtkCellRenderer *cell;
  GtkTreeViewColumn *column;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self),
      EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG,
      EmpathyAccountSelectorDialogPrivate);

  self->priv->model = gtk_list_store_new (NUM_COL,
      TP_TYPE_ACCOUNT,  /* account */
      G_TYPE_STRING,    /* icon name */
      G_TYPE_STRING);   /* name */

  /* Create treeview */
  self->priv->treeview = gtk_tree_view_new_with_model (
      GTK_TREE_MODEL (self->priv->model));

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (self->priv->treeview),
      FALSE);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (GTK_TREE_VIEW (self->priv->treeview), column);

  /* icon */
  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell, "icon-name", COL_ICON);

  /* text */
  cell = gtk_cell_renderer_text_new ();

  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", COL_NAME);

  box = gtk_dialog_get_content_area (GTK_DIALOG (self));
  gtk_box_pack_start (GTK_BOX (box), self->priv->treeview, TRUE, TRUE, 0);

  gtk_widget_show (self->priv->treeview);
}

GtkWidget *
empathy_account_selector_dialog_new (GList *accounts)
{
  return g_object_new (EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG,
      "accounts", accounts,
      NULL);
}

TpAccount *
empathy_account_selector_dialog_dup_selected (
    EmpathyAccountSelectorDialog *self)
{
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;
  TpAccount *account;

  selection = gtk_tree_view_get_selection (
      GTK_TREE_VIEW (self->priv->treeview));

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT, &account, -1);

  return account;
}

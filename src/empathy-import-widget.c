/*
 * Copyright (C) 2008-2009 Collabora Ltd.
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
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-import-widget.c */

#include <config.h>

#include "empathy-import-dialog.h"
#include "empathy-import-widget.h"
#include "empathy-import-pidgin.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-ui-utils.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <glib/gi18n-lib.h>

G_DEFINE_TYPE (EmpathyImportWidget, empathy_import_widget, G_TYPE_OBJECT)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyImportWidget)

enum
{
  COL_IMPORT = 0,
  COL_PROTOCOL,
  COL_NAME,
  COL_SOURCE,
  COL_ACCOUNT_DATA,
  COL_COUNT
};

enum {
  PROP_APPLICATION_ID = 1
};

typedef struct {
  GtkWidget *vbox;
  GtkWidget *treeview;

  GList *accounts;
  EmpathyImportApplication app_id;

  EmpathyConnectionManagers *cms;

  gboolean dispose_run;
} EmpathyImportWidgetPriv;

static gboolean
import_widget_account_id_in_list (GList *accounts,
    const gchar *account_id)
{
  GList *l;

  for (l = accounts; l; l = l->next)
    {
      TpAccount *account = l->data;
      const GHashTable *parameters;

      parameters = tp_account_get_parameters (account);

      if (!tp_strdiff (tp_asv_get_string (parameters, "account"), account_id))
        return TRUE;
    }

  return FALSE;
}

static void
account_manager_prepared_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  EmpathyImportWidget *self = user_data;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GList *l;
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  for (l = priv->accounts; l; l = l->next)
    {
      GValue *value;
      EmpathyImportAccountData *data = l->data;
      gboolean import;
      GList *accounts;
      TpConnectionManager *cm = NULL;

      if (!empathy_import_protocol_is_supported (data->protocol, &cm))
        continue;

      data->connection_manager = g_strdup (
          tp_connection_manager_get_name (cm));

      value = g_hash_table_lookup (data->settings, "account");

      accounts = tp_account_manager_get_valid_accounts (manager);

      /* Only set the "Import" cell to be active if there isn't already an
       * account set up with the same account id. */
      import = !import_widget_account_id_in_list (accounts,
          g_value_get_string (value));

      g_list_free (accounts);

      gtk_list_store_append (GTK_LIST_STORE (model), &iter);

      gtk_list_store_set (GTK_LIST_STORE (model), &iter,
          COL_IMPORT, import,
          COL_PROTOCOL, data->protocol,
          COL_NAME, g_value_get_string (value),
          COL_SOURCE, data->source,
          COL_ACCOUNT_DATA, data,
          -1);
    }
}

static void
import_widget_add_accounts_to_model (EmpathyImportWidget *self)
{
  TpAccountManager *manager;

  manager = tp_account_manager_dup ();

  tp_proxy_prepare_async (manager, NULL,
      account_manager_prepared_cb, self);

  g_object_unref (manager);
}

static void
import_widget_create_account_cb (GObject *source,
  GAsyncResult *result,
  gpointer user_data)
{
  TpAccountManager *account_manager;
  TpAccount *account;
  GError *error = NULL;
  EmpathyImportWidget *self = user_data;

  account = tp_account_manager_create_account_finish (
    TP_ACCOUNT_MANAGER (source), result, &error);

  if (account == NULL)
    {
      DEBUG ("Failed to create account: %s",
          error ? error->message : "No error given");
      g_clear_error (&error);
      return;
    }

  DEBUG ("account created\n");

  if (tp_account_is_enabled (account))
    {
      account_manager = tp_account_manager_dup ();
      empathy_connect_new_account (account, account_manager);
      g_object_unref (account_manager);
    }

  g_object_unref (self);
}

static void
import_widget_add_account (EmpathyImportWidget *self,
    EmpathyImportAccountData *data)
{
  TpAccountManager *account_manager;
  gchar *display_name;
  GHashTable *properties;
  GValue *username;

  account_manager = tp_account_manager_dup ();

  DEBUG ("connection_manager: %s\n", data->connection_manager);

  /* Set the display name of the account */
  username = g_hash_table_lookup (data->settings, "account");
  display_name = g_strdup_printf ("%s (%s)",
      data->protocol,
      g_value_get_string (username));

  DEBUG ("display name: %s\n", display_name);

  properties = tp_asv_new (NULL, NULL);
  tp_asv_set_boolean (properties, TP_IFACE_ACCOUNT ".Enabled", data->enabled);

  tp_account_manager_create_account_async (account_manager,
      (const gchar*) data->connection_manager, data->protocol, display_name,
      data->settings, properties, import_widget_create_account_cb,
      g_object_ref (self));

  g_hash_table_unref (properties);
  g_free (display_name);
  g_object_unref (account_manager);
}

static gboolean
import_widget_tree_model_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer user_data)
{
  gboolean to_import;
  EmpathyImportAccountData *data;
  EmpathyImportWidget *self = user_data;

  gtk_tree_model_get (model, iter,
      COL_IMPORT, &to_import,
      COL_ACCOUNT_DATA, &data,
      -1);

  if (to_import)
    import_widget_add_account (self, data);

  return FALSE;
}

static void
import_widget_cell_toggled_cb (GtkCellRendererToggle *cell_renderer,
    const gchar *path_str,
    EmpathyImportWidget *self)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);

  path = gtk_tree_path_new_from_string (path_str);
  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));

  gtk_tree_model_get_iter (model, &iter, path);

  gtk_list_store_set (GTK_LIST_STORE (model), &iter,
      COL_IMPORT, !gtk_cell_renderer_toggle_get_active (cell_renderer),
      -1);

  gtk_tree_path_free (path);
}

static void
import_widget_set_up_account_list (EmpathyImportWidget *self)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);
  GtkListStore *store;
  GtkTreeView *view;
  GtkTreeViewColumn *column;
  GtkCellRenderer *cell;

  priv->accounts = empathy_import_accounts_load (priv->app_id);

  store = gtk_list_store_new (COL_COUNT, G_TYPE_BOOLEAN, G_TYPE_STRING,
      G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->treeview),
      GTK_TREE_MODEL (store));

  g_object_unref (store);

  view = GTK_TREE_VIEW (priv->treeview);
  gtk_tree_view_set_headers_visible (view, TRUE);

  /* Import column */
  cell = gtk_cell_renderer_toggle_new ();
  gtk_tree_view_insert_column_with_attributes (view, -1,
      /* Translators: this is the header of a treeview column */
      _("Import"), cell,
      "active", COL_IMPORT,
      NULL);

  g_signal_connect (cell, "toggled",
      G_CALLBACK (import_widget_cell_toggled_cb), self);

  /* Protocol column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Protocol"));
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (view, column);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "editable", FALSE, NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", COL_PROTOCOL);

  /* Account column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Account"));
  gtk_tree_view_column_set_expand (column, TRUE);
  gtk_tree_view_append_column (view, column);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "editable", FALSE, NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell, "text", COL_NAME);

  if (priv->app_id == EMPATHY_IMPORT_APPLICATION_ALL)
    {
      /* Source column */
      column = gtk_tree_view_column_new ();
      gtk_tree_view_column_set_title (column, _("Source"));
      gtk_tree_view_column_set_expand (column, TRUE);
      gtk_tree_view_append_column (view, column);

      cell = gtk_cell_renderer_text_new ();
      g_object_set (cell, "editable", FALSE, NULL);
      gtk_tree_view_column_pack_start (column, cell, TRUE);
      gtk_tree_view_column_add_attribute (column, cell, "text", COL_SOURCE);
    }

  import_widget_add_accounts_to_model (self);
}

static void
import_widget_cms_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyImportWidget *self = user_data;

  if (!empathy_connection_managers_prepare_finish (
        EMPATHY_CONNECTION_MANAGERS (source), result, NULL))
    return;

  import_widget_set_up_account_list (self);
}

static void
import_widget_destroy_cb (GtkWidget *w,
    EmpathyImportWidget *self)
{
  g_object_unref (self);
}

static void
do_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_APPLICATION_ID:
      g_value_set_int (value, priv->app_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_APPLICATION_ID:
      priv->app_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_finalize (GObject *obj)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (obj);

  g_list_foreach (priv->accounts, (GFunc) empathy_import_account_data_free,
      NULL);
  g_list_free (priv->accounts);

  if (G_OBJECT_CLASS (empathy_import_widget_parent_class)->finalize != NULL)
    G_OBJECT_CLASS (empathy_import_widget_parent_class)->finalize (obj);
}

static void
do_dispose (GObject *obj)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (obj);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->cms != NULL)
    {
      g_object_unref (priv->cms);
      priv->cms = NULL;
    }

  if (G_OBJECT_CLASS (empathy_import_widget_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_import_widget_parent_class)->dispose (obj);
}

static void
do_constructed (GObject *obj)
{
  EmpathyImportWidget *self = EMPATHY_IMPORT_WIDGET (obj);
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);
  GtkBuilder *gui;
  gchar *filename;

  filename = empathy_file_lookup ("empathy-import-dialog.ui", "src");
  gui = empathy_builder_get_file (filename,
      "widget_vbox", &priv->vbox,
      "treeview", &priv->treeview,
      NULL);

  g_free (filename);
  empathy_builder_unref_and_keep_widget (gui, priv->vbox);

  g_signal_connect (priv->vbox, "destroy",
      G_CALLBACK (import_widget_destroy_cb), self);

  empathy_connection_managers_prepare_async (priv->cms,
      import_widget_cms_prepare_cb, self);
}

static void
empathy_import_widget_class_init (EmpathyImportWidgetClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->constructed = do_constructed;
  oclass->finalize = do_finalize;
  oclass->dispose = do_dispose;
  oclass->set_property = do_set_property;
  oclass->get_property = do_get_property;

  param_spec = g_param_spec_int ("application-id",
      "application-id", "The application id to import from",
      0, EMPATHY_IMPORT_APPLICATION_INVALID, EMPATHY_IMPORT_APPLICATION_ALL,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_APPLICATION_ID, param_spec);

  g_type_class_add_private (klass, sizeof (EmpathyImportWidgetPriv));
}

static void
empathy_import_widget_init (EmpathyImportWidget *self)
{
  EmpathyImportWidgetPriv *priv =
    G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_IMPORT_WIDGET,
        EmpathyImportWidgetPriv);

  self->priv = priv;

  priv->cms = empathy_connection_managers_dup_singleton ();
}

EmpathyImportWidget *
empathy_import_widget_new (EmpathyImportApplication id)
{
  return g_object_new (EMPATHY_TYPE_IMPORT_WIDGET, "application-id", id, NULL);
}

GtkWidget *
empathy_import_widget_get_widget (EmpathyImportWidget *self)
{
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);

  return priv->vbox;
}

void
empathy_import_widget_add_selected_accounts (EmpathyImportWidget *self)
{
  GtkTreeModel *model;
  EmpathyImportWidgetPriv *priv = GET_PRIV (self);

  model = gtk_tree_view_get_model (GTK_TREE_VIEW (priv->treeview));
  gtk_tree_model_foreach (model, import_widget_tree_model_foreach, self);
}

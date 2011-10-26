/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>

#include "empathy-ui-utils.h"
#include "empathy-account-chooser.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

/**
 * SECTION:empathy-account-chooser
 * @title:EmpathyAccountChooser
 * @short_description: A widget used to choose from a list of accounts
 * @include: libempathy-gtk/empathy-account-chooser.h
 *
 * #EmpathyAccountChooser is a widget which extends #GtkComboBox to provide
 * a chooser of available accounts.
 */

/**
 * EmpathyAccountChooser:
 * @parent: parent object
 *
 * Widget which extends #GtkComboBox to provide a chooser of available accounts.
 */

struct _EmpathyAccountChooserPriv
{
  TpAccountManager *manager;
  gboolean set_active_item;
  gboolean account_manually_set;
  gboolean has_all_option;
  EmpathyAccountChooserFilterFunc filter;
  gpointer filter_data;
  gboolean ready;

  TpAccount *select_when_ready;
};

typedef struct
{
  EmpathyAccountChooser *self;
  TpAccount *account;
  gboolean set;
} SetAccountData;

typedef struct
{
  EmpathyAccountChooser *self;
  TpAccount *account;
  GtkTreeIter *iter;
} FilterResultCallbackData;

static FilterResultCallbackData *
filter_result_callback_data_new (EmpathyAccountChooser *self,
    TpAccount *account,
    GtkTreeIter *iter)
{
  FilterResultCallbackData *data;

  data = g_slice_new0 (FilterResultCallbackData);
  data->self = g_object_ref (self);
  data->account = g_object_ref (account);
  data->iter = gtk_tree_iter_copy (iter);

  return data;
}

static void
filter_result_callback_data_free (FilterResultCallbackData *data)
{
  g_object_unref (data->self);
  g_object_unref (data->account);
  gtk_tree_iter_free (data->iter);
  g_slice_free (FilterResultCallbackData, data);
}

/* Distinguishes between store entries which are actually accounts, and special
 * items like the "All" entry and the separator below it, so they can be sorted
 * correctly. Higher-numbered entries will sort earlier.
 */
typedef enum {
  ROW_ACCOUNT = 0,
  ROW_SEPARATOR,
  ROW_ALL
} RowType;

enum {
  COL_ACCOUNT_IMAGE,
  COL_ACCOUNT_TEXT,
  COL_ACCOUNT_ENABLED, /* Usually tied to connected state */
  COL_ACCOUNT_ROW_TYPE,
  COL_ACCOUNT_POINTER,
  COL_ACCOUNT_COUNT
};

static void account_chooser_account_validity_changed_cb (
    TpAccountManager *manager,
    TpAccount *account,
    gboolean valid,
    EmpathyAccountChooser *self);
static void account_chooser_account_add_foreach (TpAccount *account,
    EmpathyAccountChooser *self);
static void account_chooser_account_removed_cb (TpAccountManager *manager,
    TpAccount *account,
    EmpathyAccountChooser *self);
static void account_chooser_account_remove_foreach (TpAccount *account,
    EmpathyAccountChooser*self);
static void account_chooser_update_iter (EmpathyAccountChooser *self,
    GtkTreeIter *iter);
static void account_chooser_status_changed_cb (TpAccount *account,
    guint old_status,
    guint new_status,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    gpointer user_data);
static gboolean account_chooser_separator_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyAccountChooser *self);
static gboolean account_chooser_set_account_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    SetAccountData *data);
static void update_account (EmpathyAccountChooser *self,
    TpAccount *account);
static gboolean select_account (EmpathyAccountChooser *self,
    TpAccount *account);

enum {
  PROP_0,
  PROP_HAS_ALL_OPTION,
};

enum {
  READY,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (EmpathyAccountChooser, empathy_account_chooser,
    GTK_TYPE_COMBO_BOX)

static void
empathy_account_chooser_init (EmpathyAccountChooser *self)
{
  TpSimpleClientFactory *factory;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
    EMPATHY_TYPE_ACCOUNT_CHOOSER, EmpathyAccountChooserPriv);

  self->priv->set_active_item = FALSE;
  self->priv->account_manually_set = FALSE;
  self->priv->filter = NULL;
  self->priv->filter_data = NULL;

  self->priv->manager = tp_account_manager_dup ();

  tp_g_signal_connect_object (self->priv->manager, "account-validity-changed",
      G_CALLBACK (account_chooser_account_validity_changed_cb), self, 0);

  tp_g_signal_connect_object (self->priv->manager, "account-removed",
      G_CALLBACK (account_chooser_account_removed_cb), self, 0);

  /* Make sure we'll have the capabilities feature on TpAccount's connection */
  factory = tp_proxy_get_factory (self->priv->manager);

  tp_simple_client_factory_add_account_features_varargs (factory,
      TP_ACCOUNT_FEATURE_CONNECTION, NULL);
  tp_simple_client_factory_add_connection_features_varargs (factory,
      TP_CONNECTION_FEATURE_CAPABILITIES, NULL);
}

static gint
account_cmp (GtkTreeModel *model,
    GtkTreeIter *a,
    GtkTreeIter *b,
    gpointer user_data)
{
  RowType a_type, b_type;
  gboolean a_enabled, b_enabled;
  gchar *a_text, *b_text;
  gint result;

  gtk_tree_model_get (model, a,
      COL_ACCOUNT_ENABLED, &a_enabled,
      COL_ACCOUNT_ROW_TYPE, &a_type,
      -1);
  gtk_tree_model_get (model, b,
      COL_ACCOUNT_ENABLED, &b_enabled,
      COL_ACCOUNT_ROW_TYPE, &b_type,
      -1);

  /* This assumes that we have at most one of each special row type. */
  if (a_type != b_type)
    /* Display higher-numbered special row types first. */
    return (b_type - a_type);

  /* Enabled accounts are displayed first */
  if (a_enabled != b_enabled)
    return a_enabled ? -1: 1;

  gtk_tree_model_get (model, a, COL_ACCOUNT_TEXT, &a_text, -1);
  gtk_tree_model_get (model, b, COL_ACCOUNT_TEXT, &b_text, -1);

  if (a_text == b_text)
    result = 0;
  else if (a_text == NULL)
    result = 1;
  else if (b_text == NULL)
    result = -1;
  else
    result = g_ascii_strcasecmp (a_text, b_text);

  g_free (a_text);
  g_free (b_text);

  return result;
}

static void
account_connection_notify_cb (TpAccount *account,
    GParamSpec *spec,
    EmpathyAccountChooser *self)
{
  update_account (self, account);
}

static void
account_manager_prepared_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  GList *accounts, *l;
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  EmpathyAccountChooser *self = user_data;
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  accounts = tp_account_manager_get_valid_accounts (manager);

  for (l = accounts; l != NULL; l = l->next)
    {
      TpAccount *account = l->data;

      account_chooser_account_add_foreach (account, self);

      tp_g_signal_connect_object (account, "status-changed",
          G_CALLBACK (account_chooser_status_changed_cb),
          self, 0);

      /* We generally use the TpConnection from the account to filter it so,
       * just relying on the account status is not enough. In some case we the
       * status change can be notified while the TpConnection is still
       * preparing. */
      tp_g_signal_connect_object (account, "notify::connection",
          G_CALLBACK (account_connection_notify_cb),
          self, 0);
    }

  g_list_free (accounts);

  if (self->priv->select_when_ready != NULL)
    {
      select_account (self, self->priv->select_when_ready);

      g_clear_object (&self->priv->select_when_ready);
    }

  self->priv->ready = TRUE;
  g_signal_emit (self, signals[READY], 0);
}

static void
account_chooser_constructed (GObject *object)
{
  EmpathyAccountChooser *self = (EmpathyAccountChooser *) object;
  GtkListStore *store;
  GtkCellRenderer *renderer;
  GtkComboBox *combobox;

  /* Set up combo box with new store */
  combobox = GTK_COMBO_BOX (self);

  gtk_cell_layout_clear (GTK_CELL_LAYOUT (combobox));

  store = gtk_list_store_new (COL_ACCOUNT_COUNT,
      GDK_TYPE_PIXBUF,  /* Image */
      G_TYPE_STRING,    /* Name */
      G_TYPE_BOOLEAN,   /* Enabled */
      G_TYPE_UINT,      /* Row type */
      TP_TYPE_ACCOUNT);

  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (store),
    account_cmp, self, NULL);
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (store),
    GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, GTK_SORT_ASCENDING);

  gtk_combo_box_set_model (combobox, GTK_TREE_MODEL (store));

  renderer = gtk_cell_renderer_pixbuf_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, FALSE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
      "pixbuf", COL_ACCOUNT_IMAGE,
      "sensitive", COL_ACCOUNT_ENABLED,
      NULL);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combobox), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (combobox), renderer,
      "text", COL_ACCOUNT_TEXT,
      "sensitive", COL_ACCOUNT_ENABLED,
      NULL);

  /* Populate accounts */
  tp_proxy_prepare_async (self->priv->manager, NULL,
      account_manager_prepared_cb, self);

  g_object_unref (store);

}

static void
account_chooser_dispose (GObject *object)
{
  EmpathyAccountChooser *self = EMPATHY_ACCOUNT_CHOOSER (object);

  g_clear_object (&self->priv->manager);
  g_clear_object (&self->priv->select_when_ready);

  G_OBJECT_CLASS (empathy_account_chooser_parent_class)->dispose (object);
}

static void
account_chooser_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountChooser *self = (EmpathyAccountChooser *) object;

  switch (param_id)
    {
      case PROP_HAS_ALL_OPTION:
        g_value_set_boolean (value, self->priv->has_all_option);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
account_chooser_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  switch (param_id)
    {
      case PROP_HAS_ALL_OPTION:
        empathy_account_chooser_set_has_all_option (
            EMPATHY_ACCOUNT_CHOOSER (object), g_value_get_boolean (value));
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    };
}

static void
empathy_account_chooser_class_init (EmpathyAccountChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = account_chooser_constructed;
  object_class->dispose = account_chooser_dispose;
  object_class->get_property = account_chooser_get_property;
  object_class->set_property = account_chooser_set_property;

  /**
   * EmpathyAccountChooser:has-all-option:
   *
   * Have an additional option in the list to mean all accounts.
   */
  g_object_class_install_property (object_class,
      PROP_HAS_ALL_OPTION,
      g_param_spec_boolean ("has-all-option",
        "Has All Option",
        "Have a separate option in the list to mean ALL accounts",
        FALSE,
        G_PARAM_READWRITE));

  signals[READY] =
    g_signal_new ("ready",
        G_OBJECT_CLASS_TYPE (object_class),
        G_SIGNAL_RUN_LAST,
        0,
        NULL, NULL,
        g_cclosure_marshal_generic,
        G_TYPE_NONE,
        0);

  g_type_class_add_private (object_class, sizeof (EmpathyAccountChooserPriv));
}

/**
 * empathy_account_chooser_new:
 *
 * Creates a new #EmpathyAccountChooser.
 *
 * Return value: A new #EmpathyAccountChooser
 */
GtkWidget *
empathy_account_chooser_new (void)
{
  GtkWidget *self;

  self = g_object_new (EMPATHY_TYPE_ACCOUNT_CHOOSER, NULL);

  return self;
}

gboolean
empathy_account_chooser_has_all_selected (EmpathyAccountChooser *self)
{
  GtkTreeModel *model;
  GtkTreeIter iter;
  RowType type;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self), FALSE);

  g_return_val_if_fail (self->priv->has_all_option == TRUE, FALSE);

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (self));
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self), &iter))
    return FALSE;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT_ROW_TYPE, &type, -1);

  return type == ROW_ALL;
}

/**
 * empathy_account_chooser_dup_account:
 * @self: an #EmpathyAccountChooser
 *
 * Returns the account which is currently selected in the chooser or %NULL
 * if there is no account selected. The #TpAccount returned should be
 * unrefed with g_object_unref() when finished with.
 *
 * Return value: a new ref to the #TpAccount currently selected, or %NULL.
 */
TpAccount *
empathy_account_chooser_dup_account (EmpathyAccountChooser *self)
{
  TpAccount *account;
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self), NULL);

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (self));
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (self), &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_ACCOUNT_POINTER, &account, -1);

  return account;
}

/**
 * empathy_account_chooser_get_connection:
 * @self: an #EmpathyAccountChooser
 *
 * Returns a borrowed reference to the #TpConnection associated with the
 * account currently selected. The caller must reference the returned object
 * with g_object_ref() if it will be kept
 *
 * Return value: a borrowed reference to the #TpConnection associated with the
 * account curently selected.
 */
TpConnection *
empathy_account_chooser_get_connection (EmpathyAccountChooser *self)
{
  TpAccount *account;
  TpConnection *connection;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self), NULL);

  account = empathy_account_chooser_dup_account (self);

  /* if the returned account is NULL, then the account manager probably
   * hasn't been prepared yet. It should be safe to return NULL here
   * though. */
  if (account == NULL)
    return NULL;

  connection = tp_account_get_connection (account);
  g_object_unref (account);

  return connection;
}

static gboolean
select_account (EmpathyAccountChooser *self,
    TpAccount *account)
{
  GtkComboBox *combobox;
  GtkTreeModel *model;
  GtkTreeIter iter;
  SetAccountData data;

  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self), FALSE);

  combobox = GTK_COMBO_BOX (self);
  model = gtk_combo_box_get_model (combobox);
  gtk_combo_box_get_active_iter (combobox, &iter);

  data.self = self;
  data.account = account;
  data.set = FALSE;

  gtk_tree_model_foreach (model,
      (GtkTreeModelForeachFunc) account_chooser_set_account_foreach,
      &data);

  self->priv->account_manually_set = data.set;

  return data.set;
}

/**
 * empathy_account_chooser_set_account:
 * @self: an #EmpathyAccountChooser
 * @account: a #TpAccount
 *
 * Sets the currently selected account to @account, if it exists in the list.
 *
 * Return value: whether the chooser was set to @account.
 */
gboolean
empathy_account_chooser_set_account (EmpathyAccountChooser *self,
    TpAccount *account)
{
  if (self->priv->ready)
    return select_account (self, account);

  /* Account chooser is not ready yet, we'll try selecting the account once it
   * is */
  g_clear_object (&self->priv->select_when_ready);

  if (account != NULL)
    self->priv->select_when_ready = g_object_ref (account);

  return FALSE;
}

void
empathy_account_chooser_set_all (EmpathyAccountChooser *self)
{
  GtkComboBox *combobox;
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_return_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self));

  g_return_if_fail (self->priv->has_all_option);

  combobox = GTK_COMBO_BOX (self);
  model = gtk_combo_box_get_model (combobox);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      /* 'All accounts' is the first row */
      gtk_combo_box_set_active_iter (combobox, &iter);
      self->priv->account_manually_set = TRUE;
    }
}

/**
 * empathy_account_chooser_get_has_all_option:
 * @self: an #EmpathyAccountChooser
 *
 * Returns whether @self has the #EmpathyAccountChooser:has-all-option
 * property set to true.
 *
 * Return value: whether @self has the #EmpathyAccountChooser:has-all-option
 * property enabled.
 */
gboolean
empathy_account_chooser_get_has_all_option (EmpathyAccountChooser *self)
{
  g_return_val_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self), FALSE);

  return self->priv->has_all_option;
}

/**
 * empathy_account_chooser_set_has_all_option:
 * @self: an #EmpathyAccountChooser
 * @has_all_option: a new value for the #EmpathyAccountChooser:has-all-option
 * property
 *
 * Sets the #EmpathyAccountChooser:has-all-option property.
 */
void
empathy_account_chooser_set_has_all_option (EmpathyAccountChooser *self,
    gboolean has_all_option)
{
  GtkComboBox *combobox;
  GtkListStore *store;
  GtkTreeModel *model;
  GtkTreeIter iter;

  g_return_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self));

  if (self->priv->has_all_option == has_all_option)
    return;

  combobox = GTK_COMBO_BOX (self);
  model = gtk_combo_box_get_model (combobox);
  store = GTK_LIST_STORE (model);

  self->priv->has_all_option = has_all_option;

  /*
   * The first 2 options are the ALL and separator
   */

  if (has_all_option)
    {
      gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (self),
          (GtkTreeViewRowSeparatorFunc)
          account_chooser_separator_func,
          self,
          NULL);

      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_ACCOUNT_TEXT, NULL,
          COL_ACCOUNT_ENABLED, TRUE,
          COL_ACCOUNT_POINTER, NULL,
          COL_ACCOUNT_ROW_TYPE, ROW_SEPARATOR,
          -1);

      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_ACCOUNT_TEXT, _("All accounts"),
          COL_ACCOUNT_ENABLED, TRUE,
          COL_ACCOUNT_POINTER, NULL,
          COL_ACCOUNT_ROW_TYPE, ROW_ALL,
          -1);
    }
  else
    {
      if (gtk_tree_model_get_iter_first (model, &iter))
        {
          if (gtk_list_store_remove (GTK_LIST_STORE (model), &iter))
            gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
        }

    gtk_combo_box_set_row_separator_func (GTK_COMBO_BOX (self),
        (GtkTreeViewRowSeparatorFunc)
        NULL,
        NULL,
        NULL);
  }

  g_object_notify (G_OBJECT (self), "has-all-option");
}

static void
account_chooser_account_validity_changed_cb (TpAccountManager *manager,
    TpAccount *account,
    gboolean valid,
    EmpathyAccountChooser *self)
{
  if (valid)
    account_chooser_account_add_foreach (account, self);
  else
    account_chooser_account_remove_foreach (account, self);
}

static void
account_chooser_account_add_foreach (TpAccount *account,
    EmpathyAccountChooser *self)
{
  GtkListStore *store;
  GtkComboBox *combobox;
  GtkTreeIter iter;
  gint position;

  combobox = GTK_COMBO_BOX (self);
  store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

  position = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (store), NULL);
  gtk_list_store_insert_with_values (store, &iter, position,
      COL_ACCOUNT_POINTER, account,
      -1);

  account_chooser_update_iter (self, &iter);
}

static void
account_chooser_account_removed_cb (TpAccountManager *manager,
    TpAccount *account,
    EmpathyAccountChooser *self)
{
  account_chooser_account_remove_foreach (account, self);
}

typedef struct
{
  TpAccount *account;
  GtkTreeIter *iter;
  gboolean found;
} FindAccountData;

static gboolean
account_chooser_find_account_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer user_data)
{
  FindAccountData *data = user_data;
  TpAccount *account;
  RowType type;

  gtk_tree_model_get (model, iter,
    COL_ACCOUNT_POINTER, &account,
    COL_ACCOUNT_ROW_TYPE, &type,
     -1);

  if (type != ROW_ACCOUNT)
    return FALSE;

  if (account == data->account)
    {
      data->found = TRUE;
      *(data->iter) = *iter;
      g_object_unref (account);

      return TRUE;
    }

  g_object_unref (account);

  return FALSE;
}

static gboolean
account_chooser_find_account (EmpathyAccountChooser *self,
    TpAccount *account,
    GtkTreeIter*iter)
{
  GtkListStore *store;
  GtkComboBox *combobox;
  FindAccountData data;

  combobox = GTK_COMBO_BOX (self);
  store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

  data.account = account;
  data.iter = iter;
  gtk_tree_model_foreach (GTK_TREE_MODEL (store),
        account_chooser_find_account_foreach,
        &data);

  return data.found;
}

static void
account_chooser_account_remove_foreach (TpAccount *account,
    EmpathyAccountChooser *self)
{
  GtkListStore *store;
  GtkComboBox *combobox;
  GtkTreeIter iter;

  combobox = GTK_COMBO_BOX (self);
  store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

  if (account_chooser_find_account (self, account, &iter))
    gtk_list_store_remove (store, &iter);
}

static void
account_chooser_filter_ready_cb (gboolean is_enabled,
    gpointer data)
{
  FilterResultCallbackData *fr_data = data;
  EmpathyAccountChooser *self;
  TpAccount *account;
  GtkTreeIter *iter;
  GtkListStore *store;
  GtkComboBox *combobox;
  const gchar *icon_name;
  GdkPixbuf *pixbuf;

  self = fr_data->self;
  account = fr_data->account;
  iter = fr_data->iter;
  combobox = GTK_COMBO_BOX (self);
  store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

  icon_name = tp_account_get_icon_name (account);
  pixbuf = empathy_pixbuf_from_icon_name (icon_name,
    GTK_ICON_SIZE_BUTTON);

  gtk_list_store_set (store, iter,
          COL_ACCOUNT_IMAGE, pixbuf,
          COL_ACCOUNT_TEXT, tp_account_get_display_name (account),
          COL_ACCOUNT_ENABLED, is_enabled,
          -1);

  if (pixbuf != NULL)
    g_object_unref (pixbuf);

  /* set first connected account as active account */
  if (self->priv->account_manually_set == FALSE &&
      self->priv->set_active_item == FALSE && is_enabled)
    {
      self->priv->set_active_item = TRUE;
      gtk_combo_box_set_active_iter (combobox, iter);
    }

  filter_result_callback_data_free (fr_data);
}

static void
account_chooser_update_iter (EmpathyAccountChooser *self,
    GtkTreeIter *iter)
{
  GtkListStore *store;
  GtkComboBox *combobox;
  TpAccount *account;
  FilterResultCallbackData *data;

  combobox = GTK_COMBO_BOX (self);
  store = GTK_LIST_STORE (gtk_combo_box_get_model (combobox));

  gtk_tree_model_get (GTK_TREE_MODEL (store), iter,
          COL_ACCOUNT_POINTER, &account,
          -1);

  /* Skip rows without account associated */
  if (account == NULL)
    return;

  data = filter_result_callback_data_new (self, account, iter);

  if (self->priv->filter)
    self->priv->filter (account, account_chooser_filter_ready_cb,
            (gpointer) data, self->priv->filter_data);
  else
    account_chooser_filter_ready_cb (TRUE, (gpointer) data);

  g_object_unref (account);
}

static void
update_account (EmpathyAccountChooser *self,
    TpAccount *account)
{
  GtkTreeIter iter;

  if (account_chooser_find_account (self, account, &iter))
    account_chooser_update_iter (self, &iter);
}

static void
account_chooser_status_changed_cb (TpAccount *account,
    guint old_status,
    guint new_status,
    guint reason,
    gchar *dbus_error_name,
    GHashTable *details,
    gpointer user_data)
{
  EmpathyAccountChooser *self = user_data;

  update_account (self, account);
}

static gboolean
account_chooser_separator_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    EmpathyAccountChooser *self)
{
  RowType row_type;

  gtk_tree_model_get (model, iter, COL_ACCOUNT_ROW_TYPE, &row_type, -1);
  return (row_type == ROW_SEPARATOR);
}

static gboolean
account_chooser_set_account_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    SetAccountData *data)
{
  TpAccount *account;
  gboolean equal;

  gtk_tree_model_get (model, iter, COL_ACCOUNT_POINTER, &account, -1);

  equal = (data->account == account);

  if (account)
    g_object_unref (account);

  if (equal)
    {
      GtkComboBox *combobox;

      combobox = GTK_COMBO_BOX (data->self);
      gtk_combo_box_set_active_iter (combobox, iter);

      data->set = TRUE;
    }

  return equal;
}

static gboolean
account_chooser_filter_foreach (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer self)
{
  account_chooser_update_iter (self, iter);
  return FALSE;
}

void
empathy_account_chooser_refilter (EmpathyAccountChooser *self)
{
  GtkTreeModel *model;

  self->priv->set_active_item = FALSE;
  model = gtk_combo_box_get_model (GTK_COMBO_BOX (self));
  gtk_tree_model_foreach (model, account_chooser_filter_foreach, self);
}

/**
 * empathy_account_chooser_set_filter:
 * @self: an #EmpathyAccountChooser
 * @filter: a filter
 * @user_data: data to pass to @filter, or %NULL
 *
 * Sets a filter on the @self so only accounts that are %TRUE in the eyes
 * of the filter are visible in the @self.
 */
void
empathy_account_chooser_set_filter (EmpathyAccountChooser *self,
    EmpathyAccountChooserFilterFunc filter,
    gpointer user_data)
{
  g_return_if_fail (EMPATHY_IS_ACCOUNT_CHOOSER (self));

  self->priv->filter = filter;
  self->priv->filter_data = user_data;

  /* Refilter existing data */
  empathy_account_chooser_refilter (self);
}

/**
 * EmpathyAccountChooserFilterFunc:
 * @account: a #TpAccount
 * @user_data: user data, or %NULL
 *
 * A function which decides whether the account indicated by @account
 * is visible.
 *
 * Return value: whether the account indicated by @account is visible.
 */

gboolean
empathy_account_chooser_is_ready (EmpathyAccountChooser *self)
{
  return self->priv->ready;
}

TpAccount *
empathy_account_chooser_get_account (EmpathyAccountChooser *self)
{
  TpAccount *account;

  account = empathy_account_chooser_dup_account (self);
  if (account == NULL)
    return NULL;

  g_object_unref (account);

  return account;
}

TpAccountManager *
empathy_account_chooser_get_account_manager (EmpathyAccountChooser *self)
{
  return self->priv->manager;
}

/* Pre-defined filters */

/**
 * empathy_account_chooser_filter_is_connected:
 * @account: a #TpAccount
 * @callback: an #EmpathyAccountChooserFilterResultCallback accepting the result
 * @callback_data: data passed to the @callback
 * @user_data: user data or %NULL
 *
 * A useful #EmpathyAccountChooserFilterFunc that one could pass into
 * empathy_account_chooser_set_filter() and only show connected accounts.
 *
 * Returns (via the callback) TRUE is @account is connected
 */
void
empathy_account_chooser_filter_is_connected (TpAccount *account,
  EmpathyAccountChooserFilterResultCallback callback,
  gpointer callback_data,
  gpointer user_data)
{
  gboolean is_connected =
    tp_account_get_connection_status (account, NULL)
    == TP_CONNECTION_STATUS_CONNECTED;

  callback (is_connected, callback_data);
}

/**
 * empathy_account_chooser_filter_supports_multichat:
 * @account: a #TpAccount
 * @callback: an #EmpathyAccountChooserFilterResultCallback accepting the result
 * @callback_data: data passed to the @callback
 * @user_data: user data or %NULL
 *
 * An #EmpathyAccountChooserFilterFunc that returns accounts that both
 * support multiuser text chat and are connected.
 *
 * Returns (via the callback) TRUE if @account both supports muc and
 * is connected
 */
void
empathy_account_chooser_filter_supports_chatrooms (TpAccount *account,
  EmpathyAccountChooserFilterResultCallback callback,
  gpointer callback_data,
  gpointer user_data)
{
  TpConnection *connection;
  gboolean supported = FALSE;
  TpCapabilities *caps;

  /* check if CM supports multiuser text chat */
  connection = tp_account_get_connection (account);
  if (connection == NULL)
    goto out;

  caps = tp_connection_get_capabilities (connection);
  if (caps == NULL)
    goto out;

  supported = tp_capabilities_supports_text_chatrooms (caps);

out:
  callback (supported, callback_data);
}

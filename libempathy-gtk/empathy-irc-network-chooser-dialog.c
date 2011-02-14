/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <gdesmott@gnome.org>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-irc-network-manager.h>

#include "empathy-irc-network-dialog.h"
#include "empathy-ui-utils.h"
#include "empathy-live-search.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT | EMPATHY_DEBUG_IRC
#include <libempathy/empathy-debug.h>

#include "empathy-irc-network-chooser-dialog.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIrcNetworkChooserDialog)

enum {
    PROP_SETTINGS = 1,
    PROP_NETWORK
};

enum {
	RESPONSE_RESET = 0
};

typedef struct {
    EmpathyAccountSettings *settings;
    EmpathyIrcNetwork *network;

    EmpathyIrcNetworkManager *network_manager;
    gboolean changed;

    GtkWidget *treeview;
    GtkListStore *store;
    GtkTreeModelFilter *filter;
    GtkWidget *search;
    GtkWidget *select_button;

    gulong search_sig;
    gulong activate_sig;
} EmpathyIrcNetworkChooserDialogPriv;

enum {
  COL_NETWORK_OBJ,
  COL_NETWORK_NAME,
};

G_DEFINE_TYPE (EmpathyIrcNetworkChooserDialog, empathy_irc_network_chooser_dialog,
    GTK_TYPE_DIALOG);

static void
empathy_irc_network_chooser_dialog_set_property (GObject *object,
    guint prop_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SETTINGS:
        priv->settings = g_value_dup_object (value);
        break;
      case PROP_NETWORK:
        priv->network = g_value_dup_object (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
empathy_irc_network_chooser_dialog_get_property (GObject *object,
    guint prop_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (object);

  switch (prop_id)
    {
      case PROP_SETTINGS:
        g_value_set_object (value, priv->settings);
        break;
      case PROP_NETWORK:
        g_value_set_object (value, priv->network);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

/* The iter returned by *it is a priv->store iter (not a filter one) */
static EmpathyIrcNetwork *
dup_selected_network (EmpathyIrcNetworkChooserDialog *self,
    GtkTreeIter *it)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  EmpathyIrcNetwork *network;
  GtkTreeSelection *selection;
  GtkTreeIter iter;
  GtkTreeModel *model;

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (priv->treeview));
  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return NULL;

  gtk_tree_model_get (model, &iter, COL_NETWORK_OBJ, &network, -1);
  g_assert (network != NULL);

  if (it != NULL)
    {
      gtk_tree_model_filter_convert_iter_to_child_iter (priv->filter, it,
          &iter);
    }

  return network;
}

static void
treeview_changed_cb (GtkTreeView *treeview,
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  EmpathyIrcNetwork *network;

  network = dup_selected_network (self, NULL);
  if (network == priv->network)
    {
      g_object_unref (network);
      return;
    }

  tp_clear_object (&priv->network);
  /* Transfer the reference */
  priv->network = network;

  priv->changed = TRUE;
}

/* Take a filter iterator as argument */
static void
scroll_to_iter (EmpathyIrcNetworkChooserDialog *self,
    GtkTreeIter *filter_iter)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GtkTreePath *path;

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->filter), filter_iter);

  if (path != NULL)
    {
      gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (priv->treeview),
          path, NULL, FALSE, 0, 0);

      gtk_tree_path_free (path);
    }
}

/* Take a filter iterator as argument */
static void
select_iter (EmpathyIrcNetworkChooserDialog *self,
    GtkTreeIter *filter_iter,
    gboolean emulate_changed)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GtkTreeSelection *selection;
  GtkTreePath *path;

  /* Select the network */
  selection = gtk_tree_view_get_selection (
      GTK_TREE_VIEW (priv->treeview));

  gtk_tree_selection_select_iter (selection, filter_iter);

  path = gtk_tree_model_get_path (GTK_TREE_MODEL (priv->filter), filter_iter);
  if (path != NULL)
    {
      gtk_tree_view_set_cursor (GTK_TREE_VIEW (priv->treeview), path,
          NULL, FALSE);

      gtk_tree_path_free (path);
    }

  /* Scroll to the selected network */
  scroll_to_iter (self, filter_iter);

  if (emulate_changed)
    {
      /* gtk_tree_selection_select_iter doesn't fire the 'cursor-changed' signal
       * so we call the callback manually. */
      treeview_changed_cb (GTK_TREE_VIEW (priv->treeview), self);
    }
}

static GtkTreeIter
iter_to_filter_iter (EmpathyIrcNetworkChooserDialog *self,
    GtkTreeIter *iter)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GtkTreeIter filter_iter;

  g_assert (gtk_tree_model_filter_convert_child_iter_to_iter (priv->filter,
        &filter_iter, iter));

  return filter_iter;
}

static void
fill_store (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GSList *networks, *l;

  networks = empathy_irc_network_manager_get_networks (
      priv->network_manager);

  for (l = networks; l != NULL; l = g_slist_next (l))
    {
      EmpathyIrcNetwork *network = l->data;
      GtkTreeIter iter;

      gtk_list_store_insert_with_values (priv->store, &iter, -1,
          COL_NETWORK_OBJ, network,
          COL_NETWORK_NAME, empathy_irc_network_get_name (network),
          -1);

      if (network == priv->network)
        {
          GtkTreeIter filter_iter = iter_to_filter_iter (self, &iter);

          select_iter (self, &filter_iter, FALSE);
        }

      g_object_unref (network);
    }

  g_slist_free (networks);
}

static void
irc_network_dialog_destroy_cb (GtkWidget *widget,
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  EmpathyIrcNetwork *network;
  GtkTreeIter iter, filter_iter;

  priv->changed = TRUE;

  network = dup_selected_network (self, &iter);
  if (network == NULL)
    return;

  /* name could be changed */
  gtk_list_store_set (GTK_LIST_STORE (priv->store), &iter,
      COL_NETWORK_NAME, empathy_irc_network_get_name (network), -1);

  filter_iter = iter_to_filter_iter (self, &iter);
  scroll_to_iter (self, &filter_iter);

  gtk_widget_grab_focus (priv->treeview);

  g_object_unref (network);
}

static void
display_irc_network_dialog (EmpathyIrcNetworkChooserDialog *self,
    EmpathyIrcNetwork *network)
{
  GtkWidget *dialog;

  dialog = empathy_irc_network_dialog_show (network, NULL);

  g_signal_connect (dialog, "destroy",
      G_CALLBACK (irc_network_dialog_destroy_cb), self);
}

static void
edit_network (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetwork *network;

  network = dup_selected_network (self, NULL);
  if (network == NULL)
    return;

  display_irc_network_dialog (self, network);

  g_object_unref (network);
}

static void
add_network (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  EmpathyIrcNetwork *network;
  GtkTreeIter iter, filter_iter;

  gtk_widget_hide (priv->search);

  network = empathy_irc_network_new (_("New Network"));
  empathy_irc_network_manager_add (priv->network_manager, network);

  gtk_list_store_insert_with_values (priv->store, &iter, -1,
      COL_NETWORK_OBJ, network,
      COL_NETWORK_NAME, empathy_irc_network_get_name (network),
      -1);

  filter_iter = iter_to_filter_iter (self, &iter);
  select_iter (self, &filter_iter, TRUE);

  display_irc_network_dialog (self, network);

  g_object_unref (network);
}

static void
remove_network (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  EmpathyIrcNetwork *network;
  GtkTreeIter iter;

  network = dup_selected_network (self, &iter);
  if (network == NULL)
    return;

  /* Hide the search after picking the network to get the right one */
  gtk_widget_hide (priv->search);

  DEBUG ("Remove network %s", empathy_irc_network_get_name (network));

  /* Delete network and select next network */
  if (gtk_list_store_remove (priv->store, &iter))
    {
      GtkTreeIter filter_iter = iter_to_filter_iter (self, &iter);

      select_iter (self, &filter_iter, TRUE);
    }
  else
    {
      /* this should only happen if the last network was deleted */
      GtkTreeIter last, filter_iter;
      gint n_elements;

      n_elements = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (priv->store),
          NULL);

      if (n_elements > 0)
        {
          gtk_tree_model_iter_nth_child (GTK_TREE_MODEL (priv->store), &last,
              NULL, (n_elements-1));
          filter_iter = iter_to_filter_iter (self, &last);

          select_iter (self, &filter_iter, TRUE);
        }
    }

  empathy_irc_network_manager_remove (priv->network_manager, network);
  gtk_widget_grab_focus (priv->treeview);

  g_object_unref (network);
}

static void
reset_networks (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GSList *networks, *l;

  networks = empathy_irc_network_manager_get_dropped_networks (
      priv->network_manager);

  for (l = networks; l != NULL; l = g_slist_next (l))
    {
      EmpathyIrcNetwork *network;
      GtkTreeIter iter;

      network = EMPATHY_IRC_NETWORK (l->data);
      empathy_irc_network_activate (network);

      gtk_list_store_insert_with_values (priv->store, &iter, -1,
          COL_NETWORK_OBJ, network,
          COL_NETWORK_NAME, empathy_irc_network_get_name (network),
          -1);
    }

  g_slist_foreach (networks, (GFunc) g_object_unref, NULL);
}

static void
dialog_response_cb (GtkDialog *dialog,
    gint response,
    EmpathyIrcNetworkChooserDialog *self)
{
  if (response == GTK_RESPONSE_OK)
    add_network (self);
  else if (response == GTK_RESPONSE_APPLY)
    edit_network (self);
  else if (response == GTK_RESPONSE_REJECT)
    remove_network (self);
  else if (response == RESPONSE_RESET)
    reset_networks (self);
}

static gboolean
filter_visible_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer user_data)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (user_data);
  EmpathyIrcNetwork *network;
  gboolean visible;

  gtk_tree_model_get (model, iter, COL_NETWORK_OBJ, &network, -1);

  visible = empathy_live_search_match (EMPATHY_LIVE_SEARCH (priv->search),
      empathy_irc_network_get_name (network));

  g_object_unref (network);
  return visible;
}

static void
search_activate_cb (GtkWidget *search,
  EmpathyIrcNetworkChooserDialog *self)
{
  gtk_widget_hide (search);
  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_CLOSE);
}

static void
search_text_notify_cb (EmpathyLiveSearch *search,
    GParamSpec *pspec,
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GtkTreeIter filter_iter;
  gboolean sensitive = FALSE;

  gtk_tree_model_filter_refilter (priv->filter);

  /* Is there at least one network in the view ? */
  if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (priv->filter),
        &filter_iter))
    {
      const gchar *text;

      text = empathy_live_search_get_text (EMPATHY_LIVE_SEARCH (priv->search));
      if (!EMP_STR_EMPTY (text))
        {
          /* We are doing a search, select the first matching network */
          select_iter (self, &filter_iter, TRUE);
        }
      else
        {
          /* Search has been cancelled. Scroll to the selected network */
          GtkTreeSelection *selection;

          selection = gtk_tree_view_get_selection (
              GTK_TREE_VIEW (priv->treeview));

          if (gtk_tree_selection_get_selected (selection, NULL, &filter_iter))
            scroll_to_iter (self, &filter_iter);
        }

      sensitive = TRUE;
    }

  gtk_widget_set_sensitive (priv->select_button, sensitive);
}

static void
dialog_destroy_cb (GtkWidget *widget,
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);

  g_signal_handler_disconnect (priv->search, priv->search_sig);
  g_signal_handler_disconnect (priv->search, priv->activate_sig);
}

static void
empathy_irc_network_chooser_dialog_constructed (GObject *object)
{
  EmpathyIrcNetworkChooserDialog *self = (EmpathyIrcNetworkChooserDialog *) object;
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);
  GtkDialog *dialog = GTK_DIALOG (self);
  GtkCellRenderer *renderer;
  GtkWidget *vbox;
  GtkTreeViewColumn *column;
  GtkWidget *scroll;

  g_assert (priv->settings != NULL);

  gtk_window_set_title (GTK_WINDOW (self), _("Choose an IRC network"));

  /* Create store and treeview */
  priv->store = gtk_list_store_new (2, G_TYPE_OBJECT, G_TYPE_STRING);

  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (priv->store),
      COL_NETWORK_NAME,
      GTK_SORT_ASCENDING);

  priv->treeview = gtk_tree_view_new ();
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (priv->treeview), FALSE);
  gtk_tree_view_set_enable_search (GTK_TREE_VIEW (priv->treeview), FALSE);

  column = gtk_tree_view_column_new ();
  gtk_tree_view_append_column (GTK_TREE_VIEW (priv->treeview), column);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (column), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (column),
      renderer,
      "text", COL_NETWORK_NAME,
      NULL);

  /* add the treeview in a GtkScrolledWindow */
  vbox = gtk_dialog_get_content_area (dialog);

  scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
      GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

  gtk_container_add (GTK_CONTAINER (scroll), priv->treeview);
  gtk_box_pack_start (GTK_BOX (vbox), scroll, TRUE, TRUE, 6);

  /* Live search */
  priv->search = empathy_live_search_new (priv->treeview);

  gtk_box_pack_start (GTK_BOX (vbox), priv->search, FALSE, TRUE, 0);

  priv->filter = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (
          GTK_TREE_MODEL (priv->store), NULL));
  gtk_tree_model_filter_set_visible_func (priv->filter,
          filter_visible_func, self, NULL);

  gtk_tree_view_set_model (GTK_TREE_VIEW (priv->treeview),
          GTK_TREE_MODEL (priv->filter));

  priv->search_sig = g_signal_connect (priv->search, "notify::text",
      G_CALLBACK (search_text_notify_cb), self);

  priv->activate_sig = g_signal_connect (priv->search, "activate",
      G_CALLBACK (search_activate_cb), self);

  /* Add buttons */
  gtk_dialog_add_buttons (dialog,
      GTK_STOCK_ADD, GTK_RESPONSE_OK,
      GTK_STOCK_EDIT, GTK_RESPONSE_APPLY,
      GTK_STOCK_REMOVE, GTK_RESPONSE_REJECT,
      _("Reset _Networks List"), RESPONSE_RESET,
      NULL);

  priv->select_button = gtk_dialog_add_button (dialog,
      C_("verb displayed on a button to select an IRC network", "Select"),
      GTK_RESPONSE_CLOSE);

  fill_store (self);

  g_signal_connect (priv->treeview, "cursor-changed",
      G_CALLBACK (treeview_changed_cb), self);

  g_signal_connect (self, "response",
      G_CALLBACK (dialog_response_cb), self);
  g_signal_connect (self, "destroy",
      G_CALLBACK (dialog_destroy_cb), self);

  /* Request a side ensuring to display at least some networks */
  gtk_widget_set_size_request (GTK_WIDGET (self), -1, 300);

  gtk_window_set_modal (GTK_WINDOW (self), TRUE);
}

static void
empathy_irc_network_chooser_dialog_dispose (GObject *object)
{
  EmpathyIrcNetworkManager *self = (EmpathyIrcNetworkManager *) object;
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);

  tp_clear_object (&priv->settings);
  tp_clear_object (&priv->network);
  tp_clear_object (&priv->network_manager);
  tp_clear_object (&priv->store);
  tp_clear_object (&priv->filter);

  if (G_OBJECT_CLASS (empathy_irc_network_chooser_dialog_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_irc_network_chooser_dialog_parent_class)->dispose (object);
}

static void
empathy_irc_network_chooser_dialog_class_init (EmpathyIrcNetworkChooserDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = empathy_irc_network_chooser_dialog_get_property;
  object_class->set_property = empathy_irc_network_chooser_dialog_set_property;
  object_class->constructed = empathy_irc_network_chooser_dialog_constructed;
  object_class->dispose = empathy_irc_network_chooser_dialog_dispose;

  g_object_class_install_property (object_class, PROP_SETTINGS,
    g_param_spec_object ("settings",
      "Settings",
      "The EmpathyAccountSettings to show and edit",
      EMPATHY_TYPE_ACCOUNT_SETTINGS,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_object_class_install_property (object_class, PROP_NETWORK,
    g_param_spec_object ("network",
      "Network",
      "The EmpathyIrcNetwork selected in the treeview",
      EMPATHY_TYPE_IRC_NETWORK,
      G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

  g_type_class_add_private (object_class,
      sizeof (EmpathyIrcNetworkChooserDialogPriv));
}

static void
empathy_irc_network_chooser_dialog_init (EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG, EmpathyIrcNetworkChooserDialogPriv);
  self->priv = priv;

  priv->network_manager = empathy_irc_network_manager_dup_default ();
}

GtkWidget *
empathy_irc_network_chooser_dialog_new (EmpathyAccountSettings *settings,
    EmpathyIrcNetwork *network,
    GtkWindow *parent)
{
  return g_object_new (EMPATHY_TYPE_IRC_NETWORK_CHOOSER_DIALOG,
      "settings", settings,
      "network", network,
      "transient-for", parent,
      NULL);
}

EmpathyIrcNetwork *
empathy_irc_network_chooser_dialog_get_network (
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);

  return priv->network;
}

gboolean
empathy_irc_network_chooser_dialog_get_changed (
    EmpathyIrcNetworkChooserDialog *self)
{
  EmpathyIrcNetworkChooserDialogPriv *priv = GET_PRIV (self);

  return priv->changed;
}

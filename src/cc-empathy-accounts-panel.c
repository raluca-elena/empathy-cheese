/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2010 Collabora, Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors: Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include "config.h"

#include <stdlib.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/telepathy-glib.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#include "empathy-accounts-common.h"
#include "empathy-account-assistant.h"
#include "empathy-accounts-dialog.h"

#include "cc-empathy-accounts-panel.h"

#define CC_EMPATHY_ACCOUNTS_PANEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), CC_TYPE_EMPATHY_ACCOUNTS_PANEL, CcEmpathyAccountsPanelPrivate))

struct CcEmpathyAccountsPanelPrivate
{
  /* the original window holding the dialog content; it needs to be retained and
   * destroyed in our finalize(), since it invalidates its children (even if
   * they've already been reparented by the time it is destroyed) */
  GtkWidget *accounts_window;

  GtkWidget *assistant;
  GtkWidget *container;
};

G_DEFINE_DYNAMIC_TYPE (CcEmpathyAccountsPanel, cc_empathy_accounts_panel, CC_TYPE_PANEL)

static void
panel_pack_with_accounts_dialog (CcEmpathyAccountsPanel *panel)
{
  GtkWidget *content;
  GtkWidget *action_area;

  if (panel->priv->accounts_window != NULL)
    {
      gtk_widget_destroy (panel->priv->accounts_window);
      gtk_container_remove (GTK_CONTAINER (panel),
          gtk_bin_get_child (GTK_BIN (panel)));
    }

    panel->priv->accounts_window = empathy_accounts_dialog_show (NULL, NULL);
    gtk_widget_hide (panel->priv->accounts_window);

    content = gtk_dialog_get_content_area (
        GTK_DIALOG (panel->priv->accounts_window));
    action_area = gtk_dialog_get_action_area (
        GTK_DIALOG (panel->priv->accounts_window));
    gtk_widget_set_no_show_all (action_area, TRUE);
    gtk_widget_hide (action_area);

    gtk_widget_reparent (content, GTK_WIDGET (panel->priv->container));
}

static void
account_assistant_closed_cb (GtkWidget *widget,
    gpointer user_data)
{
  CcEmpathyAccountsPanel *panel = CC_EMPATHY_ACCOUNTS_PANEL (user_data);

  if (empathy_accounts_dialog_is_creating (
      EMPATHY_ACCOUNTS_DIALOG (panel->priv->accounts_window)))
    {
      empathy_account_dialog_cancel (
        EMPATHY_ACCOUNTS_DIALOG (panel->priv->accounts_window));
    }

  gtk_widget_set_sensitive (GTK_WIDGET (panel), TRUE);
  panel->priv->assistant = NULL;
}

static void
connection_managers_prepare (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyConnectionManagers *cm_mgr = EMPATHY_CONNECTION_MANAGERS (source);
  TpAccountManager *account_mgr;
  CcEmpathyAccountsPanel *panel = CC_EMPATHY_ACCOUNTS_PANEL (user_data);

  account_mgr = TP_ACCOUNT_MANAGER (g_object_get_data (G_OBJECT (cm_mgr),
      "account-manager"));

  if (!empathy_connection_managers_prepare_finish (cm_mgr, result, NULL))
    goto out;

  panel_pack_with_accounts_dialog (panel);

  empathy_accounts_import (account_mgr, cm_mgr);

  if (!empathy_accounts_has_non_salut_accounts (account_mgr))
    {
      GtkWindow *parent;

      parent = empathy_get_toplevel_window (GTK_WIDGET (panel));
      panel->priv->assistant = empathy_account_assistant_show (parent, cm_mgr);

      gtk_widget_set_sensitive (GTK_WIDGET (panel), FALSE);

      tp_g_signal_connect_object (panel->priv->assistant, "hide",
        G_CALLBACK (account_assistant_closed_cb),
        panel, 0);
    }

out:
  /* remove ref from active_changed() */
  g_object_unref (account_mgr);
  g_object_unref (cm_mgr);
}

static void
account_manager_ready_for_accounts_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *account_mgr = TP_ACCOUNT_MANAGER (source_object);
  CcEmpathyAccountsPanel *panel = CC_EMPATHY_ACCOUNTS_PANEL (user_data);
  GError *error = NULL;

  if (!tp_account_manager_prepare_finish (account_mgr, result, &error))
    {
      g_warning ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  if (empathy_accounts_has_non_salut_accounts (account_mgr))
    {
      panel_pack_with_accounts_dialog (panel);

      /* remove ref from active_changed() */
      g_object_unref (account_mgr);
    }
  else
    {
      EmpathyConnectionManagers *cm_mgr;

      cm_mgr = empathy_connection_managers_dup_singleton ();

      g_object_set_data_full (G_OBJECT (cm_mgr), "account-manager",
          g_object_ref (account_mgr), (GDestroyNotify) g_object_unref);

      empathy_connection_managers_prepare_async (cm_mgr,
          connection_managers_prepare, panel);
    }
}

static void
cc_empathy_accounts_panel_finalize (GObject *object)
{
  CcEmpathyAccountsPanel *panel;

  g_return_if_fail (object != NULL);
  g_return_if_fail (CC_IS_EMPATHY_ACCOUNTS_PANEL (object));

  panel = CC_EMPATHY_ACCOUNTS_PANEL (object);

  g_return_if_fail (panel->priv != NULL);

  gtk_widget_destroy (panel->priv->accounts_window);

  if (panel->priv->assistant != NULL)
    gtk_widget_destroy (panel->priv->assistant);

  G_OBJECT_CLASS (cc_empathy_accounts_panel_parent_class)->finalize (object);
}

static void
cc_empathy_accounts_panel_class_init (CcEmpathyAccountsPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = cc_empathy_accounts_panel_finalize;

  g_type_class_add_private (klass, sizeof (CcEmpathyAccountsPanelPrivate));
}

static void
cc_empathy_accounts_panel_class_finalize (CcEmpathyAccountsPanelClass *klass)
{
}

static void
cc_empathy_accounts_panel_init (CcEmpathyAccountsPanel *panel)
{
  TpAccountManager *account_manager;

  panel->priv = CC_EMPATHY_ACCOUNTS_PANEL_GET_PRIVATE (panel);

  /* create a container widget immediately, and pack it into the panel,
   * because the CC library expects a children to exist after
   * the object is constructed.
   */
  panel->priv->container = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_show (panel->priv->container);
  gtk_container_add (GTK_CONTAINER (panel), panel->priv->container);

  empathy_gtk_init ();

  /* unref'd in final endpoint callbacks */
  account_manager = tp_account_manager_dup ();

  tp_account_manager_prepare_async (account_manager, NULL,
      account_manager_ready_for_accounts_cb, panel);
}

void
cc_empathy_accounts_panel_register (GIOModule *module)
{
  /* Setup gettext */
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

  cc_empathy_accounts_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
      CC_TYPE_EMPATHY_ACCOUNTS_PANEL, "empathy-accounts", 10);
}

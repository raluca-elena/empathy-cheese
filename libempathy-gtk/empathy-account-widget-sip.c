/*
 * Copyright (C) 2007-2008 Guillaume Desmottes
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
 *          Frederic Peters <fpeters@0d.be>
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>
#include <libempathy/empathy-utils.h>

#include "empathy-account-widget.h"
#include "empathy-account-widget-private.h"
#include "empathy-account-widget-sip.h"
#include "empathy-ui-utils.h"

typedef struct {
  EmpathyAccountWidget *self;
  GtkWidget *vbox_settings;

  GtkWidget *label_stun_server;
  GtkWidget *entry_stun_server;
  GtkWidget *label_stun_port;
  GtkWidget *spinbutton_stun_port;
  GtkWidget *checkbutton_discover_stun;
  GtkWidget *combobox_transport;
  GtkWidget *combobox_keep_alive_mechanism;
  GtkWidget *spinbutton_keepalive_interval;
} EmpathyAccountWidgetSip;

static void
account_widget_sip_destroy_cb (GtkWidget *widget,
                               EmpathyAccountWidgetSip *settings)
{
  g_slice_free (EmpathyAccountWidgetSip, settings);
}

static void
account_widget_sip_discover_stun_toggled_cb (
    GtkWidget *checkbox,
    EmpathyAccountWidgetSip *settings)
{
  gboolean active;

  active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox));
  gtk_widget_set_sensitive (settings->label_stun_server, !active);
  gtk_widget_set_sensitive (settings->entry_stun_server, !active);
  gtk_widget_set_sensitive (settings->label_stun_port, !active);
  gtk_widget_set_sensitive (settings->spinbutton_stun_port, !active);
}

static void
keep_alive_mechanism_combobox_change_cb (GtkWidget *widget,
    EmpathyAccountWidgetSip *self)
{
  GtkTreeIter iter;
  GtkTreeModel *model;
  gchar *mechanism;
  gboolean enabled;

  /* Unsensitive the keep-alive spin button if keep-alive is disabled */
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter))
    return;

  model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
  gtk_tree_model_get (model, &iter, 0, &mechanism, -1);

  enabled = tp_strdiff (mechanism, "none");

  gtk_widget_set_sensitive (self->spinbutton_keepalive_interval, enabled);
  g_free (mechanism);
}

static void
checkbutton_tel_toggled (
    GtkWidget *checkbox,
    EmpathyAccountWidgetSip *sip_settings)
{
  EmpathyAccountSettings *settings;

  settings = empathy_account_widget_get_settings (sip_settings->self);

  empathy_account_settings_set_uri_scheme_tel (settings,
      gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (checkbox)));

  empathy_account_widget_changed (sip_settings->self);
}

void
empathy_account_widget_sip_build (EmpathyAccountWidget *self,
    const char *filename,
    GtkWidget **grid_common_settings)
{
  EmpathyAccountWidgetSip *settings;
  GtkWidget *vbox_settings;
  gboolean is_simple;
  GtkWidget *grid_advanced;

  g_object_get (self, "simple", &is_simple, NULL);

  if (is_simple)
    {
      self->ui_details->gui = empathy_builder_get_file (filename,
          "vbox_sip_simple", &vbox_settings,
          NULL);

      empathy_account_widget_handle_params (self,
          "entry_userid_simple", "account",
          "entry_password_simple", "password",
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_userid_simple");
    }
  else
    {
      GtkListStore *store;
      GtkTreeIter iter;
      GtkCellRenderer *renderer;
      GtkToggleButton *checkbutton_tel;

      settings = g_slice_new0 (EmpathyAccountWidgetSip);
      settings->self = self;

      self->ui_details->gui = empathy_builder_get_file (filename,
          "grid_common_settings", grid_common_settings,
          "grid_advanced_sip_settings", &grid_advanced,
          "vbox_sip_settings", &vbox_settings,
          "label_stun-server", &settings->label_stun_server,
          "entry_stun-server", &settings->entry_stun_server,
          "label_stun-port", &settings->label_stun_port,
          "spinbutton_stun-port", &settings->spinbutton_stun_port,
          "checkbutton_discover-stun", &settings->checkbutton_discover_stun,
          "spinbutton_keepalive-interval",
            &settings->spinbutton_keepalive_interval,
          "checkbutton_tel", &checkbutton_tel,
          NULL);
      settings->vbox_settings = vbox_settings;

      gtk_toggle_button_set_active (checkbutton_tel,
          empathy_account_settings_has_uri_scheme_tel (
            empathy_account_widget_get_settings (self)));

      empathy_account_widget_handle_params (self,
          "entry_userid", "account",
          "entry_password", "password",
          "checkbutton_discover-stun", "discover-stun",
          "entry_stun-server", "stun-server",
          "spinbutton_stun-port", "stun-port",
          "entry_auth-user", "auth-user",
          "entry_proxy-host", "proxy-host",
          "spinbutton_port", "port",
          "checkbutton_loose-routing", "loose-routing",
          "checkbutton_discover-binding", "discover-binding",
          "spinbutton_keepalive-interval", "keepalive-interval",
          "checkbutton_ignore-tls-errors", "ignore-tls-errors",
          NULL);

      account_widget_sip_discover_stun_toggled_cb (
          settings->checkbutton_discover_stun,
          settings);

      empathy_builder_connect (self->ui_details->gui, settings,
          "vbox_sip_settings", "destroy", account_widget_sip_destroy_cb,
          "checkbutton_discover-stun", "toggled",
          account_widget_sip_discover_stun_toggled_cb,
          "checkbutton_tel", "toggled", checkbutton_tel_toggled,
          NULL);

      self->ui_details->default_focus = g_strdup ("entry_userid");

      /* Create the 'transport' combo box. The first column has to contain the
       * value of the param. */
      store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
      settings->combobox_transport = gtk_combo_box_new_with_model (
          GTK_TREE_MODEL (store));

      renderer = gtk_cell_renderer_text_new ();
      gtk_cell_layout_pack_start (
          GTK_CELL_LAYOUT (settings->combobox_transport), renderer, TRUE);
      gtk_cell_layout_add_attribute (
          GTK_CELL_LAYOUT (settings->combobox_transport), renderer, "text", 1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "auto", 1, _("Auto"), -1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "udp", 1, _("UDP"), -1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "tcp", 1, _("TCP"), -1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "tls", 1, _("TLS"), -1);

      empathy_account_widget_setup_widget (self, settings->combobox_transport,
          "transport");

      gtk_grid_attach (GTK_GRID (grid_advanced), settings->combobox_transport,
          1, 11, 3, 1);

      gtk_widget_show (settings->combobox_transport);

      /* Create the 'keep-alive mechanism' combo box */
      store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
      settings->combobox_keep_alive_mechanism = gtk_combo_box_new_with_model (
          GTK_TREE_MODEL (store));

      renderer = gtk_cell_renderer_text_new ();
      gtk_cell_layout_pack_start (
          GTK_CELL_LAYOUT (settings->combobox_keep_alive_mechanism), renderer,
          TRUE);
      gtk_cell_layout_add_attribute (
          GTK_CELL_LAYOUT (settings->combobox_keep_alive_mechanism), renderer,
          "text", 1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "auto", 1, _("Auto"), -1);

      gtk_list_store_append (store, &iter);
      /* translators: this string is very specific to SIP's internal; maybe
       * best to keep the English version. */
      gtk_list_store_set (store, &iter, 0, "register", 1, _("Register"), -1);

      gtk_list_store_append (store, &iter);
      /* translators: this string is very specific to SIP's internal; maybe
       * best to keep the English version. */
      gtk_list_store_set (store, &iter, 0, "options", 1, _("Options"), -1);

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, "none", 1, _("None"), -1);

      g_signal_connect (settings->combobox_keep_alive_mechanism, "changed",
          G_CALLBACK (keep_alive_mechanism_combobox_change_cb), settings);

      empathy_account_widget_setup_widget (self,
          settings->combobox_keep_alive_mechanism, "keepalive-mechanism");

      gtk_grid_attach (GTK_GRID (grid_advanced),
          settings->combobox_keep_alive_mechanism,
          1, 7, 3, 1);

      gtk_widget_show (settings->combobox_keep_alive_mechanism);
    }

  self->ui_details->widget = vbox_settings;
}

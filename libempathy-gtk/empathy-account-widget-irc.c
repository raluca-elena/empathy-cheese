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
 */

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-utils.h>

#include "empathy-irc-network-dialog.h"
#include "empathy-irc-network-chooser.h"
#include "empathy-account-widget.h"
#include "empathy-account-widget-private.h"
#include "empathy-account-widget-irc.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT | EMPATHY_DEBUG_IRC
#include <libempathy/empathy-debug.h>

typedef struct {
  EmpathyAccountWidget *self;

  GtkWidget *vbox_settings;

  GtkWidget *network_chooser;
} EmpathyAccountWidgetIrc;

static void
account_widget_irc_destroy_cb (GtkWidget *widget,
                               EmpathyAccountWidgetIrc *settings)
{
  g_slice_free (EmpathyAccountWidgetIrc, settings);
}

static void
account_widget_irc_setup (EmpathyAccountWidgetIrc *settings)
{
  const gchar *nick = NULL;
  const gchar *fullname = NULL;
  gint port = 6667;
  const gchar *charset;
  gboolean ssl = FALSE;
  EmpathyAccountSettings *ac_settings;

  g_object_get (settings->self, "settings", &ac_settings, NULL);

  nick = empathy_account_settings_get_string (ac_settings, "account");
  fullname = empathy_account_settings_get_string (ac_settings,
      "fullname");
  charset = empathy_account_settings_get_string (ac_settings, "charset");
  port = empathy_account_settings_get_uint32 (ac_settings, "port");
  ssl = empathy_account_settings_get_boolean (ac_settings, "use-ssl");

  if (!nick)
    {
      nick = g_strdup (g_get_user_name ());
      empathy_account_settings_set_string (ac_settings,
        "account", nick);
    }

  if (!fullname)
    {
      fullname = g_strdup (g_get_real_name ());
      if (!fullname)
        {
          fullname = g_strdup (nick);
        }
      empathy_account_settings_set_string (ac_settings,
          "fullname", fullname);
    }
}

static void
network_changed_cb (EmpathyIrcNetworkChooser *chooser,
    EmpathyAccountWidgetIrc *settings)
{
  empathy_account_widget_changed (settings->self);
}

/**
 * set_password_prompt_if_needed:
 *
 * If @password is not empty, this function sets the 'password-prompt' param
 * on @ac_settings. This will ensure that Idle actually asks for the password
 * when connecting.
 *
 * Return: %TRUE if the password-prompt param has been changed
 */
static gboolean
set_password_prompt_if_needed (EmpathyAccountSettings *ac_settings,
    const gchar *password)
{
  gboolean prompt;

  prompt = !tp_str_empty (password);

  if (prompt == empathy_account_settings_get_boolean (ac_settings,
        "password-prompt"))
    return FALSE;

  empathy_account_settings_set_boolean (ac_settings, "password-prompt",
      prompt);

  return TRUE;
}

static void
entry_password_changed_cb (GtkEntry *entry,
    EmpathyAccountWidgetIrc *settings)
{
  const gchar *password;
  EmpathyAccountSettings *ac_settings;

  g_object_get (settings->self, "settings", &ac_settings, NULL);

  password = gtk_entry_get_text (entry);

  set_password_prompt_if_needed (ac_settings, password);

  g_object_unref (ac_settings);
}

EmpathyIrcNetworkChooser *
empathy_account_widget_irc_build (EmpathyAccountWidget *self,
    const char *filename,
    GtkWidget **table_common_settings)
{
  EmpathyAccountWidgetIrc *settings;
  EmpathyAccountSettings *ac_settings;
  GtkWidget *entry_password;
  const gchar *password;

  settings = g_slice_new0 (EmpathyAccountWidgetIrc);
  settings->self = self;

  self->ui_details->gui = empathy_builder_get_file (filename,
      "table_irc_settings", table_common_settings,
      "vbox_irc", &self->ui_details->widget,
      "table_irc_settings", &settings->vbox_settings,
      "entry_password", &entry_password,
      NULL);

  /* Add network chooser button */
  g_object_get (settings->self, "settings", &ac_settings, NULL);

  settings->network_chooser = empathy_irc_network_chooser_new (ac_settings);

  g_signal_connect (settings->network_chooser, "changed",
      G_CALLBACK (network_changed_cb), settings);

  gtk_table_attach (GTK_TABLE (*table_common_settings),
      settings->network_chooser, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);

  gtk_widget_show (settings->network_chooser);

  account_widget_irc_setup (settings);

  empathy_account_widget_handle_params (self,
      "entry_nick", "account",
      "entry_fullname", "fullname",
      "entry_password", "password",
      "entry_quit_message", "quit-message",
      NULL);

  empathy_builder_connect (self->ui_details->gui, settings,
      "table_irc_settings", "destroy", account_widget_irc_destroy_cb,
      NULL);

  self->ui_details->default_focus = g_strdup ("entry_nick");

  g_object_unref (ac_settings);

  /* Automatically set password-prompt when needed */
  password = empathy_account_settings_get_string (ac_settings, "password");

  if (set_password_prompt_if_needed (ac_settings, password))
    {
      /* Apply right now to save password-prompt */
      empathy_account_settings_apply_async (ac_settings, NULL, NULL);
    }

  g_signal_connect (entry_password, "changed",
      G_CALLBACK (entry_password_changed_cb), settings);

  return EMPATHY_IRC_NETWORK_CHOOSER (settings->network_chooser);
}

EmpathyIrcNetworkChooser *
empathy_account_widget_irc_build_simple (EmpathyAccountWidget *self,
    const char *filename)
{
  EmpathyAccountWidgetIrc *settings;
  EmpathyAccountSettings *ac_settings;
  GtkAlignment *alignment;

  settings = g_slice_new0 (EmpathyAccountWidgetIrc);
  settings->self = self;

  self->ui_details->gui = empathy_builder_get_file (filename,
      "vbox_irc_simple", &self->ui_details->widget,
      "alignment_network_simple", &alignment,
      NULL);

  /* Add network chooser button */
  g_object_get (settings->self, "settings", &ac_settings, NULL);

  settings->network_chooser = empathy_irc_network_chooser_new (ac_settings);

  g_signal_connect (settings->network_chooser, "changed",
      G_CALLBACK (network_changed_cb), settings);

  gtk_container_add (GTK_CONTAINER (alignment), settings->network_chooser);

  gtk_widget_show (settings->network_chooser);

  empathy_account_widget_handle_params (self,
      "entry_nick_simple", "account",
      NULL);

  empathy_builder_connect (self->ui_details->gui, settings,
      "vbox_irc_simple", "destroy", account_widget_irc_destroy_cb,
      NULL);

  self->ui_details->default_focus = g_strdup ("entry_nick_simple");

  g_object_unref (ac_settings);

  return EMPATHY_IRC_NETWORK_CHOOSER (settings->network_chooser);
}

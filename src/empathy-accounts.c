/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 *          Jonathan Tellier <jonathan.tellier@gmail.com>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <telepathy-glib/account-manager.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/util.h>

#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-connection-managers.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-accounts.h"
#include "empathy-accounts-common.h"
#include "empathy-accounts-dialog.h"
#include "empathy-account-assistant.h"
#include "empathy-auto-salut-account-helper.h"

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

#define EMPATHY_ACCOUNTS_DBUS_NAME "org.gnome.EmpathyAccounts"

static gboolean only_if_needed = FALSE;
static gboolean hidden = FALSE;
static gchar *selected_account_name = NULL;
static gboolean account_manager_prepared = FALSE;
static gboolean assistant = FALSE;

static void
maybe_show_accounts_ui (TpAccountManager *manager)
{
  if (hidden ||
      (only_if_needed && empathy_accounts_has_non_salut_accounts (manager)))
    gtk_main_quit ();
  else
    empathy_accounts_show_accounts_ui (manager, NULL, assistant, gtk_main_quit);
}

static TpAccount *
find_account (TpAccountManager *mgr,
    const gchar *path)
{
  GList *accounts, *l;
  TpAccount *found = NULL;

  accounts = tp_account_manager_get_valid_accounts (mgr);
  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      if (!tp_strdiff (tp_proxy_get_object_path (l->data), path))
        {
          found = l->data;
          break;
        }
    }

  g_list_free (accounts);
  return found;
}

static void
account_manager_ready_for_accounts_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_clear_error (&error);
      return;
    }

  account_manager_prepared = TRUE;

  if (selected_account_name != NULL)
    {
      gchar *account_path;
      TpAccount *account;

      /* create and prep the corresponding TpAccount so it's fully ready by the
       * time we try to select it in the accounts dialog */
      if (g_str_has_prefix (selected_account_name, TP_ACCOUNT_OBJECT_PATH_BASE))
        account_path = g_strdup (selected_account_name);
      else
        account_path = g_strdup_printf ("%s%s", TP_ACCOUNT_OBJECT_PATH_BASE,
            selected_account_name);

      account = find_account (manager, account_path);

      if (account != NULL)
        {
          empathy_accounts_show_accounts_ui (manager, account, assistant,
              G_CALLBACK (gtk_main_quit));

          return;
        }
      else
        {
          DEBUG ("Failed to find account with path %s", account_path);

          g_clear_error (&error);

          maybe_show_accounts_ui (manager);
        }

      g_free (account_path);
    }
  else
    {
      maybe_show_accounts_ui (manager);
    }
}

static int
app_command_line_cb (GApplication *app,
    GApplicationCommandLine *cmdline)
{
  g_application_hold (app);

  /* if the window is ready, present it; otherwise, it will be presented when
   * the accounts manager is prepared */
  if (account_manager_prepared)
    {
      TpAccountManager *account_manager;

      account_manager = tp_account_manager_dup ();
      empathy_accounts_show_accounts_ui (account_manager, NULL, assistant,
              G_CALLBACK (gtk_main_quit));

      g_object_unref (account_manager);
    }

  return 0;
}

static gboolean
local_cmdline (GApplication *app,
    gchar ***arguments,
    gint *exit_status)
{
  gint i;
  gchar **argv;
  gint argc = 0;
  gboolean retval = FALSE;
  GError *error = NULL;

  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "hidden", 'h',
        0, G_OPTION_ARG_NONE, &hidden,
        N_("Don't display any dialogs; do any work (eg, importing) and exit"),
        NULL },
      { "if-needed", 'n',
        0, G_OPTION_ARG_NONE, &only_if_needed,
        N_("Don't display any dialogs unless there are only \"People Nearby\" accounts"),
        NULL },
      { "select-account", 's',
        G_OPTION_FLAG_IN_MAIN, G_OPTION_ARG_STRING, &selected_account_name,
        N_("Initially select given account (eg, "
            "gabble/jabber/foo_40example_2eorg0)"),
        N_("<account-id>") },
      { "assistant", 'a',
        0, G_OPTION_ARG_NONE, &assistant,
        N_("Show account assistant"),
        NULL },

      { NULL }
  };

  optcontext = g_option_context_new (N_("- Empathy Accounts"));
  g_option_context_add_group (optcontext, gtk_get_option_group (TRUE));
  g_option_context_add_main_entries (optcontext, options, GETTEXT_PACKAGE);

  argv = *arguments;
  for (i = 0; argv[i] != NULL; i++)
    argc++;

  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command line options.\n",
          error->message, argv[0]);
      g_warning ("Error in empathy init: %s", error->message);

      *exit_status = EXIT_FAILURE;
      retval = TRUE;
    }

  g_option_context_free (optcontext);

  return retval;
}

#define COMMAND_ACCOUNTS_DIALOG 1

int
main (int argc, char *argv[])
{
  TpAccountManager *account_manager;
  GtkApplication *app;
  GObjectClass *app_class;
  gint retval;

  g_thread_init (NULL);
  empathy_init ();

  gtk_init (&argc, &argv);
  empathy_gtk_init ();

  g_set_application_name (_("Empathy Accounts"));

  /* Make empathy and empathy-accounts appear as the same app in gnome-shell */
  gdk_set_program_class ("Empathy");
  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

  app = gtk_application_new (EMPATHY_ACCOUNTS_DBUS_NAME,
      G_APPLICATION_HANDLES_COMMAND_LINE);
  app_class = G_OBJECT_GET_CLASS (app);
  G_APPLICATION_CLASS (app_class)->local_command_line = local_cmdline;

  account_manager = tp_account_manager_dup ();

  tp_proxy_prepare_async (account_manager, NULL,
    account_manager_ready_for_accounts_cb, NULL);

  g_signal_connect (app, "command-line", G_CALLBACK (app_command_line_cb),
      NULL);

  retval = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (account_manager);
  g_object_unref (app);

  return retval;
}

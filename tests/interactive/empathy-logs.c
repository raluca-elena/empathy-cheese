/*
 * Copyright (C) 2008 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>
#include <stdlib.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>
#include <libempathy-gtk/empathy-log-window.h>
#include <libempathy-gtk/empathy-ui-utils.h>

static void
destroy_cb (GtkWidget *dialog,
    gpointer user_data)
{
  gtk_main_quit ();
}

static void
account_manager_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;
  GtkWidget *window;

  tp_proxy_prepare_finish (source, result, &error);
  g_assert_no_error (error);

  window = empathy_log_window_show (NULL, NULL, FALSE, NULL);

  g_signal_connect (window, "destroy", G_CALLBACK (destroy_cb), NULL);
}

int
main (int argc,
    char *argv[])
{
  TpAccountManager *mgr;

  g_thread_init (NULL);
  gtk_init (&argc, &argv);
  empathy_gtk_init ();
  g_set_application_name (PACKAGE_NAME);
  gtk_window_set_default_icon_name ("empathy");

  mgr = tp_account_manager_dup ();

  tp_proxy_prepare_async (mgr, NULL, account_manager_prepare_cb, NULL);

  gtk_main ();

  g_object_unref (mgr);

  return EXIT_SUCCESS;
}

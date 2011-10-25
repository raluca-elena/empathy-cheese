/*
 * Copyright (C) 2005-2007 Imendio AB
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
 */

#include <config.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libempathy/empathy-utils.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "empathy-debug-window.h"

#define EMPATHY_DEBUGGER_DBUS_NAME "org.gnome.Empathy.Debugger"

static GtkWidget *window = NULL;
static gchar *service = NULL;

static void
activate_cb (GApplication *app)
{
  if (window == NULL)
    {
      window = empathy_debug_window_new (NULL);

      gtk_application_add_window (GTK_APPLICATION (app),
          GTK_WINDOW (window));
    }
  else
    {
      gtk_window_present (GTK_WINDOW (window));
    }

  if (service != NULL)
    empathy_debug_window_show (EMPATHY_DEBUG_WINDOW (window),
        service);
}

static gint
command_line_cb (GApplication *application,
    GApplicationCommandLine *command_line,
    gpointer user_data)
{
  GError *error = NULL;
  gchar **argv;
  gint argc;
  gint retval = 0;

  GOptionContext *optcontext;
  GOptionEntry options[] = {
      { "show-service", 's',
        0, G_OPTION_ARG_STRING, &service,
        N_("Show a particular service"),
        NULL },
      { NULL }
  };

  optcontext = g_option_context_new (N_("- Empathy Debugger"));
  g_option_context_add_group (optcontext, gtk_get_option_group (TRUE));
  g_option_context_add_main_entries (optcontext, options, GETTEXT_PACKAGE);

  argv = g_application_command_line_get_arguments (command_line, &argc);

  if (!g_option_context_parse (optcontext, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n",
          error->message, argv[0]);

      retval = 1;
    }

  g_option_context_free (optcontext);
  g_strfreev (argv);

  g_application_activate (application);

  return retval;
}

int
main (int argc,
    char **argv)
{
  GtkApplication *app;
  gint retval;

  g_thread_init (NULL);
  gtk_init (&argc, &argv);
  empathy_gtk_init ();

  app = gtk_application_new (EMPATHY_DEBUGGER_DBUS_NAME,
      G_APPLICATION_HANDLES_COMMAND_LINE);
  g_signal_connect (app, "activate", G_CALLBACK (activate_cb), NULL);
  g_signal_connect (app, "command-line", G_CALLBACK (command_line_cb), NULL);

  g_set_application_name (_("Empathy Debugger"));

  /* Make empathy and empathy-debugger appear as the same app in gnome-shell */
  gdk_set_program_class ("Empathy");
  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

  retval = g_application_run (G_APPLICATION (app), argc, argv);

  g_object_unref (app);

  return retval;
}

/*
 * Copyright (C) 2011 Collabora Ltd.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <config.h>

#include <gtk/gtk.h>

#include <libempathy/empathy-contact-manager.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-contact-blocking-dialog.h>

int
main (int argc,
    char **argv)
  {
    EmpathyContactManager *manager;
    GtkWidget *dialog;

    gtk_init (&argc, &argv);
    empathy_gtk_init ();

    manager = empathy_contact_manager_dup_singleton ();
    dialog = empathy_contact_blocking_dialog_new (NULL);

    g_signal_connect_swapped (dialog, "response",
        G_CALLBACK (gtk_main_quit), NULL);

    gtk_widget_show (dialog);

    gtk_main ();

    gtk_widget_destroy (dialog);
    g_object_unref (manager);

    return 0;
  }

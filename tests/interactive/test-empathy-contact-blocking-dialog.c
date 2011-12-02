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

#include <libempathy/empathy-client-factory.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-contact-blocking-dialog.h>

static void
am_prepare_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
    GMainLoop *loop = user_data;
    GtkWidget *dialog;

    dialog = empathy_contact_blocking_dialog_new (NULL);

    gtk_dialog_run (GTK_DIALOG (dialog));

    g_main_loop_quit (loop);
}

int
main (int argc,
    char **argv)
  {
    EmpathyClientFactory *factory;
    TpAccountManager *am;
    GMainLoop *loop;

    gtk_init (&argc, &argv);
    empathy_gtk_init ();

    /* The blocking dialog needs the contact list for the contacts completion
     * so we prepare it first. */
    factory = empathy_client_factory_dup ();

    tp_simple_client_factory_add_connection_features_varargs (
        TP_SIMPLE_CLIENT_FACTORY (factory),
        TP_CONNECTION_FEATURE_CONTACT_LIST,
        NULL);

    am = tp_account_manager_dup ();

    loop = g_main_loop_new (NULL, FALSE);

    tp_proxy_prepare_async (am, NULL, am_prepare_cb, loop);

    g_main_loop_run (loop);

    g_object_unref (am);
    return 0;
  }

/*
 * Copyright (C) 2008, 2009 Collabora Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more av.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include "config.h"

#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <telepathy-glib/enums.h>

#include <libempathy/empathy-contact.h>
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-ft-factory.h>
#include <libempathy/empathy-ft-handler.h>

#include <libempathy-gtk/empathy-contact-chooser.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include "nautilus-sendto-plugin.h"

static EmpathyFTFactory *factory = NULL;
static guint transfers = 0;

static gboolean destroy (NstPlugin *plugin);

static gboolean
init (NstPlugin *plugin)
{
  g_print ("Init %s plugin\n", plugin->info->id);

  empathy_gtk_init ();

  return TRUE;
}

static EmpathyContact *
dup_contact_from_individual (FolksIndividual *individual)
{
  EmpathyContact *contact;
  gboolean can_do_action;

  if (individual == NULL)
    return NULL;

  contact = empathy_contact_dup_best_for_action (individual,
      EMPATHY_ACTION_SEND_FILE);
  if (contact == NULL)
    return NULL;

  can_do_action = empathy_contact_can_do_action (contact,
      EMPATHY_ACTION_SEND_FILE);

  if (!can_do_action)
    {
      /* If contact can't do FT we don't care about him */
      g_object_unref (contact);
      return NULL;
    }

  return contact;
}

static gboolean
filter_individual (EmpathyContactChooser *chooser,
    FolksIndividual *individual,
    gboolean is_online,
    gboolean searching,
    gpointer user_data)
{
  EmpathyContact *contact;

  if (!is_online)
    return FALSE;

  contact = dup_contact_from_individual (individual);
  if (contact == NULL)
    return FALSE;

  g_object_unref (contact);
  return TRUE;
}

static GtkWidget *
get_contacts_widget (NstPlugin *plugin)
{
  GtkWidget *chooser;

  chooser = empathy_contact_chooser_new ();
  empathy_contact_chooser_set_filter_func (EMPATHY_CONTACT_CHOOSER (chooser),
      filter_individual, plugin);

  empathy_contact_chooser_show_search_entry (EMPATHY_CONTACT_CHOOSER (chooser),
      FALSE);

  /* Make sure to display some contacts */
  gtk_widget_set_size_request (chooser, -1, 300);
  return chooser;
}

static EmpathyContact *
get_selected_contact (GtkWidget *widget)
{
  FolksIndividual *individual;
  EmpathyContact *contact;

  individual = empathy_contact_chooser_dup_selected (
      EMPATHY_CONTACT_CHOOSER (widget));
  if (individual == NULL)
    return NULL;

  contact = dup_contact_from_individual (individual);

  g_object_unref (individual);
  return contact;
}

static gboolean
validate_destination (NstPlugin *plugin,
                      GtkWidget *contact_widget,
                      gchar **error)
{
  EmpathyContact *contact = NULL;
  gboolean ret = TRUE;

  contact = get_selected_contact (contact_widget);

  if (contact == NULL)
    return FALSE;

  g_object_unref (contact);

  return TRUE;
}

static void
quit (void)
{
  if (--transfers > 0)
    return;

  destroy (NULL);
  gtk_main_quit ();
}

static void
transfer_done_cb (EmpathyFTHandler *handler,
                  TpFileTransferChannel *channel,
                  NstPlugin *plugin)
{
  quit ();
}

static void
transfer_error_cb (EmpathyFTHandler *handler,
                   GError *error,
                   NstPlugin *plugin)
{
  quit ();
}

static void
error_dialog_cb (GtkDialog *dialog,
                 gint arg,
                 gpointer user_data)
{
  gtk_widget_destroy (GTK_WIDGET (dialog));
  quit ();
}

static void
handler_ready_cb (EmpathyFTFactory *fact,
                  EmpathyFTHandler *handler,
                  GError *error,
                  NstPlugin *plugin)
{
  if (error != NULL)
    {
      GtkWidget *dialog;
      dialog = gtk_message_dialog_new (NULL, 0, GTK_MESSAGE_ERROR,
          GTK_BUTTONS_CLOSE, "%s",
          error->message ? error->message : _("No error message"));

      g_signal_connect (dialog, "response", G_CALLBACK (error_dialog_cb), NULL);
      gtk_widget_show (dialog);
    }
  else
    {
      g_signal_connect (handler, "transfer-done",
          G_CALLBACK (transfer_done_cb), plugin);
      g_signal_connect (handler, "transfer-error",
          G_CALLBACK (transfer_error_cb), plugin);

      empathy_ft_handler_start_transfer (handler);
    }
}

static gboolean
send_files (NstPlugin *plugin,
            GtkWidget *contact_widget,
            GList *file_list)
{
  EmpathyContact *contact;
  GList *l;

  contact = get_selected_contact (contact_widget);

  if (contact == NULL)
    return FALSE;

  factory = empathy_ft_factory_dup_singleton ();

  g_signal_connect (factory, "new-ft-handler",
      G_CALLBACK (handler_ready_cb), plugin);

  for (l = file_list; l; l = l->next)
    {
      gchar *path = l->data;
      GFile *file;

      file = g_file_new_for_uri (path);

      ++transfers;

      empathy_ft_factory_new_transfer_outgoing (factory,
          contact, file, empathy_get_current_action_time ());

      g_object_unref (file);
    }

  g_object_unref (contact);

  if (transfers == 0)
    {
      destroy (NULL);
      return TRUE;
    }

  return FALSE;
}

static gboolean
destroy (NstPlugin *plugin)
{
  if (factory)
    g_object_unref (factory);

  return TRUE;
}

static
NstPluginInfo plugin_info = {
  "im",
  "empathy",
  N_("Instant Message (Empathy)"),
  GETTEXT_PACKAGE,
  NAUTILUS_CAPS_NONE,
  init,
  get_contacts_widget,
  validate_destination,
  send_files,
  destroy
};

NST_INIT_PLUGIN (plugin_info)


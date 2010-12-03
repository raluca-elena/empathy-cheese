/*
 * Copyright (C) 2010 Collabora Ltd.
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
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <stdlib.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-auth-factory.h>
#include <libempathy/empathy-server-sasl-handler.h>
#include <libempathy/empathy-server-tls-handler.h>
#include <libempathy/empathy-tls-verifier.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-tls-dialog.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#include <gnutls/gnutls.h>

#include <extensions/extensions.h>

#define TIMEOUT 60

static gboolean use_timer = TRUE;
static guint timeout_id = 0;
static guint num_windows = 0;

static gboolean
timeout_cb (gpointer p)
{
  DEBUG ("Timeout reached; exiting...");

  gtk_main_quit ();
  return FALSE;
}

static void
start_timer (void)
{
  if (!use_timer)
    return;

  if (timeout_id != 0)
    return;

  DEBUG ("Start timer");

  timeout_id = g_timeout_add_seconds (TIMEOUT, timeout_cb, NULL);
}

static void
stop_timer (void)
{
  if (timeout_id == 0)
    return;

  DEBUG ("Stop timer");

  g_source_remove (timeout_id);
  timeout_id = 0;
}

static void
tls_dialog_response_cb (GtkDialog *dialog,
    gint response_id,
    gpointer user_data)
{
  EmpathyTLSCertificate *certificate = NULL;
  EmpTLSCertificateRejectReason reason = 0;
  GHashTable *details = NULL;
  EmpathyTLSDialog *tls_dialog = EMPATHY_TLS_DIALOG (dialog);
  gboolean remember = FALSE;

  DEBUG ("Response %d", response_id);

  g_object_get (tls_dialog,
      "certificate", &certificate,
      "reason", &reason,
      "remember", &remember,
      "details", &details,
      NULL);

  gtk_widget_destroy (GTK_WIDGET (dialog));

  if (response_id == GTK_RESPONSE_YES)
    {
      empathy_tls_certificate_accept_async (certificate, NULL, NULL);
    }
  else
    {
      tp_asv_set_boolean (details, "user-requested", TRUE);
      empathy_tls_certificate_reject_async (certificate, reason, details,
          NULL, NULL);
    }

  if (remember)
    empathy_tls_certificate_store_ca (certificate);

  g_object_unref (certificate);
  g_hash_table_unref (details);

  /* restart the timeout */
  num_windows--;

  if (num_windows > 0)
    return;

  start_timer ();
}

static void
display_interactive_dialog (EmpathyTLSCertificate *certificate,
    EmpTLSCertificateRejectReason reason,
    GHashTable *details)
{
  GtkWidget *tls_dialog;

  /* stop the timeout */
  num_windows++;
  stop_timer ();

  tls_dialog = empathy_tls_dialog_new (certificate, reason, details);
  g_signal_connect (tls_dialog, "response",
      G_CALLBACK (tls_dialog_response_cb), NULL);

  gtk_widget_show (tls_dialog);
}

static void
verifier_verify_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  gboolean res;
  EmpTLSCertificateRejectReason reason;
  GError *error = NULL;
  EmpathyTLSCertificate *certificate = NULL;
  GHashTable *details = NULL;

  g_object_get (source,
      "certificate", &certificate,
      NULL);

  res = empathy_tls_verifier_verify_finish (EMPATHY_TLS_VERIFIER (source),
      result, &reason, &details, &error);

  if (error != NULL)
    {
      DEBUG ("Error: %s", error->message);
      display_interactive_dialog (certificate, reason, details);

      g_error_free (error);
    }
  else
    {
      empathy_tls_certificate_accept_async (certificate, NULL, NULL);
    }

  g_object_unref (certificate);
}

static void
auth_factory_new_tls_handler_cb (EmpathyAuthFactory *factory,
    EmpathyServerTLSHandler *handler,
    gpointer user_data)
{
  EmpathyTLSCertificate *certificate = NULL;
  gchar *hostname = NULL;
  EmpathyTLSVerifier *verifier;

  DEBUG ("New TLS server handler received from the factory");

  g_object_get (handler,
      "certificate", &certificate,
      "hostname", &hostname,
      NULL);

  verifier = empathy_tls_verifier_new (certificate, hostname);
  empathy_tls_verifier_verify_async (verifier,
      verifier_verify_cb, NULL);

  g_object_unref (verifier);
  g_object_unref (certificate);
  g_free (hostname);
}

typedef struct
{
  EmpathyServerSASLHandler *handler;
  GtkWidget *entry;
  GtkWidget *ticky;
} PasswordDialogData;

static void
password_dialog_response_cb (GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
  PasswordDialogData *data = user_data;

  if (response == GTK_RESPONSE_OK)
    {
      empathy_server_sasl_handler_provide_password (data->handler,
          gtk_entry_get_text (GTK_ENTRY (data->entry)),
          gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (data->ticky)));
    }
  else
    {
      empathy_server_sasl_handler_cancel (data->handler);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));

  g_object_unref (data->handler);
  g_slice_free (PasswordDialogData, data);
}

static void
clear_icon_released_cb (GtkEntry *entry,
    GtkEntryIconPosition icon_pos,
    GdkEvent *event,
    gpointer user_data)
{
  gtk_entry_set_text (entry, "");
}

static void
password_entry_changed_cb (GtkEditable *entry,
    gpointer user_data)
{
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (entry));

  gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, !EMP_STR_EMPTY (str));
}

static void
auth_factory_new_sasl_handler_cb (EmpathyAuthFactory *factory,
    EmpathyServerSASLHandler *handler,
    gpointer user_data)
{
  GtkWidget *dialog, *entry, *icon, *ticky;
  GtkBox *box;
  TpAccount *account;
  PasswordDialogData *data;

  DEBUG ("New SASL server handler received from the factory");

  account = empathy_server_sasl_handler_get_account (handler);

  dialog = gtk_message_dialog_new_with_markup (NULL, 0,
      GTK_MESSAGE_OTHER, GTK_BUTTONS_OK_CANCEL,
      _("Enter your password for account\n<b>%s</b>"),
      tp_account_get_display_name (account));

  box = GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  /* dialog icon */
  icon = gtk_image_new_from_icon_name (tp_account_get_icon_name (account),
      GTK_ICON_SIZE_DIALOG);
  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), icon);
  gtk_widget_show (icon);

  /* entry */
  entry = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (entry), FALSE);

  /* entry clear icon */
  gtk_entry_set_icon_from_stock (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
  gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, FALSE);

  g_signal_connect (entry, "icon-release",
      G_CALLBACK (clear_icon_released_cb), NULL);
  g_signal_connect (entry, "changed",
      G_CALLBACK (password_entry_changed_cb), NULL);

  gtk_box_pack_start (box, entry, FALSE, FALSE, 0);
  gtk_widget_show (entry);

  /* remember password ticky */
  ticky = gtk_check_button_new_with_label (_("Remember password"));

  gtk_box_pack_start (box, ticky, FALSE, FALSE, 0);
  gtk_widget_show (ticky);

  data = g_slice_new0 (PasswordDialogData);
  data->handler = g_object_ref (handler);
  data->entry = entry;
  data->ticky = ticky;

  g_signal_connect (dialog, "response",
      G_CALLBACK (password_dialog_response_cb), data);

  gtk_widget_show (dialog);
}

int
main (int argc,
    char **argv)
{
  GOptionContext *context;
  GError *error = NULL;
  EmpathyAuthFactory *factory;

  g_thread_init (NULL);

  context = g_option_context_new (N_(" - Empathy authentication client"));
  g_option_context_add_group (context, gtk_get_option_group (TRUE));
  g_option_context_set_translation_domain (context, GETTEXT_PACKAGE);

  if (!g_option_context_parse (context, &argc, &argv, &error))
    {
      g_print ("%s\nRun '%s --help' to see a full list of available command "
          "line options.\n", error->message, argv[0]);
      g_warning ("Error in empathy-auth-client init: %s", error->message);
      return EXIT_FAILURE;
    }

  g_option_context_free (context);

  empathy_gtk_init ();
  gnutls_global_init ();
  g_set_application_name (_("Empathy authentication client"));

  gtk_window_set_default_icon_name ("empathy");
  textdomain (GETTEXT_PACKAGE);

  factory = empathy_auth_factory_dup_singleton ();

  g_signal_connect (factory, "new-server-tls-handler",
      G_CALLBACK (auth_factory_new_tls_handler_cb), NULL);

  g_signal_connect (factory, "new-server-sasl-handler",
      G_CALLBACK (auth_factory_new_sasl_handler_cb), NULL);

  if (!empathy_auth_factory_register (factory, &error))
    {
      g_critical ("Failed to register the auth factory: %s\n", error->message);
      g_error_free (error);
      g_object_unref (factory);

      return EXIT_FAILURE;
    }

  DEBUG ("Empathy auth client started.");

  if (g_getenv ("EMPATHY_PERSIST") != NULL)
    {
      DEBUG ("Timed-exit disabled");

      use_timer = FALSE;
    }

  start_timer ();

  gtk_main ();

  g_object_unref (factory);

  return EXIT_SUCCESS;
}

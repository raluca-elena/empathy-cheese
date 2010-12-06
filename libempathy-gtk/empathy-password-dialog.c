/*
 * empathy-password-dialog.c - Source for EmpathyPasswordDialog
 * Copyright (C) 2010 Collabora Ltd.
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

#include "empathy-password-dialog.h"

#include <glib/gi18n-lib.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>

G_DEFINE_TYPE (EmpathyPasswordDialog, empathy_password_dialog,
    GTK_TYPE_MESSAGE_DIALOG)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyPasswordDialog);

enum {
  PROP_HANDLER = 1,

  LAST_PROPERTY,
};

typedef struct {
  EmpathyServerSASLHandler *handler;

  GtkWidget *entry;
  GtkWidget *ticky;

  gboolean grabbing;

  gboolean dispose_run;
} EmpathyPasswordDialogPriv;

static void
empathy_password_dialog_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_HANDLER:
      g_value_set_object (value, priv->handler);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_password_dialog_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_HANDLER:
      g_assert (priv->handler == NULL); /* construct only */
      priv->handler = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_password_dialog_dispose (GObject *object)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (object);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  tp_clear_object (&priv->handler);

  G_OBJECT_CLASS (empathy_password_dialog_parent_class)->dispose (object);
}

static void
password_dialog_response_cb (GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (user_data);

  if (response == GTK_RESPONSE_OK)
    {
      empathy_server_sasl_handler_provide_password (priv->handler,
          gtk_entry_get_text (GTK_ENTRY (priv->entry)),
          gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->ticky)));
    }
  else
    {
      empathy_server_sasl_handler_cancel (priv->handler);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
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

static gboolean
password_dialog_grab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (user_data);

  if (!priv->grabbing)
    {
      GdkGrabStatus status = gdk_keyboard_grab (gtk_widget_get_window (widget),
          FALSE, gdk_event_get_time (event));

      if (status != GDK_GRAB_SUCCESS)
        DEBUG ("Could not grab keyboard; grab status was %u", status);
      else
        priv->grabbing = TRUE;
    }

  return FALSE;
}

static gboolean
password_dialog_ungrab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyPasswordDialogPriv *priv = GET_PRIV (user_data);

  if (priv->grabbing)
    {
      gdk_keyboard_ungrab (gdk_event_get_time (event));
      priv->grabbing = FALSE;
    }

  return FALSE;
}

static gboolean
password_dialog_window_state_changed (GtkWidget *widget,
    GdkEventWindowState *event,
    gpointer data)
{
  GdkWindowState state = gdk_window_get_state (gtk_widget_get_window (widget));

  if (state & GDK_WINDOW_STATE_WITHDRAWN
      || state & GDK_WINDOW_STATE_ICONIFIED
      || state & GDK_WINDOW_STATE_FULLSCREEN
      || state & GDK_WINDOW_STATE_MAXIMIZED)
    {
      password_dialog_ungrab_keyboard (widget, (GdkEvent*) event, data);
    }
  else
    {
      password_dialog_grab_keyboard (widget, (GdkEvent*) event, data);
    }

  return FALSE;
}

static void
empathy_password_dialog_constructed (GObject *object)
{
  EmpathyPasswordDialog *dialog;
  EmpathyPasswordDialogPriv *priv;
  TpAccount *account;
  GtkWidget *icon;
  GtkBox *box;
  gchar *text;

  dialog = EMPATHY_PASSWORD_DIALOG (object);
  priv = GET_PRIV (dialog);

  g_assert (priv->handler != NULL);

  priv->grabbing = FALSE;

  account = empathy_server_sasl_handler_get_account (priv->handler);

  /* dialog */
  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
      GTK_STOCK_OK, GTK_RESPONSE_OK,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      NULL);

  text = g_strdup_printf (_("Enter your password for account\n<b>%s</b>"),
      tp_account_get_display_name (account));
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), text);
  g_free (text);

  gtk_window_set_icon_name (GTK_WINDOW (dialog),
      GTK_STOCK_DIALOG_AUTHENTICATION);

  box = GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  /* dialog icon */
  icon = gtk_image_new_from_icon_name (tp_account_get_icon_name (account),
      GTK_ICON_SIZE_DIALOG);
  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (dialog), icon);
  gtk_widget_show (icon);

  /* entry */
  priv->entry = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (priv->entry), FALSE);

  /* entry clear icon */
  gtk_entry_set_icon_from_stock (GTK_ENTRY (priv->entry),
      GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
  gtk_entry_set_icon_sensitive (GTK_ENTRY (priv->entry),
      GTK_ENTRY_ICON_SECONDARY, FALSE);

  g_signal_connect (priv->entry, "icon-release",
      G_CALLBACK (clear_icon_released_cb), NULL);
  g_signal_connect (priv->entry, "changed",
      G_CALLBACK (password_entry_changed_cb), NULL);

  gtk_box_pack_start (box, priv->entry, FALSE, FALSE, 0);
  gtk_widget_show (priv->entry);

  /* remember password ticky box */
  priv->ticky = gtk_check_button_new_with_label (_("Remember password"));

  /* Don't add this to the dialog yet because we haven't set up
   * everything in the UI properly yet and the MC transition isn't
   * ready etc. so we'll just force it to never remember a
   * password. */
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->ticky), FALSE);
  /*
  gtk_box_pack_start (box, priv->ticky, FALSE, FALSE, 0);
  gtk_widget_show (priv->ticky);
  */

  g_signal_connect (dialog, "response",
      G_CALLBACK (password_dialog_response_cb), dialog);
  g_signal_connect (dialog, "window-state-event",
      G_CALLBACK (password_dialog_window_state_changed), dialog);
  g_signal_connect (dialog, "map-event",
      G_CALLBACK (password_dialog_grab_keyboard), dialog);
  g_signal_connect (dialog, "unmap-event",
      G_CALLBACK (password_dialog_ungrab_keyboard), dialog);

  gtk_widget_grab_focus (priv->entry);
}

static void
empathy_password_dialog_init (EmpathyPasswordDialog *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_PASSWORD_DIALOG, EmpathyPasswordDialogPriv);
}

static void
empathy_password_dialog_class_init (EmpathyPasswordDialogClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyPasswordDialogPriv));

  oclass->set_property = empathy_password_dialog_set_property;
  oclass->get_property = empathy_password_dialog_get_property;
  oclass->dispose = empathy_password_dialog_dispose;
  oclass->constructed = empathy_password_dialog_constructed;

  pspec = g_param_spec_object ("handler", "The EmpathyServerSASLHandler",
      "The EmpathyServerSASLHandler to be used.",
      EMPATHY_TYPE_SERVER_SASL_HANDLER,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_HANDLER, pspec);
}

GtkWidget *
empathy_password_dialog_new (EmpathyServerSASLHandler *handler)
{
  g_assert (EMPATHY_IS_SERVER_SASL_HANDLER (handler));

  return g_object_new (EMPATHY_TYPE_PASSWORD_DIALOG,
      "handler", handler, NULL);
}

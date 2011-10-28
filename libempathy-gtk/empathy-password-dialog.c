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

enum {
  PROP_HANDLER = 1,

  LAST_PROPERTY,
};

struct _EmpathyPasswordDialogPriv {
  EmpathyServerSASLHandler *handler;

  GtkWidget *entry;
  GtkWidget *ticky;
  GtkWidget *ok_button;

  gboolean grabbing;

  gboolean dispose_run;
};

static void
empathy_password_dialog_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) object;

  switch (property_id)
    {
    case PROP_HANDLER:
      g_value_set_object (value, self->priv->handler);
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
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) object;

  switch (property_id)
    {
    case PROP_HANDLER:
      g_assert (self->priv->handler == NULL); /* construct only */
      self->priv->handler = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_password_dialog_dispose (GObject *object)
{
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) object;

  if (self->priv->dispose_run)
    return;

  self->priv->dispose_run = TRUE;

  tp_clear_object (&self->priv->handler);

  G_OBJECT_CLASS (empathy_password_dialog_parent_class)->dispose (object);
}

static void
password_dialog_response_cb (GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) dialog;

  if (response == GTK_RESPONSE_OK)
    {
      empathy_server_sasl_handler_provide_password (self->priv->handler,
          gtk_entry_get_text (GTK_ENTRY (self->priv->entry)),
          gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->priv->ticky)));
    }
  else
    {
      empathy_server_sasl_handler_cancel (self->priv->handler);
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
  EmpathyPasswordDialog *self = user_data;
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (entry));

  gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, !EMP_STR_EMPTY (str));

  gtk_widget_set_sensitive (self->priv->ok_button,
      !EMP_STR_EMPTY (str));
}

static void
password_entry_activate_cb (GtkEntry *entry,
    EmpathyPasswordDialog *self)
{
  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
}

static gboolean
password_dialog_grab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyPasswordDialog *self = user_data;

  if (!self->priv->grabbing)
    {
      GdkDevice *device = gdk_event_get_device (event);

      if (device != NULL)
        {
          GdkGrabStatus status = gdk_device_grab (device,
              gtk_widget_get_window (widget),
              GDK_OWNERSHIP_WINDOW,
              FALSE,
              GDK_ALL_EVENTS_MASK,
              NULL,
              gdk_event_get_time (event));

          if (status != GDK_GRAB_SUCCESS)
            DEBUG ("Could not grab keyboard; grab status was %u", status);
          else
            self->priv->grabbing = TRUE;
        }
      else
        DEBUG ("Could not get the event device!");
    }

  return FALSE;
}

static gboolean
password_dialog_ungrab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyPasswordDialog *self = user_data;

  if (self->priv->grabbing)
    {
      GdkDevice *device = gdk_event_get_device (event);

      if (device != NULL)
        {
          gdk_device_ungrab (device, gdk_event_get_time (event));
          self->priv->grabbing = FALSE;
        }
      else
        DEBUG ("Could not get the event device!");
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
      password_dialog_ungrab_keyboard (widget, (GdkEvent *) event, data);
    }
  else
    {
      password_dialog_grab_keyboard (widget, (GdkEvent *) event, data);
    }

  return FALSE;
}

static void
password_dialog_handler_invalidated_cb (EmpathyServerSASLHandler *handler,
    EmpathyPasswordDialog *dialog)
{
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
empathy_password_dialog_constructed (GObject *object)
{
  EmpathyPasswordDialog *self;
  TpAccount *account;
  GtkWidget *icon;
  GtkBox *box;
  gchar *text;

  self = EMPATHY_PASSWORD_DIALOG (object);

  g_assert (self->priv->handler != NULL);

  self->priv->grabbing = FALSE;

  account = empathy_server_sasl_handler_get_account (self->priv->handler);

  tp_g_signal_connect_object (self->priv->handler, "invalidated",
      G_CALLBACK (password_dialog_handler_invalidated_cb),
      object, 0);

  /* dialog */
  gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  self->priv->ok_button = gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_OK, GTK_RESPONSE_OK);
  gtk_widget_set_sensitive (self->priv->ok_button, FALSE);

  text = g_strdup_printf (_("Enter your password for account\n<b>%s</b>"),
      tp_account_get_display_name (account));
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (self), text);
  g_free (text);

  gtk_window_set_icon_name (GTK_WINDOW (self),
      GTK_STOCK_DIALOG_AUTHENTICATION);

  box = GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self)));

  /* dialog icon */
  icon = gtk_image_new_from_icon_name (tp_account_get_icon_name (account),
      GTK_ICON_SIZE_DIALOG);
  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (self), icon);
  gtk_widget_show (icon);

  /* entry */
  self->priv->entry = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (self->priv->entry), FALSE);

  /* entry clear icon */
  gtk_entry_set_icon_from_stock (GTK_ENTRY (self->priv->entry),
      GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
  gtk_entry_set_icon_sensitive (GTK_ENTRY (self->priv->entry),
      GTK_ENTRY_ICON_SECONDARY, FALSE);

  g_signal_connect (self->priv->entry, "icon-release",
      G_CALLBACK (clear_icon_released_cb), NULL);
  g_signal_connect (self->priv->entry, "changed",
      G_CALLBACK (password_entry_changed_cb), self);
  g_signal_connect (self->priv->entry, "activate",
      G_CALLBACK (password_entry_activate_cb), self);

  gtk_box_pack_start (box, self->priv->entry, FALSE, FALSE, 0);
  gtk_widget_show (self->priv->entry);

  /* remember password ticky box */
  self->priv->ticky = gtk_check_button_new_with_label (_("Remember password"));

  gtk_box_pack_start (box, self->priv->ticky, FALSE, FALSE, 0);

  /* only show it if we actually support it */
  if (empathy_server_sasl_handler_can_save_response_somewhere (
        self->priv->handler))
    gtk_widget_show (self->priv->ticky);

  g_signal_connect (self, "response",
      G_CALLBACK (password_dialog_response_cb), self);
  g_signal_connect (self, "window-state-event",
      G_CALLBACK (password_dialog_window_state_changed), self);
  g_signal_connect (self, "map-event",
      G_CALLBACK (password_dialog_grab_keyboard), self);
  g_signal_connect (self, "unmap-event",
      G_CALLBACK (password_dialog_ungrab_keyboard), self);

  gtk_widget_grab_focus (self->priv->entry);

  gtk_window_set_position (GTK_WINDOW (self), GTK_WIN_POS_CENTER_ALWAYS);
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

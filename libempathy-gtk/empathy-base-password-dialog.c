/*
 * empathy-base-password-dialog.c - Source for EmpathyBasePasswordDialog
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

#include "empathy-base-password-dialog.h"

#include <glib/gi18n-lib.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>

G_DEFINE_TYPE (EmpathyBasePasswordDialog, empathy_base_password_dialog,
    GTK_TYPE_MESSAGE_DIALOG)

enum {
  PROP_ACCOUNT = 1,

  LAST_PROPERTY,
};

struct _EmpathyBasePasswordDialogPriv {
  gboolean grabbing;
};

static void
empathy_base_password_dialog_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyBasePasswordDialog *self = (EmpathyBasePasswordDialog *) object;

  switch (property_id)
    {
    case PROP_ACCOUNT:
      g_value_set_object (value, self->account);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_base_password_dialog_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyBasePasswordDialog *self = (EmpathyBasePasswordDialog *) object;

  switch (property_id)
    {
    case PROP_ACCOUNT:
      g_assert (self->account == NULL); /* construct only */
      self->account = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_base_password_dialog_dispose (GObject *object)
{
  EmpathyBasePasswordDialog *self = (EmpathyBasePasswordDialog *) object;

  tp_clear_object (&self->account);

  G_OBJECT_CLASS (empathy_base_password_dialog_parent_class)->dispose (object);
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
  EmpathyBasePasswordDialog *self = user_data;
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (entry));

  gtk_entry_set_icon_sensitive (GTK_ENTRY (entry),
      GTK_ENTRY_ICON_SECONDARY, !EMP_STR_EMPTY (str));

  gtk_widget_set_sensitive (self->ok_button,
      !EMP_STR_EMPTY (str));
}

static void
password_entry_activate_cb (GtkEntry *entry,
    EmpathyBasePasswordDialog *self)
{
  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_OK);
}

static gboolean
base_password_dialog_grab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyBasePasswordDialog *self = user_data;

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
base_password_dialog_ungrab_keyboard (GtkWidget *widget,
    GdkEvent *event,
    gpointer user_data)
{
  EmpathyBasePasswordDialog *self = user_data;

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
base_password_dialog_window_state_changed (GtkWidget *widget,
    GdkEventWindowState *event,
    gpointer data)
{
  GdkWindowState state = gdk_window_get_state (gtk_widget_get_window (widget));

  if (state & GDK_WINDOW_STATE_WITHDRAWN
      || state & GDK_WINDOW_STATE_ICONIFIED
      || state & GDK_WINDOW_STATE_FULLSCREEN
      || state & GDK_WINDOW_STATE_MAXIMIZED)
    {
      base_password_dialog_ungrab_keyboard (widget, (GdkEvent *) event, data);
    }
  else
    {
      base_password_dialog_grab_keyboard (widget, (GdkEvent *) event, data);
    }

  return FALSE;
}

static void
empathy_base_password_dialog_constructed (GObject *object)
{
  EmpathyBasePasswordDialog *self;
  GtkWidget *icon;
  GtkBox *box;
  gchar *text;

  self = EMPATHY_BASE_PASSWORD_DIALOG (object);

  g_assert (self->account != NULL);

  self->priv->grabbing = FALSE;

  /* dialog */
  gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  self->ok_button = gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_OK, GTK_RESPONSE_OK);
  gtk_widget_set_sensitive (self->ok_button, FALSE);

  text = g_strdup_printf (_("Enter your password for account\n<b>%s</b>"),
      tp_account_get_display_name (self->account));
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (self), text);
  g_free (text);

  gtk_window_set_icon_name (GTK_WINDOW (self),
      GTK_STOCK_DIALOG_AUTHENTICATION);

  box = GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (self)));

  /* dialog icon */
  icon = gtk_image_new_from_icon_name (
      tp_account_get_icon_name (self->account), GTK_ICON_SIZE_DIALOG);
  gtk_message_dialog_set_image (GTK_MESSAGE_DIALOG (self), icon);
  gtk_widget_show (icon);

  /* entry */
  self->entry = gtk_entry_new ();
  gtk_entry_set_visibility (GTK_ENTRY (self->entry), FALSE);

  /* entry clear icon */
  gtk_entry_set_icon_from_stock (GTK_ENTRY (self->entry),
      GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
  gtk_entry_set_icon_sensitive (GTK_ENTRY (self->entry),
      GTK_ENTRY_ICON_SECONDARY, FALSE);

  g_signal_connect (self->entry, "icon-release",
      G_CALLBACK (clear_icon_released_cb), NULL);
  g_signal_connect (self->entry, "changed",
      G_CALLBACK (password_entry_changed_cb), self);
  g_signal_connect (self->entry, "activate",
      G_CALLBACK (password_entry_activate_cb), self);

  gtk_box_pack_start (box, self->entry, FALSE, FALSE, 0);
  gtk_widget_show (self->entry);

  /* remember password ticky box */
  self->ticky = gtk_check_button_new_with_label (_("Remember password"));

  gtk_box_pack_start (box, self->ticky, FALSE, FALSE, 0);

  g_signal_connect (self, "window-state-event",
      G_CALLBACK (base_password_dialog_window_state_changed), self);
  g_signal_connect (self, "map-event",
      G_CALLBACK (base_password_dialog_grab_keyboard), self);
  g_signal_connect (self, "unmap-event",
      G_CALLBACK (base_password_dialog_ungrab_keyboard), self);

  gtk_widget_grab_focus (self->entry);

  gtk_window_set_position (GTK_WINDOW (self), GTK_WIN_POS_CENTER_ALWAYS);
}

static void
empathy_base_password_dialog_init (EmpathyBasePasswordDialog *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_BASE_PASSWORD_DIALOG, EmpathyBasePasswordDialogPriv);
}

static void
empathy_base_password_dialog_class_init (EmpathyBasePasswordDialogClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyBasePasswordDialogPriv));

  oclass->set_property = empathy_base_password_dialog_set_property;
  oclass->get_property = empathy_base_password_dialog_get_property;
  oclass->dispose = empathy_base_password_dialog_dispose;
  oclass->constructed = empathy_base_password_dialog_constructed;

  pspec = g_param_spec_object ("account", "The TpAccount",
      "The TpAccount to be used.",
      TP_TYPE_ACCOUNT,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_ACCOUNT, pspec);
}

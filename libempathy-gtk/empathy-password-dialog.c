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
    EMPATHY_TYPE_BASE_PASSWORD_DIALOG)

enum {
  PROP_HANDLER = 1,

  LAST_PROPERTY,
};

struct _EmpathyPasswordDialogPriv {
  EmpathyServerSASLHandler *handler;
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

  tp_clear_object (&self->priv->handler);

  G_OBJECT_CLASS (empathy_password_dialog_parent_class)->dispose (object);
}

static void
password_dialog_response_cb (GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) dialog;
  EmpathyBasePasswordDialog *base = (EmpathyBasePasswordDialog *) dialog;

  if (response == GTK_RESPONSE_OK)
    {
      empathy_server_sasl_handler_provide_password (self->priv->handler,
          gtk_entry_get_text (GTK_ENTRY (base->entry)),
          gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (base->ticky)));
    }
  else
    {
      empathy_server_sasl_handler_cancel (self->priv->handler);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
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
  EmpathyPasswordDialog *self = (EmpathyPasswordDialog *) object;
  EmpathyBasePasswordDialog *base = (EmpathyBasePasswordDialog *) object;
  gchar *text;

  G_OBJECT_CLASS (empathy_password_dialog_parent_class)->constructed (object);

  tp_g_signal_connect_object (self->priv->handler, "invalidated",
      G_CALLBACK (password_dialog_handler_invalidated_cb),
      object, 0);

  text = g_strdup_printf (_("Enter your password for account\n<b>%s</b>"),
      tp_account_get_display_name (base->account));
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (self), text);
  g_free (text);

  /* only show it if we actually support it */
  if (empathy_server_sasl_handler_can_save_response_somewhere (
        self->priv->handler))
    gtk_widget_show (base->ticky);

  g_signal_connect (self, "response",
      G_CALLBACK (password_dialog_response_cb), self);
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
      "handler", handler,
      "account", empathy_server_sasl_handler_get_account (handler),
      NULL);
}

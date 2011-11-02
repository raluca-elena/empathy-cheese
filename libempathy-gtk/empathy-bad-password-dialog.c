/*
 * empathy-bad-password-dialog.c - Source for EmpathyBadPasswordDialog
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

#include "empathy-bad-password-dialog.h"

#include <glib/gi18n-lib.h>

#define DEBUG_FLAG EMPATHY_DEBUG_SASL
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>

G_DEFINE_TYPE (EmpathyBadPasswordDialog, empathy_bad_password_dialog,
    EMPATHY_TYPE_BASE_PASSWORD_DIALOG)

enum {
  PROP_PASSWORD = 1,

  LAST_PROPERTY,
};

/* signal enum */
enum {
  RETRY,
  LAST_SIGNAL,
};

static guint signals[LAST_SIGNAL] = {0};

struct _EmpathyBadPasswordDialogPriv {
  gchar *password;
};

static void
empathy_bad_password_dialog_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyBadPasswordDialog *self = (EmpathyBadPasswordDialog *) object;

  switch (property_id)
    {
    case PROP_PASSWORD:
      g_value_set_string (value, self->priv->password);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_bad_password_dialog_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyBadPasswordDialog *self = (EmpathyBadPasswordDialog *) object;

  switch (property_id)
    {
    case PROP_PASSWORD:
      g_assert (self->priv->password == NULL); /* construct only */
      self->priv->password = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_bad_password_dialog_finalize (GObject *object)
{
  EmpathyBadPasswordDialog *self = (EmpathyBadPasswordDialog *) object;

  tp_clear_pointer (&self->priv->password, g_free);

  G_OBJECT_CLASS (empathy_bad_password_dialog_parent_class)->finalize (object);
}

static void
bad_password_dialog_response_cb (GtkDialog *dialog,
    gint response,
    gpointer user_data)
{
  EmpathyBadPasswordDialog *self = (EmpathyBadPasswordDialog *) dialog;
  EmpathyBasePasswordDialog *base = (EmpathyBasePasswordDialog *) dialog;

  if (response == GTK_RESPONSE_OK)
    {
      const gchar *password;

      password = gtk_entry_get_text (GTK_ENTRY (base->entry));

      g_signal_emit (self, signals[RETRY], 0, base->account, password);
    }

  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
empathy_bad_password_dialog_constructed (GObject *object)
{
  EmpathyBadPasswordDialog *self = (EmpathyBadPasswordDialog *) object;
  EmpathyBasePasswordDialog *base = (EmpathyBasePasswordDialog *) object;
  gchar *text;

  G_OBJECT_CLASS (empathy_bad_password_dialog_parent_class)->constructed (
      object);

  text = g_strdup_printf (_("Authentification failed for account <b>%s</b>"),
      tp_account_get_display_name (base->account));
  gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (self), text);
  g_free (text);

  if (self->priv->password != NULL)
    {
      gtk_entry_set_text (GTK_ENTRY (base->entry), self->priv->password);

      gtk_editable_select_region (GTK_EDITABLE (base->entry), 0, -1);
    }

  gtk_button_set_label (GTK_BUTTON (base->ok_button), _("Retry"));

  g_signal_connect (self, "response",
      G_CALLBACK (bad_password_dialog_response_cb), self);
}

static void
empathy_bad_password_dialog_init (EmpathyBadPasswordDialog *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_BAD_PASSWORD_DIALOG, EmpathyBadPasswordDialogPriv);
}

static void
empathy_bad_password_dialog_class_init (EmpathyBadPasswordDialogClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyBadPasswordDialogPriv));

  oclass->set_property = empathy_bad_password_dialog_set_property;
  oclass->get_property = empathy_bad_password_dialog_get_property;
  oclass->finalize = empathy_bad_password_dialog_finalize;
  oclass->constructed = empathy_bad_password_dialog_constructed;

  pspec = g_param_spec_string ("password", "Password",
      "The wrong password",
      NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_PASSWORD, pspec);

  signals[RETRY] = g_signal_new ("retry",
      G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      NULL, NULL,
      g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, TP_TYPE_ACCOUNT, G_TYPE_STRING);
}

GtkWidget *
empathy_bad_password_dialog_new (TpAccount *account,
    const gchar *password)
{
  g_return_val_if_fail (TP_IS_ACCOUNT (account), NULL);

  return g_object_new (EMPATHY_TYPE_BAD_PASSWORD_DIALOG,
      "account", account,
      "password", password,
      NULL);
}

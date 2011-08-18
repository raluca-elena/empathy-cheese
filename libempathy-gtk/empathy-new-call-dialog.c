/*
 * Copyright (C) 2009 Collabora Ltd.
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
 *
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/interfaces.h>

#include <telepathy-yell/telepathy-yell.h>

#include <libempathy/empathy-tp-contact-factory.h>
#include <libempathy/empathy-camera-monitor.h>
#include <libempathy/empathy-contact-manager.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-request-util.h>

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-new-call-dialog.h"
#include "empathy-account-chooser.h"
#include "empathy-call-utils.h"

static EmpathyNewCallDialog *dialog_singleton = NULL;

G_DEFINE_TYPE(EmpathyNewCallDialog, empathy_new_call_dialog,
               EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG)

typedef struct _EmpathyNewCallDialogPriv EmpathyNewCallDialogPriv;

typedef struct {
  EmpathyAccountChooserFilterResultCallback callback;
  gpointer                                  user_data;
} FilterCallbackData;

struct _EmpathyNewCallDialogPriv {
  GtkWidget *check_video;

  EmpathyCameraMonitor *monitor;
};

#define GET_PRIV(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), EMPATHY_TYPE_NEW_CALL_DIALOG, \
    EmpathyNewCallDialogPriv))

/**
 * SECTION:empathy-new-call-dialog
 * @title: EmpathyNewCallDialog
 * @short_description: A dialog to show a new call
 * @include: libempathy-gtk/empathy-new-call-dialog.h
 *
 * #EmpathyNewCallDialog is a dialog which allows a call
 * to be started with any contact on any enabled account.
 */

static void
empathy_new_call_dialog_response (GtkDialog *dialog, int response_id)
{
  EmpathyNewCallDialogPriv *priv = GET_PRIV (dialog);
  gboolean video;
  TpAccount *account;
  const gchar *contact_id;

  if (response_id != GTK_RESPONSE_ACCEPT) goto out;

  contact_id = empathy_contact_selector_dialog_get_selected (
      EMPATHY_CONTACT_SELECTOR_DIALOG (dialog), NULL, &account);

  if (EMP_STR_EMPTY (contact_id) || account == NULL) goto out;

  /* check if video is enabled now because the dialog will be destroyed once
   * we return from this function. */
  video = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->check_video));

  empathy_call_new_with_streams (contact_id,
      account, TRUE, video,
      empathy_get_current_action_time ());

out:
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
empathy_new_call_dialog_account_filter (EmpathyContactSelectorDialog *dialog,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    TpAccount *account)
{
  TpConnection *connection;
  gboolean supported = FALSE;
  guint i;
  TpCapabilities *caps;
  GPtrArray *classes;

  /* check if CM supports calls */
  connection = tp_account_get_connection (account);
  if (connection == NULL)
      goto out;

  caps = tp_connection_get_capabilities (connection);
  if (caps == NULL)
      goto out;

  classes = tp_capabilities_get_channel_classes (caps);

  for (i = 0; i < classes->len; i++)
    {
      GHashTable *fixed;
      GStrv allowed;
      const gchar *chan_type;

      tp_value_array_unpack (g_ptr_array_index (classes, i), 2,
          &fixed, &allowed);

      chan_type = tp_asv_get_string (fixed, TP_PROP_CHANNEL_CHANNEL_TYPE);

      if (tp_strdiff (chan_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA)
          && tp_strdiff (chan_type, TPY_IFACE_CHANNEL_TYPE_CALL))
        continue;

      if (tp_asv_get_uint32 (fixed, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL) !=
          TP_HANDLE_TYPE_CONTACT)
        continue;

      supported = TRUE;
      break;
    }

out:
  callback (supported, callback_data);
}

static void
empathy_new_call_dialog_dispose (GObject *object)
{
  EmpathyNewCallDialogPriv *priv = GET_PRIV (object);

  tp_clear_object (&priv->monitor);

  G_OBJECT_CLASS (empathy_new_call_dialog_parent_class)->dispose (object);
}

static GObject *
empathy_new_call_dialog_constructor (GType type,
    guint n_props,
    GObjectConstructParam *props)
{
  GObject *retval;

  if (dialog_singleton)
    {
      retval = G_OBJECT (dialog_singleton);
      g_object_ref (retval);
    }
  else
    {
      retval = G_OBJECT_CLASS (
      empathy_new_call_dialog_parent_class)->constructor (type,
        n_props, props);

      dialog_singleton = EMPATHY_NEW_CALL_DIALOG (retval);
      g_object_add_weak_pointer (retval, (gpointer) &dialog_singleton);
    }

  return retval;
}

static void
empathy_new_call_dialog_init (EmpathyNewCallDialog *dialog)
{
  EmpathyContactSelectorDialog *parent = EMPATHY_CONTACT_SELECTOR_DIALOG (
        dialog);
  EmpathyNewCallDialogPriv *priv = GET_PRIV (dialog);
  GtkWidget *image;

  priv->monitor = empathy_camera_monitor_dup_singleton ();

  /* add video toggle */
  priv->check_video = gtk_check_button_new_with_mnemonic (_("Send _Video"));
  g_object_bind_property (priv->monitor, "available",
      priv->check_video, "sensitive",
      G_BINDING_SYNC_CREATE);

  gtk_box_pack_end (GTK_BOX (parent->vbox), priv->check_video,
      FALSE, TRUE, 0);

  gtk_widget_show (priv->check_video);

  /* add chat button */
  parent->button_action = gtk_button_new_with_mnemonic (_("C_all"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (parent->button_action), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), parent->button_action,
      GTK_RESPONSE_ACCEPT);
  gtk_widget_show (parent->button_action);

  /* Tweak the dialog */
  gtk_window_set_title (GTK_WINDOW (dialog), _("New Call"));
  gtk_window_set_role (GTK_WINDOW (dialog), "new_call");

  gtk_widget_set_sensitive (parent->button_action, FALSE);
}

static void
empathy_new_call_dialog_class_init (
  EmpathyNewCallDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (class);
  EmpathyContactSelectorDialogClass *selector_dialog_class = \
    EMPATHY_CONTACT_SELECTOR_DIALOG_CLASS (class);

  g_type_class_add_private (class, sizeof (EmpathyNewCallDialogPriv));

  object_class->constructor = empathy_new_call_dialog_constructor;
  object_class->dispose = empathy_new_call_dialog_dispose;

  dialog_class->response = empathy_new_call_dialog_response;

  selector_dialog_class->account_filter = empathy_new_call_dialog_account_filter;
}

/**
 * empathy_new_call_dialog_new:
 * @parent: parent #GtkWindow of the dialog
 *
 * Create a new #EmpathyNewCallDialog it.
 *
 * Return value: the new #EmpathyNewCallDialog
 */
GtkWidget *
empathy_new_call_dialog_show (GtkWindow *parent)
{
  GtkWidget *dialog;

  dialog = g_object_new (EMPATHY_TYPE_NEW_CALL_DIALOG, NULL);

  if (parent)
    {
      gtk_window_set_transient_for (GTK_WINDOW (dialog),
                  GTK_WINDOW (parent));
    }

  gtk_widget_show (dialog);
  return dialog;
}

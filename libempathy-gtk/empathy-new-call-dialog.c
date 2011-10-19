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

#include <libempathy-gtk/empathy-contact-chooser.h>
#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-images.h>

#include "empathy-new-call-dialog.h"
#include "empathy-account-chooser.h"
#include "empathy-call-utils.h"

static EmpathyNewCallDialog *dialog_singleton = NULL;

G_DEFINE_TYPE(EmpathyNewCallDialog, empathy_new_call_dialog,
               GTK_TYPE_DIALOG)

struct _EmpathyNewCallDialogPriv {
  GtkWidget *chooser;
  GtkWidget *button_audio;
  GtkWidget *button_video;

  EmpathyCameraMonitor *monitor;
};

/* Re-use the accept and ok Gtk response so we are sure they won't be used
 * when the dialog window is closed for example */
enum
{
  RESPONSE_AUDIO = GTK_RESPONSE_ACCEPT,
  RESPONSE_VIDEO = GTK_RESPONSE_OK,
};

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
empathy_new_call_dialog_response (GtkDialog *dialog,
    int response_id)
{
  EmpathyNewCallDialog *self = (EmpathyNewCallDialog *) dialog;
  FolksIndividual *individual;
  EmpathyContact *contact;

  if (response_id != RESPONSE_AUDIO &&
      response_id != RESPONSE_VIDEO)
    goto out;

  individual = empathy_contact_chooser_dup_selected (
      EMPATHY_CONTACT_CHOOSER (self->priv->chooser));
  if (individual == NULL) goto out;

  empathy_individual_can_audio_video_call (individual, NULL, NULL, &contact);
  g_assert (contact != NULL);

  empathy_call_new_with_streams (empathy_contact_get_id (contact),
      empathy_contact_get_account (contact), TRUE,
      response_id == RESPONSE_VIDEO, empathy_get_current_action_time ());

  g_object_unref (individual);
  g_object_unref (contact);

out:
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
empathy_new_call_dialog_dispose (GObject *object)
{
  EmpathyNewCallDialog *self = (EmpathyNewCallDialog *) object;

  tp_clear_object (&self->priv->monitor);

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

static gboolean
filter_individual (EmpathyContactChooser *chooser,
    FolksIndividual *individual,
    gboolean is_online,
    gboolean searching,
    gpointer user_data)
{
  gboolean can_audio_call, can_video_call;

  empathy_individual_can_audio_video_call (individual, &can_audio_call,
      &can_video_call, NULL);

  return can_audio_call || can_video_call;
}

static void
selection_changed_cb (GtkWidget *chooser,
    FolksIndividual *selected,
    EmpathyNewCallDialog *self)
{
  gboolean can_audio_call, can_video_call;

  if (selected == NULL)
    {
      can_audio_call = can_video_call = FALSE;
    }
  else
    {
      empathy_individual_can_audio_video_call (selected, &can_audio_call,
          &can_video_call, NULL);
    }

  gtk_widget_set_sensitive (self->priv->button_audio, can_audio_call);
  gtk_widget_set_sensitive (self->priv->button_video, can_video_call);
}

static void
selection_activate_cb (GtkWidget *chooser,
    EmpathyNewCallDialog *self)
{
  gtk_dialog_response (GTK_DIALOG (self), RESPONSE_AUDIO);
}

static void
empathy_new_call_dialog_init (EmpathyNewCallDialog *self)
{
  GtkWidget *label;
  GtkWidget *image;
  GtkWidget *content;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_NEW_CALL_DIALOG, EmpathyNewCallDialogPriv);

  self->priv->monitor = empathy_camera_monitor_dup_singleton ();

  content = gtk_dialog_get_content_area (GTK_DIALOG (self));

  label = gtk_label_new (_("Enter a contact identifier or phone number:"));
  gtk_box_pack_start (GTK_BOX (content), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  /* contact chooser */
  self->priv->chooser = empathy_contact_chooser_new ();

  empathy_contact_chooser_set_filter_func (
      EMPATHY_CONTACT_CHOOSER (self->priv->chooser), filter_individual, self);

  gtk_box_pack_start (GTK_BOX (content), self->priv->chooser, TRUE, TRUE, 6);
  gtk_widget_show (self->priv->chooser);

  g_signal_connect (self->priv->chooser, "selection-changed",
      G_CALLBACK (selection_changed_cb), self);
  g_signal_connect (self->priv->chooser, "activate",
      G_CALLBACK (selection_activate_cb), self);

  /* close button */
  gtk_dialog_add_button (GTK_DIALOG (self),
      GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE);

  /* add video button */
  self->priv->button_video = gtk_button_new_with_mnemonic (_("_Video Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VIDEO_CALL,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (self->priv->button_video), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (self), self->priv->button_video,
      RESPONSE_VIDEO);
  gtk_widget_show (self->priv->button_video);

  /* add audio button */
  self->priv->button_audio = gtk_button_new_with_mnemonic (_("_Audio Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP,
      GTK_ICON_SIZE_BUTTON);
  gtk_button_set_image (GTK_BUTTON (self->priv->button_audio), image);

  gtk_dialog_add_action_widget (GTK_DIALOG (self), self->priv->button_audio,
      RESPONSE_AUDIO);
  gtk_widget_show (self->priv->button_audio);

  /* Tweak the dialog */
  gtk_window_set_title (GTK_WINDOW (self), _("New Call"));
  gtk_window_set_role (GTK_WINDOW (self), "new_call");

  /* Set a default height so a few contacts are displayed */
  gtk_window_set_default_size (GTK_WINDOW (self), -1, 400);

  gtk_widget_set_sensitive (self->priv->button_audio, FALSE);
  gtk_widget_set_sensitive (self->priv->button_video, FALSE);
}

static void
empathy_new_call_dialog_class_init (
  EmpathyNewCallDialogClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);
  GtkDialogClass *dialog_class = GTK_DIALOG_CLASS (class);

  g_type_class_add_private (class, sizeof (EmpathyNewCallDialogPriv));

  object_class->constructor = empathy_new_call_dialog_constructor;
  object_class->dispose = empathy_new_call_dialog_dispose;

  dialog_class->response = empathy_new_call_dialog_response;
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

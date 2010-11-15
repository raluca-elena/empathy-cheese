/*
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
 *
 * Authors: Philip Withnall <philip.withnall@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-linking-dialog.h"
#include "empathy-individual-linker.h"
#include "empathy-ui-utils.h"

/**
 * SECTION:empathy-individual-widget
 * @title:EmpathyLinkingDialog
 * @short_description: A dialog used to link individuals together
 * @include: libempathy-empathy-linking-dialog.h
 *
 * #EmpathyLinkingDialog is a dialog which allows selection of individuals to
 * link together, and preview of the newly linked individual. When submitted, it
 * pushes the new links to backing storage.
 */

/**
 * EmpathyLinkingDialog:
 * @parent: parent object
 *
 * Widget which displays appropriate widgets with details about an individual,
 * also allowing changing these details, if desired.
 *
 * Currently, it's just a thin wrapper around #EmpathyContactWidget, and
 * displays the details of the first eligible persona found in the individual.
 */

static GtkWidget *linking_dialog = NULL;

/* Fairly arbitrary response ID for the "Unlink" button */
#define RESPONSE_UNLINK 5

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyLinkingDialog)

typedef struct {
  EmpathyIndividualLinker *linker; /* child widget */
  GtkWidget *link_button; /* child widget */
} EmpathyLinkingDialogPriv;

G_DEFINE_TYPE (EmpathyLinkingDialog, empathy_linking_dialog,
    GTK_TYPE_DIALOG);

static void
linker_notify_has_changed_cb (EmpathyIndividualLinker *linker,
    GParamSpec *pspec,
    EmpathyLinkingDialog *self)
{
  EmpathyLinkingDialogPriv *priv = GET_PRIV (self);

  /* Only make the "Link" button sensitive if the linked Individual has been
   * changed. */
  gtk_widget_set_sensitive (priv->link_button,
      empathy_individual_linker_get_has_changed (linker));
}

static void
empathy_linking_dialog_class_init (EmpathyLinkingDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  g_type_class_add_private (object_class, sizeof (EmpathyLinkingDialogPriv));
}

static void
empathy_linking_dialog_init (EmpathyLinkingDialog *self)
{
  EmpathyLinkingDialogPriv *priv;
  GtkDialog *dialog;
  GtkWidget *button;
  GtkBox *content_area;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_LINKING_DIALOG, EmpathyLinkingDialogPriv);
  self->priv = priv;

  dialog = GTK_DIALOG (self);

  /* Set up dialog */
  gtk_window_set_resizable (GTK_WINDOW (self), TRUE);
  /* Translators: this is the title of the linking dialogue (reached by
   * right-clicking on a contact and selecting "Link…"). "Link" in this title
   * is a verb. */
  gtk_window_set_title (GTK_WINDOW (self), _("Link Contacts"));
  gtk_widget_set_size_request (GTK_WIDGET (self), 600, 500);

  /* Unlink button */
  button = gtk_button_new_with_mnemonic (
      C_("Unlink individual (button)", "_Unlink…"));
  gtk_widget_set_tooltip_text (button, _("Completely split the displayed "
      "meta-contact into the contacts it contains."));
  gtk_dialog_add_action_widget (dialog, button, RESPONSE_UNLINK);
  gtk_widget_show (button);

  /* Cancel button */
  button = gtk_button_new_with_label (GTK_STOCK_CANCEL);
  gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
  gtk_dialog_add_action_widget (dialog, button, GTK_RESPONSE_CANCEL);
  gtk_widget_show (button);

  /* Add button */
  /* Translators: this is an action button in the linking dialogue. "Link" is
   * used here as a verb meaning "to connect two contacts to form a
   * meta-contact". */
  priv->link_button = gtk_button_new_with_mnemonic (_("_Link"));
  gtk_dialog_add_action_widget (dialog, priv->link_button, GTK_RESPONSE_OK);
  gtk_widget_show (priv->link_button);

  /* Linker widget */
  priv->linker =
      EMPATHY_INDIVIDUAL_LINKER (empathy_individual_linker_new (NULL));
  g_signal_connect (priv->linker, "notify::has-changed",
      (GCallback) linker_notify_has_changed_cb, self);

  gtk_container_set_border_width (GTK_CONTAINER (priv->linker), 8);
  content_area = GTK_BOX (gtk_dialog_get_content_area (dialog));
  gtk_box_pack_start (content_area, GTK_WIDGET (priv->linker), TRUE, TRUE, 0);
  gtk_widget_show (GTK_WIDGET (priv->linker));
}

static void
linking_response_cb (EmpathyLinkingDialog *self,
    gint response,
    gpointer user_data)
{
  EmpathyLinkingDialogPriv *priv = GET_PRIV (self);

  if (response == GTK_RESPONSE_OK)
    {
      EmpathyIndividualManager *manager;
      GList *personas;

      manager = empathy_individual_manager_dup_singleton ();

      personas = empathy_individual_linker_get_linked_personas (priv->linker);
      empathy_individual_manager_link_personas (manager, personas);

      g_object_unref (manager);
    }
  else if (response == RESPONSE_UNLINK)
    {
      EmpathyIndividualManager *manager;
      FolksIndividual *individual;
      GtkWidget *dialog;

      individual =
          empathy_individual_linker_get_start_individual (priv->linker);

      /* Show a confirmation dialogue first */
      dialog = gtk_message_dialog_new (GTK_WINDOW (self), GTK_DIALOG_MODAL,
          GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, _("Unlink meta-contact '%s'?"),
          folks_aliasable_get_alias (FOLKS_ALIASABLE (individual)));
      gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
          _("Are you sure you want to unlink this meta-contact? This will "
            "completely split the meta-contact into the contacts it "
            "contains."));
      gtk_dialog_add_buttons (GTK_DIALOG (dialog),
          GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
          C_("Unlink individual (button)", "_Unlink"), GTK_RESPONSE_OK,
          NULL);

      if (gtk_dialog_run (GTK_DIALOG (dialog)) != GTK_RESPONSE_OK)
        {
          gtk_widget_destroy (dialog);
          return;
        }

      gtk_widget_destroy (dialog);

      manager = empathy_individual_manager_dup_singleton ();
      empathy_individual_manager_unlink_individual (manager, individual);
      g_object_unref (manager);
    }

  linking_dialog = NULL;
  gtk_widget_destroy (GTK_WIDGET (self));
}

/**
 * empathy_linking_dialog_show:
 * @individual: the #FolksIndividual to start linking against
 * @parent: a parent window for the dialogue, or %NULL
 *
 * Create and show the linking dialogue, with @individual selected as the
 * individual to link to. If the dialogue is already being shown, raise it and
 * reset it so the start individual is @individual.
 *
 * Return value: the linking dialog
 */
GtkWidget *
empathy_linking_dialog_show (FolksIndividual *individual,
    GtkWindow *parent)
{
  EmpathyLinkingDialogPriv *priv;
  GList *personas, *l;
  guint num_personas = 0;

  /* Create the dialogue if it doesn't exist */
  if (linking_dialog == NULL)
    {
      linking_dialog = GTK_WIDGET (g_object_new (EMPATHY_TYPE_LINKING_DIALOG,
          NULL));

      g_signal_connect (linking_dialog, "response",
          (GCallback) linking_response_cb, NULL);
    }

  priv = GET_PRIV (linking_dialog);

  if (parent != NULL)
    gtk_window_set_transient_for (GTK_WINDOW (linking_dialog), parent);

  empathy_individual_linker_set_start_individual (priv->linker, individual);

  /* Count how many Telepathy personas we have, to see whether we can
   * unlink */
  personas = folks_individual_get_personas (individual);
  for (l = personas; l != NULL; l = l->next)
    {
      if (TPF_IS_PERSONA (l->data))
        num_personas++;
    }

  /* Only make the "Unlink" button sensitive if we have enough personas */
  gtk_dialog_set_response_sensitive (GTK_DIALOG (linking_dialog),
      RESPONSE_UNLINK, (num_personas > 1) ? TRUE : FALSE);

  gtk_window_present (GTK_WINDOW (linking_dialog));

  return linking_dialog;
}

EmpathyIndividualLinker *
empathy_linking_dialog_get_individual_linker (EmpathyLinkingDialog *self)
{
  g_return_val_if_fail (EMPATHY_IS_LINKING_DIALOG (self), NULL);

  return GET_PRIV (self)->linker;
}

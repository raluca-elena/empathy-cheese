/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include <config.h>

#include <string.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>
#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-individual-information-dialog.h"
#include "empathy-individual-widget.h"
#include "empathy-ui-utils.h"

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIndividualInformationDialog)
typedef struct {
  FolksIndividual *individual;
  GtkWidget *individual_widget; /* child widget */
  GtkWidget *label; /* child widget */
} EmpathyIndividualInformationDialogPriv;

enum {
  PROP_0,
  PROP_INDIVIDUAL,
};

/* Info dialogs currently open.
 * Each dialog contains a referenced pointer to its Individual */
static GList *information_dialogs = NULL;

static void individual_information_dialog_set_individual (
    EmpathyIndividualInformationDialog *dialog,
    FolksIndividual *individual);

G_DEFINE_TYPE (EmpathyIndividualInformationDialog,
    empathy_individual_information_dialog, GTK_TYPE_DIALOG);

static void
individual_dialogs_response_cb (GtkDialog *dialog,
    gint response,
    GList **dialogs)
{
  *dialogs = g_list_remove (*dialogs, dialog);
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static gint
individual_dialogs_find (GObject *object,
    FolksIndividual *individual)
{
  EmpathyIndividualInformationDialogPriv *priv = GET_PRIV (object);

  return individual != priv->individual;
}

void
empathy_individual_information_dialog_show (FolksIndividual *individual,
    GtkWindow *parent)
{
  GtkWidget *dialog;
  GList *l;

  g_return_if_fail (FOLKS_IS_INDIVIDUAL (individual));
  g_return_if_fail (parent == NULL || GTK_IS_WINDOW (parent));

  l = g_list_find_custom (information_dialogs, individual,
      (GCompareFunc) individual_dialogs_find);

  if (l != NULL)
    {
      gtk_window_present (GTK_WINDOW (l->data));
      return;
    }

  /* Create dialog */
  dialog = g_object_new (EMPATHY_TYPE_INDIVIDUAL_INFORMATION_DIALOG,
      "individual", individual,
      NULL);

  information_dialogs = g_list_prepend (information_dialogs, dialog);
  gtk_widget_show (dialog);
}

static void
individual_removed_cb (FolksIndividual *individual,
    FolksIndividual *replacement_individual,
    EmpathyIndividualInformationDialog *self)
{
  individual_information_dialog_set_individual (self,
      replacement_individual);

  /* Destroy the dialogue */
  if (replacement_individual == NULL)
    {
      individual_dialogs_response_cb (GTK_DIALOG (self),
          GTK_RESPONSE_DELETE_EVENT, &information_dialogs);
    }
}

static void
set_label_visibility (EmpathyIndividualInformationDialog *dialog)
{
  EmpathyIndividualInformationDialogPriv *priv = GET_PRIV (dialog);
  GList *personas, *l;
  guint num_personas = 0;

  /* Count how many Telepathy personas we have, to see whether we can
   * unlink */
  if (priv->individual != NULL)
    {
      personas = folks_individual_get_personas (priv->individual);
      for (l = personas; l != NULL; l = l->next)
        {
          if (empathy_folks_persona_is_interesting (FOLKS_PERSONA (l->data)))
            num_personas++;
        }
    }

  /* Only make the label visible if we have enough personas */
  gtk_widget_set_visible (priv->label, (num_personas > 1) ? TRUE : FALSE);
}

static void
individual_information_dialog_set_individual (
    EmpathyIndividualInformationDialog *dialog,
    FolksIndividual *individual)
{
  EmpathyIndividualInformationDialogPriv *priv;

  g_return_if_fail (EMPATHY_INDIVIDUAL_INFORMATION_DIALOG (dialog));
  g_return_if_fail (individual == NULL || FOLKS_IS_INDIVIDUAL (individual));

  priv = GET_PRIV (dialog);

  /* Remove the old Individual */
  if (priv->individual != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->individual,
          (GCallback) individual_removed_cb, dialog);
    }

  tp_clear_object (&priv->individual);

  /* Add the new Individual */
  priv->individual = individual;

  if (individual != NULL)
    {
      g_object_ref (individual);
      g_signal_connect (individual, "removed",
          (GCallback) individual_removed_cb, dialog);

      /* Update the UI */
      gtk_window_set_title (GTK_WINDOW (dialog),
          folks_aliasable_get_alias (FOLKS_ALIASABLE (individual)));
      empathy_individual_widget_set_individual (
          EMPATHY_INDIVIDUAL_WIDGET (priv->individual_widget), individual);
      set_label_visibility (dialog);
    }
}

static void
individual_information_dialog_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualInformationDialogPriv *priv = GET_PRIV (object);

  switch (param_id) {
  case PROP_INDIVIDUAL:
    g_value_set_object (value, priv->individual);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
    break;
  };
}

static void
individual_information_dialog_set_property (GObject *object,
  guint param_id,
  const GValue *value,
  GParamSpec   *pspec)
{
  EmpathyIndividualInformationDialog *dialog =
    EMPATHY_INDIVIDUAL_INFORMATION_DIALOG (object);

  switch (param_id) {
  case PROP_INDIVIDUAL:
    individual_information_dialog_set_individual (dialog,
        FOLKS_INDIVIDUAL (g_value_get_object (value)));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
    break;
  };
}

static void
individual_information_dialog_dispose (GObject *object)
{
  individual_information_dialog_set_individual (
      EMPATHY_INDIVIDUAL_INFORMATION_DIALOG (object), NULL);

  G_OBJECT_CLASS (
      empathy_individual_information_dialog_parent_class)->dispose (object);
}

static void
empathy_individual_information_dialog_class_init (
    EmpathyIndividualInformationDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = individual_information_dialog_dispose;
  object_class->get_property = individual_information_dialog_get_property;
  object_class->set_property = individual_information_dialog_set_property;

  g_object_class_install_property (object_class,
      PROP_INDIVIDUAL,
      g_param_spec_object ("individual",
          "Folks Individual",
          "Folks Individual to base the dialog upon",
          FOLKS_TYPE_INDIVIDUAL,
          G_PARAM_CONSTRUCT |
          G_PARAM_READWRITE |
          G_PARAM_STATIC_STRINGS));

  g_type_class_add_private (object_class,
      sizeof (EmpathyIndividualInformationDialogPriv));
}

static void
empathy_individual_information_dialog_init (
    EmpathyIndividualInformationDialog *dialog)
{
  GtkWidget *button;
  GtkBox *content_area;
  gchar *label_string;
  EmpathyIndividualInformationDialogPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (
      dialog, EMPATHY_TYPE_INDIVIDUAL_INFORMATION_DIALOG,
      EmpathyIndividualInformationDialogPriv);

  dialog->priv = priv;
  priv->individual = NULL;

  gtk_window_set_resizable (GTK_WINDOW (dialog), TRUE);

  content_area = GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog)));

  /* Translators: the heading at the top of the Information dialogue */
  label_string = g_strdup_printf ("<b>%s</b>", _("Linked Contacts"));
  priv->label = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL (priv->label), label_string);
  g_free (label_string);

  gtk_misc_set_alignment (GTK_MISC (priv->label), 0.0, 0.5);
  gtk_misc_set_padding (GTK_MISC (priv->label), 6, 6);
  gtk_box_pack_start (content_area, priv->label, FALSE, TRUE, 0);
  gtk_widget_show (priv->label);

  /* Individual widget */
  priv->individual_widget = empathy_individual_widget_new (priv->individual,
      EMPATHY_INDIVIDUAL_WIDGET_SHOW_LOCATION |
      EMPATHY_INDIVIDUAL_WIDGET_SHOW_DETAILS |
      EMPATHY_INDIVIDUAL_WIDGET_SHOW_PERSONAS);
  gtk_container_set_border_width (GTK_CONTAINER (priv->individual_widget), 6);
  gtk_box_pack_start (content_area, priv->individual_widget, TRUE, TRUE, 0);
  gtk_widget_show (priv->individual_widget);

  /* Close button */
  button = gtk_button_new_with_label (GTK_STOCK_CLOSE);
  gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
  gtk_dialog_add_action_widget (GTK_DIALOG (dialog), button,
      GTK_RESPONSE_CLOSE);
  gtk_widget_set_can_default (button, TRUE);
  gtk_window_set_default (GTK_WINDOW (dialog), button);
  gtk_widget_show (button);

  g_signal_connect (dialog, "response",
      G_CALLBACK (individual_dialogs_response_cb), &information_dialogs);
}

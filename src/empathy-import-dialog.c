/*
 * Copyright (C) 2008-2009 Collabora Ltd.
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
 * Authors: Jonny Lamb <jonny.lamb@collabora.co.uk>
 *          Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

#include <config.h>

#include <glib.h>
#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>

#include "empathy-import-dialog.h"
#include "empathy-import-pidgin.h"
#include "empathy-import-widget.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>
#include <libempathy-gtk/empathy-ui-utils.h>

enum {
  PROP_PARENT = 1,
  PROP_SHOW_WARNING
};

typedef struct {
  GtkWindow *parent_window;

  EmpathyImportWidget *iw;

  gboolean show_warning;
} EmpathyImportDialogPriv;

G_DEFINE_TYPE (EmpathyImportDialog, empathy_import_dialog, GTK_TYPE_DIALOG)
#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyImportDialog)

static void
import_dialog_add_import_widget (EmpathyImportDialog *self)
{
  EmpathyImportWidget *iw;
  EmpathyImportDialogPriv *priv = GET_PRIV (self);
  GtkWidget *widget, *area;

  area = gtk_dialog_get_content_area (GTK_DIALOG (self));

  iw = empathy_import_widget_new (EMPATHY_IMPORT_APPLICATION_ALL);
  widget = empathy_import_widget_get_widget (iw);
  gtk_box_pack_start (GTK_BOX (area), widget, FALSE, FALSE, 0);
  gtk_widget_show (widget);

  priv->iw = iw;

  gtk_dialog_add_buttons (GTK_DIALOG (self), GTK_STOCK_CANCEL,
      GTK_RESPONSE_CANCEL, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
}

static void
import_dialog_show_warning_message (EmpathyImportDialog *self)
{
  GtkWidget *hbox, *vbox, *w;

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);

  w = gtk_label_new (_("No accounts to import could be found. Empathy "
          "currently only supports importing accounts from Pidgin."));
  gtk_label_set_line_wrap  (GTK_LABEL (w), TRUE);
  gtk_label_set_selectable (GTK_LABEL (w), TRUE);
  gtk_misc_set_alignment   (GTK_MISC  (w), 0.0, 0.0);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);

  w = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING,
      GTK_ICON_SIZE_DIALOG);
  gtk_misc_set_alignment (GTK_MISC (w), 0.5, 0.0);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);

  w = gtk_dialog_get_content_area (GTK_DIALOG (self));
  gtk_box_pack_start (GTK_BOX (w), hbox, FALSE, FALSE, 0);

  gtk_box_set_spacing (GTK_BOX (w), 14); /* 14 + 2 * 5 = 24 */

  gtk_dialog_add_button (GTK_DIALOG (self), GTK_STOCK_CLOSE,
      GTK_RESPONSE_CLOSE);

  gtk_widget_show_all (w);
}

static void
impl_signal_response (GtkDialog *dialog,
    gint response_id)
{
  EmpathyImportDialogPriv *priv = GET_PRIV (dialog);

  if (response_id == GTK_RESPONSE_OK)
    empathy_import_widget_add_selected_accounts (priv->iw);

  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
do_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyImportDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      g_value_set_object (value, priv->parent_window);
      break;
    case PROP_SHOW_WARNING:
      g_value_set_boolean (value, priv->show_warning);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyImportDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      priv->parent_window = g_value_get_object (value);
      break;
    case PROP_SHOW_WARNING:
      priv->show_warning = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_constructed (GObject *obj)
{
  EmpathyImportDialog *self = EMPATHY_IMPORT_DIALOG (obj);
  EmpathyImportDialogPriv *priv = GET_PRIV (self);
  gboolean have_accounts;

  have_accounts = empathy_import_accounts_to_import ();

  if (!have_accounts)
    {
      if (priv->show_warning)
        {
          import_dialog_show_warning_message (self);
        }
      else
        DEBUG ("No accounts to import; closing dialog silently.");
    }
  else
    {
      import_dialog_add_import_widget (self);
    }

  if (priv->parent_window)
    gtk_window_set_transient_for (GTK_WINDOW (self), priv->parent_window);
}

static void
empathy_import_dialog_init (EmpathyImportDialog *self)
{
  EmpathyImportDialogPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_IMPORT_DIALOG, EmpathyImportDialogPriv);

  self->priv = priv;

  gtk_container_set_border_width (GTK_CONTAINER (self), 5);
  gtk_window_set_title (GTK_WINDOW (self), _("Import Accounts"));
  gtk_window_set_modal (GTK_WINDOW (self), TRUE);
}

static void
empathy_import_dialog_class_init (EmpathyImportDialogClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GtkDialogClass *gtkclass = GTK_DIALOG_CLASS (klass);
  GParamSpec *param_spec;

  oclass->constructed = do_constructed;
  oclass->get_property = do_get_property;
  oclass->set_property = do_set_property;

  gtkclass->response = impl_signal_response;

  param_spec = g_param_spec_object ("parent-window",
      "parent-window", "The parent window",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_PARENT, param_spec);

  param_spec = g_param_spec_boolean ("show-warning",
      "show-warning", "Whether a warning should be shown when there are no "
       "sources for importing accounts.",
      FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_SHOW_WARNING, param_spec);

  g_type_class_add_private (klass, sizeof (EmpathyImportDialogPriv));
}

GtkWidget *
empathy_import_dialog_new (GtkWindow *parent,
    gboolean warning)
{
  return g_object_new (EMPATHY_TYPE_IMPORT_DIALOG, "parent-window",
      parent, "show-warning", warning, NULL);
}

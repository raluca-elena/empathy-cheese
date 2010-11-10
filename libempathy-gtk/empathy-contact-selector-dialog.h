/*
 * Copyright (C) 2007-2009 Collabora Ltd.
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
 * Authors: Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_CONTACT_SELECTOR_DIALOG_H__
#define __EMPATHY_CONTACT_SELECTOR_DIALOG_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include <telepathy-glib/connection.h>
#include <telepathy-glib/account.h>

#include "empathy-account-chooser.h"

G_BEGIN_DECLS

typedef struct _EmpathyContactSelectorDialog EmpathyContactSelectorDialog;
typedef struct _EmpathyContactSelectorDialogClass \
          EmpathyContactSelectorDialogClass;

struct _EmpathyContactSelectorDialogClass {
  GtkDialogClass parent_class;

  void (*account_filter) (EmpathyContactSelectorDialog *self,
      EmpathyAccountChooserFilterResultCallback callback,
      gpointer callback_data,
      TpAccount *account);
  gboolean (*contact_filter) (EmpathyContactSelectorDialog *self,
      const char *id);
};

struct _EmpathyContactSelectorDialog {
  GtkDialog parent;

  /* protected fields */
  GtkWidget *vbox;
  GtkWidget *button_action;
};

GType empathy_contact_selector_dialog_get_type (void);
const gchar *empathy_contact_selector_dialog_get_selected (
    EmpathyContactSelectorDialog *self,
    TpConnection **connection,
    TpAccount **account);
void empathy_contact_selector_dialog_set_show_account_chooser (
    EmpathyContactSelectorDialog *self,
    gboolean show_account_chooser);
gboolean empathy_contact_selector_dialog_get_show_account_chooser (
    EmpathyContactSelectorDialog *self);

void empathy_contact_selector_dialog_set_filter_account (
    EmpathyContactSelectorDialog *self,
    TpAccount *account);

TpAccount * empathy_contact_selector_dialog_get_filter_account (
    EmpathyContactSelectorDialog *self);

/* TYPE MACROS */
#define EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG \
  (empathy_contact_selector_dialog_get_type ())
#define EMPATHY_CONTACT_SELECTOR_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG, \
    EmpathyContactSelectorDialog))
#define EMPATHY_CONTACT_SELECTOR_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG, \
    EmpathyContactSelectorDialogClass))
#define EMPATHY_IS_CONTACT_SELECTOR_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG))
#define EMPATHY_IS_CONTACT_SELECTOR_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG))
#define EMPATHY_CONTACT_SELECTOR_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG, \
    EmpathyContactSelectorDialogClass))

G_END_DECLS

#endif /* __EMPATHY_CONTACT_SELECTOR_DIALOG_H__ */

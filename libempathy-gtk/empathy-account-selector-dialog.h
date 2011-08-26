/*
 * Copyright (C) 2011 Collabora Ltd.
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

#ifndef __EMPATHY_ACCOUNT_SELECTOR_DIALOG_H__
#define __EMPATHY_ACCOUNT_SELECTOR_DIALOG_H__

#include <gtk/gtk.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG \
  (empathy_account_selector_dialog_get_type ())
#define EMPATHY_ACCOUNT_SELECTOR_DIALOG(o) (G_TYPE_CHECK_INSTANCE_CAST ((o),\
      EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG, EmpathyAccountSelectorDialog))
#define EMPATHY_ACCOUNT_SELECTOR_DIALOG_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k),\
      EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG, EmpathyAccountSelectorDialogClass))
#define EMPATHY_IS_ACCOUNT_SELECTOR_DIALOG(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o),\
      EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG))
#define EMPATHY_IS_ACCOUNT_SELECTOR_DIALOG_CLASS(k) (\
    G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG))
#define EMPATHY_ACCOUNT_SELECTOR_DIALOG_GET_CLASS(o) (\
    G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_ACCOUNT_SELECTOR_DIALOG,\
      EmpathyAccountSelectorDialogClass))

typedef struct _EmpathyAccountSelectorDialog EmpathyAccountSelectorDialog;
typedef struct _EmpathyAccountSelectorDialogClass \
          EmpathyAccountSelectorDialogClass;
typedef struct _EmpathyAccountSelectorDialogPrivate \
          EmpathyAccountSelectorDialogPrivate;

struct _EmpathyAccountSelectorDialog {
  GtkDialog parent;

  EmpathyAccountSelectorDialogPrivate *priv;
};

struct _EmpathyAccountSelectorDialogClass {
  GtkDialogClass parent_class;
};

GType empathy_account_selector_dialog_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_account_selector_dialog_new (GList *accounts);

TpAccount * empathy_account_selector_dialog_dup_selected (
    EmpathyAccountSelectorDialog *self);

G_END_DECLS

#endif /* __EMPATHY_ACCOUNT_SELECTOR_DIALOG_H__ */

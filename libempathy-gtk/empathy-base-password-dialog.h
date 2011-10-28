/*
 * empathy-base-password-dialog.h - Header for EmpathyBasePasswordDialog
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

#ifndef __EMPATHY_BASE_PASSWORD_DIALOG_H__
#define __EMPATHY_BASE_PASSWORD_DIALOG_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-server-sasl-handler.h>

#include <extensions/extensions.h>

G_BEGIN_DECLS

typedef struct _EmpathyBasePasswordDialog EmpathyBasePasswordDialog;
typedef struct _EmpathyBasePasswordDialogClass EmpathyBasePasswordDialogClass;
typedef struct _EmpathyBasePasswordDialogPriv EmpathyBasePasswordDialogPriv;

struct _EmpathyBasePasswordDialogClass {
  GtkMessageDialogClass parent_class;
};

struct _EmpathyBasePasswordDialog {
  GtkMessageDialog parent;
  EmpathyBasePasswordDialogPriv *priv;

  /* protected */
  TpAccount *account;
  GtkWidget *entry;
  GtkWidget *ticky;
  GtkWidget *ok_button;
};

GType empathy_base_password_dialog_get_type (void);

#define EMPATHY_TYPE_BASE_PASSWORD_DIALOG \
  (empathy_base_password_dialog_get_type ())
#define EMPATHY_BASE_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_BASE_PASSWORD_DIALOG, \
    EmpathyBasePasswordDialog))
#define EMPATHY_BASE_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_BASE_PASSWORD_DIALOG, \
  EmpathyBasePasswordDialogClass))
#define EMPATHY_IS_BASE_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_BASE_PASSWORD_DIALOG))
#define EMPATHY_IS_BASE_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_BASE_PASSWORD_DIALOG))
#define EMPATHY_BASE_PASSWORD_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_BASE_PASSWORD_DIALOG, \
  EmpathyBasePasswordDialogClass))

G_END_DECLS

#endif /* #ifndef __EMPATHY_BASE_PASSWORD_DIALOG_H__*/

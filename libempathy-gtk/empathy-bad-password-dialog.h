/*
 * empathy-bad-password-dialog.h - Header for EmpathyBadPasswordDialog
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

#ifndef __EMPATHY_BAD_PASSWORD_DIALOG_H__
#define __EMPATHY_BAD_PASSWORD_DIALOG_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-base-password-dialog.h>

#include <extensions/extensions.h>

G_BEGIN_DECLS

typedef struct _EmpathyBadPasswordDialog EmpathyBadPasswordDialog;
typedef struct _EmpathyBadPasswordDialogClass EmpathyBadPasswordDialogClass;
typedef struct _EmpathyBadPasswordDialogPriv EmpathyBadPasswordDialogPriv;

struct _EmpathyBadPasswordDialogClass {
    EmpathyBasePasswordDialogClass parent_class;
};

struct _EmpathyBadPasswordDialog {
    EmpathyBasePasswordDialog parent;
    EmpathyBadPasswordDialogPriv *priv;
};

GType empathy_bad_password_dialog_get_type (void);

#define EMPATHY_TYPE_BAD_PASSWORD_DIALOG \
  (empathy_bad_password_dialog_get_type ())
#define EMPATHY_BAD_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_BAD_PASSWORD_DIALOG, \
    EmpathyBadPasswordDialog))
#define EMPATHY_BAD_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_BAD_PASSWORD_DIALOG, \
  EmpathyBadPasswordDialogClass))
#define EMPATHY_IS_BAD_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_BAD_PASSWORD_DIALOG))
#define EMPATHY_IS_BAD_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_BAD_PASSWORD_DIALOG))
#define EMPATHY_BAD_PASSWORD_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_BAD_PASSWORD_DIALOG, \
  EmpathyBadPasswordDialogClass))

GtkWidget * empathy_bad_password_dialog_new (TpAccount *account,
    const gchar *password);

G_END_DECLS

#endif /* #ifndef __EMPATHY_BAD_PASSWORD_DIALOG_H__*/

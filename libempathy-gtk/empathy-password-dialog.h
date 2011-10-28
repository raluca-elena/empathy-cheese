/*
 * empathy-password-dialog.h - Header for EmpathyPasswordDialog
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

#ifndef __EMPATHY_PASSWORD_DIALOG_H__
#define __EMPATHY_PASSWORD_DIALOG_H__

#include <glib-object.h>
#include <gtk/gtk.h>

#include <libempathy/empathy-server-sasl-handler.h>

#include <libempathy-gtk/empathy-base-password-dialog.h>

#include <extensions/extensions.h>

G_BEGIN_DECLS

typedef struct _EmpathyPasswordDialog EmpathyPasswordDialog;
typedef struct _EmpathyPasswordDialogClass EmpathyPasswordDialogClass;
typedef struct _EmpathyPasswordDialogPriv EmpathyPasswordDialogPriv;

struct _EmpathyPasswordDialogClass {
    EmpathyBasePasswordDialogClass parent_class;
};

struct _EmpathyPasswordDialog {
    EmpathyBasePasswordDialog parent;
    EmpathyPasswordDialogPriv *priv;
};

GType empathy_password_dialog_get_type (void);

#define EMPATHY_TYPE_PASSWORD_DIALOG \
  (empathy_password_dialog_get_type ())
#define EMPATHY_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_PASSWORD_DIALOG, \
    EmpathyPasswordDialog))
#define EMPATHY_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_PASSWORD_DIALOG, \
  EmpathyPasswordDialogClass))
#define EMPATHY_IS_PASSWORD_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_PASSWORD_DIALOG))
#define EMPATHY_IS_PASSWORD_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_PASSWORD_DIALOG))
#define EMPATHY_PASSWORD_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_PASSWORD_DIALOG, \
  EmpathyPasswordDialogClass))

GtkWidget * empathy_password_dialog_new (EmpathyServerSASLHandler *handler);

G_END_DECLS

#endif /* #ifndef __EMPATHY_PASSWORD_DIALOG_H__*/

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

#ifndef __EMPATHY_NEW_CALL_DIALOG_H__
#define __EMPATHY_NEW_CALL_DIALOG_H__

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _EmpathyNewCallDialog EmpathyNewCallDialog;
typedef struct _EmpathyNewCallDialogClass EmpathyNewCallDialogClass;
typedef struct _EmpathyNewCallDialogPriv EmpathyNewCallDialogPriv;

struct _EmpathyNewCallDialogClass {
    GtkDialogClass parent_class;
};

struct _EmpathyNewCallDialog {
    GtkDialog parent;

    EmpathyNewCallDialogPriv *priv;
};

GType empathy_new_call_dialog_get_type (void);

/* TYPE MACROS */
#define EMPATHY_TYPE_NEW_CALL_DIALOG \
  (empathy_new_call_dialog_get_type ())
#define EMPATHY_NEW_CALL_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), EMPATHY_TYPE_NEW_CALL_DIALOG, \
    EmpathyNewCallDialog))
#define EMPATHY_NEW_CALL_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), EMPATHY_TYPE_NEW_CALL_DIALOG, \
    EmpathyNewCallDialogClass))
#define EMPATHY_IS_NEW_CALL_DIALOG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), EMPATHY_TYPE_NEW_CALL_DIALOG))
#define EMPATHY_IS_NEW_CALL_DIALOG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), EMPATHY_TYPE_NEW_CALL_DIALOG))
#define EMPATHY_NEW_CALL_DIALOG_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_NEW_CALL_DIALOG, \
    EmpathyNewCallDialogClass))

GtkWidget * empathy_new_call_dialog_show (GtkWindow *parent);

G_END_DECLS

#endif /* __EMPATHY_NEW_CALL_DIALOG_H__ */

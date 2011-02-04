/*
 * empathy-contact-blocking-dialog.h
 *
 * EmpathyContactBlockingDialog
 *
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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_CONTACT_BLOCKING_DIALOG_H__
#define __EMPATHY_CONTACT_BLOCKING_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG	(empathy_contact_blocking_dialog_get_type ())
#define EMPATHY_CONTACT_BLOCKING_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG, EmpathyContactBlockingDialog))
#define EMPATHY_CONTACT_BLOCKING_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG, EmpathyContactBlockingDialogClass))
#define EMPATHY_IS_CONTACT_BLOCKING_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG))
#define EMPATHY_IS_CONTACT_BLOCKING_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG))
#define EMPATHY_CONTACT_BLOCKING_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONTACT_BLOCKING_DIALOG, EmpathyContactBlockingDialogClass))

typedef struct _EmpathyContactBlockingDialog EmpathyContactBlockingDialog;
typedef struct _EmpathyContactBlockingDialogClass EmpathyContactBlockingDialogClass;
typedef struct _EmpathyContactBlockingDialogPrivate EmpathyContactBlockingDialogPrivate;

struct _EmpathyContactBlockingDialog
{
  GtkDialog parent;
  EmpathyContactBlockingDialogPrivate *priv;
};

struct _EmpathyContactBlockingDialogClass
{
  GtkDialogClass parent_class;
};

GType empathy_contact_blocking_dialog_get_type (void);

GtkWidget *empathy_contact_blocking_dialog_new (GtkWindow *parent);

G_END_DECLS

#endif

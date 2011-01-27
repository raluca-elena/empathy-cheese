/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * empathy-contact-search-dialog.h
 *
 * Copyright (C) 2010-2011 Collabora Ltd.
 *
 * The code contained in this file is free software; you can redistribute
 * it and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either version
 * 2.1 of the License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this code; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *     Danielle Madeley <danielle.madeley@collabora.co.uk>
 *     Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#ifndef __EMPATHY_CONTACT_SEARCH_DIALOG_H__
#define __EMPATHY_CONTACT_SEARCH_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_CONTACT_SEARCH_DIALOG	(empathy_contact_search_dialog_get_type ())
#define EMPATHY_CONTACT_SEARCH_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_CONTACT_SEARCH_DIALOG, EmpathyContactSearchDialog))
#define EMPATHY_CONTACT_SEARCH_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_CONTACT_SEARCH_DIALOG, EmpathyContactSearchDialogClass))
#define EMPATHY_IS_CONTACT_SEARCH_DIALOG(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_CONTACT_SEARCH_DIALOG))
#define EMPATHY_IS_CONTACT_SEARCH_DIALOG_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_CONTACT_SEARCH_DIALOG))
#define EMPATHY_CONTACT_SEARCH_DIALOG_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONTACT_SEARCH_DIALOG, EmpathyContactSearchDialogClass))

typedef struct _EmpathyContactSearchDialog EmpathyContactSearchDialog;
typedef struct _EmpathyContactSearchDialogClass EmpathyContactSearchDialogClass;

struct _EmpathyContactSearchDialog
{
  GtkDialog parent;
};

struct _EmpathyContactSearchDialogClass
{
  GtkDialogClass parent_class;
};

GType empathy_contact_search_dialog_get_type (void);
GtkWidget *empathy_contact_search_dialog_new (GtkWindow *parent);

G_END_DECLS

#endif

/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Geert-Jan Van den Bogaerde <geertjan@gnome.org>
 *          Xavier Claessens <xclaesse@gmail.com>
 *          Guillaume Desmottes <guillaume.desmottes@collabora.co.uk>
 */

#ifndef __EMPATHY_INPUT_TEXT_VIEW_H__
#define __EMPATHY_INPUT_TEXT_VIEW_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_INPUT_TEXT_VIEW         (empathy_input_text_view_get_type ())
#define EMPATHY_INPUT_TEXT_VIEW(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_INPUT_TEXT_VIEW, EmpathyInputTextView))
#define EMPATHY_INPUT_TEXT_VIEW_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), EMPATHY_TYPE_INPUT_TEXT_VIEW, EmpathyInputTextViewClass))
#define EMPATHY_IS_INPUT_TEXT_VIEW(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_INPUT_TEXT_VIEW))
#define EMPATHY_IS_INPUT_TEXT_VIEW_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_INPUT_TEXT_VIEW))
#define EMPATHY_INPUT_TEXT_VIEW_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_INPUT_TEXT_VIEW, EmpathyInputTextViewClass))

typedef struct _EmpathyInputTextView EmpathyInputTextView;
typedef struct _EmpathyInputTextViewClass EmpathyInputTextViewClass;
typedef struct _EmpathyInputTextViewPrivate EmpathyInputTextViewPrivate;

struct _EmpathyInputTextView
{
  GtkTextView parent;
  EmpathyInputTextViewPrivate *priv;
};

struct _EmpathyInputTextViewClass
{
  GtkTextViewClass parent_class;
};

GType empathy_input_text_view_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_input_text_view_new (void);

G_END_DECLS
#endif /* __EMPATHY_INPUT_TEXT_VIEW_H__ */

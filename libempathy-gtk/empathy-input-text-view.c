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

#include "empathy-input-text-view.h"

G_DEFINE_TYPE (EmpathyInputTextView, empathy_input_text_view,
    GTK_TYPE_TEXT_VIEW);

struct _EmpathyInputTextViewPrivate
{
    gpointer unused;
};

static void
empathy_input_text_view_class_init (EmpathyInputTextViewClass *cls)
{
  g_type_class_add_private (cls, sizeof (EmpathyInputTextViewPrivate));
}

static void
empathy_input_text_view_init (EmpathyInputTextView *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INPUT_TEXT_VIEW, EmpathyInputTextViewPrivate);
}

GtkWidget *
empathy_input_text_view_new (void)
{
    return g_object_new (EMPATHY_TYPE_INPUT_TEXT_VIEW,
        "pixels-above-lines", 2,
        "pixels-below-lines", 2,
        "pixels-inside-wrap", 1,
        "right-margin", 2,
        "left-margin", 2,
        "wrap-mode", GTK_WRAP_WORD_CHAR,
        NULL);
}

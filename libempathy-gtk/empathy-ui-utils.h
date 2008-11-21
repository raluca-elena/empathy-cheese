/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 * 
 * Authors: Mikael Hallendal <micke@imendio.com>
 *          Richard Hult <richard@imendio.com>
 *          Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 * 
 *          Part of this file is copied from GtkSourceView (gtksourceiter.c):
 *          Paolo Maggi
 *          Jeroen Zwartepoorte
 */

#ifndef __EMPATHY_UI_UTILS_H__
#define __EMPATHY_UI_UTILS_H__

#include <gtk/gtk.h>
#include <glade/glade.h>

#include <libmissioncontrol/mc-account.h>
#include <libmissioncontrol/mc-profile.h>

#include <libempathy/empathy-contact.h>
#include <libempathy/empathy-file.h>

#include "empathy-chat-view.h"
#include "empathy-ft-manager.h"

G_BEGIN_DECLS

#define G_STR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

/* Glade */
void            empathy_glade_get_file_simple           (const gchar      *filename,
							 const gchar      *root,
							 const gchar      *domain,
							 const gchar      *first_required_widget,
							 ...);
GladeXML *      empathy_glade_get_file                  (const gchar      *filename,
							 const gchar      *root,
							 const gchar      *domain,
							 const gchar      *first_required_widget,
							 ...);
void            empathy_glade_connect                   (GladeXML         *gui,
							 gpointer          user_data,
							 gchar            *first_widget,
							 ...);
void            empathy_glade_setup_size_group          (GladeXML         *gui,
							 GtkSizeGroupMode  mode,
							 gchar            *first_widget,
							 ...);
/* Pixbufs */
const gchar * empathy_icon_name_from_account            (McAccount        *account);
const gchar * empathy_icon_name_for_presence            (McPresence        presence);
const gchar * empathy_icon_name_for_contact             (EmpathyContact   *contact);
GdkPixbuf *   empathy_pixbuf_from_data                  (gchar            *data,
							 gsize             data_size);
GdkPixbuf *   empathy_pixbuf_from_data_and_mime         (gchar            *data,
							 gsize             data_size,
							 gchar           **mime_type);
GdkPixbuf *   empathy_pixbuf_from_avatar_scaled         (EmpathyAvatar    *avatar,
							 gint              width,
							 gint              height);
GdkPixbuf *   empathy_pixbuf_avatar_from_contact_scaled (EmpathyContact   *contact,
							 gint              width,
							 gint              height);
GdkPixbuf * empathy_pixbuf_scale_down_if_necessary      (GdkPixbuf        *pixbuf,
							 gint              max_size);
GdkPixbuf * empathy_pixbuf_from_icon_name               (const gchar      *icon_name,
							 GtkIconSize       icon_size);
/* Text view */
gboolean    empathy_text_iter_forward_search            (const GtkTextIter*iter,
							 const gchar      *str,
							 GtkTextIter      *match_start,
							 GtkTextIter      *match_end,
							 const GtkTextIter*limit);
gboolean    empathy_text_iter_backward_search           (const GtkTextIter*iter,
							 const gchar      *str,
							 GtkTextIter      *match_start,
							 GtkTextIter      *match_end,
							 const GtkTextIter*limit);
/* Windows */
gboolean    empathy_window_get_is_visible               (GtkWindow        *window);
void        empathy_window_present                      (GtkWindow        *window,
							 gboolean          steal_focus);
void        empathy_window_iconify                      (GtkWindow        *window,
							 GtkStatusIcon    *status_icon);
GtkWindow * empathy_get_toplevel_window                 (GtkWidget        *widget);
void        empathy_url_show                            (const char       *url);
void        empathy_toggle_button_set_state_quietly     (GtkWidget        *widget,
							 GCallback         callback,
							 gpointer          user_data,
							 gboolean          active);
GtkWidget * empathy_link_button_new                     (const gchar      *url,
							 const gchar      *title);
GtkTextTag *empathy_text_buffer_tag_set                 (GtkTextBuffer *buffer,
							 const gchar   *tag_name,
							 const gchar   *first_property_name,
							 ...);

typedef gboolean (*EmpathyFileChooserCallback)          (EmpathyTpFile *tp_file,
							 gpointer       user_data);

void        empathy_send_file_with_file_chooser         (EmpathyContact             *contact,
							 EmpathyFileChooserCallback  callback,
							 gpointer                    user_data);
void    empathy_send_file_with_file_chooser_and_manager (EmpathyContact   *contact);

G_END_DECLS

#endif /*  __EMPATHY_UI_UTILS_H__ */

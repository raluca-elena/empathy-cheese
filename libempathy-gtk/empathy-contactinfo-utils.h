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
 * Authors: Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#ifndef __EMPATHY_CONTACTINFO_UTILS_H__
#define __EMPATHY_CONTACTINFO_UTILS_H__

#include <glib.h>
#include <telepathy-glib/connection.h>

G_BEGIN_DECLS

gboolean empathy_contact_info_lookup_field (const gchar *field_name,
    const gchar **title, gboolean *linkify);
char *empathy_contact_info_field_label (const char *field_name,
    GStrv parameters);

gint empathy_contact_info_field_cmp (TpContactInfoField *field1,
    TpContactInfoField *field2);
gint empathy_contact_info_field_spec_cmp (TpContactInfoFieldSpec *spec1,
    TpContactInfoFieldSpec *spec2);

G_END_DECLS

#endif /*  __EMPATHY_UTILS_H__ */

/*
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 *          Philip Withnall <philip.withnall@collabora.co.uk>
 *          Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <config.h>

#include <string.h>

#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>

#include "empathy-contactinfo-utils.h"

typedef struct
{
  const gchar *field_name;
  const gchar *title;
  gboolean linkify;
} InfoFieldData;

static InfoFieldData info_field_data[] =
{
  { "fn",    N_("Full name"),      FALSE },
  { "tel",   N_("Phone number"),   FALSE },
  { "email", N_("E-mail address"), TRUE },
  { "url",   N_("Website"),        TRUE },
  { "bday",  N_("Birthday"),       FALSE },
  { NULL, NULL }
};

typedef struct
{
  const gchar *type;
  const gchar *title;
} InfoParameterData;

static InfoParameterData info_parameter_data[] =
{
  { "work", N_("work") },
  { "home", N_("home") },
  { "cell", N_("mobile") },
  { "voice", N_("voice") },
  { "pref", N_("preferred") },
  { "postal", N_("postal") },
  { "parcel", N_("parcel") },
  { NULL, NULL }
};

gboolean
empathy_contact_info_lookup_field (const gchar *field_name,
    const gchar **title,
    gboolean *linkify)
{
  guint i;

  for (i = 0; info_field_data[i].field_name != NULL; i++)
    {
      if (tp_strdiff (info_field_data[i].field_name, field_name) == FALSE)
        {
          if (title != NULL)
            *title = gettext (info_field_data[i].title);

          if (linkify != NULL)
            *linkify = info_field_data[i].linkify;

          return TRUE;
        }
    }

  return FALSE;
}

static char *
build_parameters_string (GStrv parameters)
{
  GPtrArray *output = g_ptr_array_new ();
  char *join;
  GStrv iter;

  for (iter = parameters; iter != NULL && *iter != NULL; iter++)
    {
      static const char *prefix = "type=";
      const char *param = *iter;
      InfoParameterData *iter2;

      if (!g_str_has_prefix (param, prefix))
        continue;

      param += strlen (prefix);

      for (iter2 = info_parameter_data; iter2->type != NULL; iter2++)
        {
          if (!tp_strdiff (iter2->type, param))
            {
              g_ptr_array_add (output, gettext (iter2->title));
              break;
            }
        }
    }

  if (output->len == 0)
    return NULL;

  g_ptr_array_add (output, NULL); /* NULL-terminate */

  join = g_strjoinv (", ", (char **) output->pdata);
  g_ptr_array_free (output, TRUE);

  return join;
}

char *
empathy_contact_info_field_label (const char *field_name,
    GStrv parameters)
{
  char *ret;
  const char *title;
  char *join = build_parameters_string (parameters);

  if (!empathy_contact_info_lookup_field (field_name, &title, NULL))
    return NULL;

  if (join != NULL)
    ret = g_strdup_printf ("%s (%s):", title, join);
  else
    ret = g_strdup_printf ("%s:", title);

  g_free (join);

  return ret;
}

static gint
contact_info_field_name_cmp (const gchar *name1,
    const gchar *name2)
{
  guint i;

  if (tp_strdiff (name1, name2) == FALSE)
    return 0;

  /* We use the order of info_field_data */
  for (i = 0; info_field_data[i].field_name != NULL; i++)
    {
      if (tp_strdiff (info_field_data[i].field_name, name1) == FALSE)
        return -1;
      if (tp_strdiff (info_field_data[i].field_name, name2) == FALSE)
        return +1;
    }

  return g_strcmp0 (name1, name2);
}

gint
empathy_contact_info_field_cmp (TpContactInfoField *field1,
    TpContactInfoField *field2)
{
  return contact_info_field_name_cmp (field1->field_name, field2->field_name);
}

gint
empathy_contact_info_field_spec_cmp (TpContactInfoFieldSpec *spec1,
    TpContactInfoFieldSpec *spec2)
{
    return contact_info_field_name_cmp (spec1->name, spec2->name);
}

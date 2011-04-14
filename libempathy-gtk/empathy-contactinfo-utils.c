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
#include <stdlib.h>

#include <glib/gi18n-lib.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-time.h>

#include "empathy-contactinfo-utils.h"
#include "empathy-string-parser.h"

static gchar *
linkify_first_value (GStrv values)
{
  return empathy_add_link_markup (values[0]);
}

static gchar *
format_idle_time (GStrv values)
{
  const gchar *value = values[0];
  int duration = strtol (value, NULL, 10);

  if (duration <= 0)
    return NULL;

  return empathy_duration_to_string (duration);
}

static gchar *
format_server (GStrv values)
{
  g_assert (values[0] != NULL);

  if (values[1] == NULL)
    return g_markup_escape_text (values[0], -1);
  else
    return g_markup_printf_escaped ("%s (%s)", values[0], values[1]);
}

static gchar *
presence_hack (GStrv values)
{
  if (tp_str_empty (values[0]))
    return NULL;

  return g_markup_escape_text (values[0], -1);
}

typedef struct
{
  const gchar *field_name;
  const gchar *title;
  EmpathyContactInfoFormatFunc format;
} InfoFieldData;

/* keep this syncronised with info_field_data below */
static const char *info_field_names[] =
{
  "fn",
  "tel",
  "email",
  "url",
  "bday",

  "x-idle-time",
  "x-irc-server",
  "x-host",

  "x-presence-status-message",

  NULL
};

static InfoFieldData info_field_data[G_N_ELEMENTS (info_field_names)] =
{
  { "fn",    N_("Full name"),      NULL },
  { "tel",   N_("Phone number"),   NULL },
  { "email", N_("E-mail address"), linkify_first_value },
  { "url",   N_("Website"),        linkify_first_value },
  { "bday",  N_("Birthday"),       NULL },

  /* Note to translators: this is the caption for a string of the form "5
   * minutes ago", and refers to the time since the contact last interacted
   * with their IM client. */
  { "x-idle-time",  N_("Last seen:"),      format_idle_time },
  { "x-irc-server", N_("Server:"),         format_server },
  { "x-host",       N_("Connected from:"), format_server },

  /* FIXME: once Idle implements SimplePresence using this information, we can
   * and should bin this. */
  { "x-presence-status-message", N_("Away message:"), presence_hack },

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

const char **
empathy_contact_info_get_field_names (guint *nnames)
{
  if (nnames != NULL)
    *nnames = G_N_ELEMENTS (info_field_names) - 1;

  return info_field_names;
}

gboolean
empathy_contact_info_lookup_field (const gchar *field_name,
    const gchar **title,
    EmpathyContactInfoFormatFunc *format)
{
  guint i;

  for (i = 0; info_field_data[i].field_name != NULL; i++)
    {
      if (tp_strdiff (info_field_data[i].field_name, field_name) == FALSE)
        {
          if (title != NULL)
            *title = gettext (info_field_data[i].title);

          if (format != NULL)
            *format = info_field_data[i].format;

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

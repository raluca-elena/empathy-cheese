/*
 * Copyright (C) 2008-2009 Collabora Ltd.
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
 */

#include "empathy-webkit-utils.h"
#include "empathy-smiley-manager.h"

static void
empathy_webkit_match_newline (const gchar *text,
    gssize len,
    EmpathyStringReplace replace_func,
    EmpathyStringParser *sub_parsers,
    gpointer user_data)
{
  GString *string = user_data;
  gint i;
  gint prev = 0;

  if (len < 0)
    len = G_MAXSSIZE;

  /* Replace \n by <br/> */
  for (i = 0; i < len && text[i] != '\0'; i++)
    {
      if (text[i] == '\n')
        {
          empathy_string_parser_substr (text + prev, i - prev,
              sub_parsers, user_data);
          g_string_append (string, "<br/>");
          prev = i + 1;
        }
    }

  empathy_string_parser_substr (text + prev, i - prev,
              sub_parsers, user_data);
}

static void
empathy_webkit_replace_smiley (const gchar *text,
    gssize len,
    gpointer match_data,
    gpointer user_data)
{
  EmpathySmileyHit *hit = match_data;
  GString *string = user_data;

  /* Replace smiley by a <img/> tag */
  g_string_append_printf (string,
      "<img src=\"%s\" alt=\"%.*s\" title=\"%.*s\"/>",
      hit->path, (int)len, text, (int)len, text);
}

static EmpathyStringParser string_parsers[] = {
  { empathy_string_match_link, empathy_string_replace_link },
  { empathy_webkit_match_newline, NULL },
  { empathy_string_match_all, empathy_string_replace_escaped },
  { NULL, NULL}
};

static EmpathyStringParser string_parsers_with_smiley[] = {
  { empathy_string_match_link, empathy_string_replace_link },
  { empathy_string_match_smiley, empathy_webkit_replace_smiley },
  { empathy_webkit_match_newline, NULL },
  { empathy_string_match_all, empathy_string_replace_escaped },
  { NULL, NULL }
};

EmpathyStringParser *
empathy_webkit_get_string_parser (gboolean smileys)
{
  if (smileys)
    return string_parsers_with_smiley;
  else
    return string_parsers;
}

/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003-2007 Imendio AB
 * Copyright (C) 2007-2010 Collabora Ltd.
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
 * Authors: Richard Hult <richard@imendio.com>
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include "empathy-time.h"

/* Note: EmpathyTime is always in UTC. */

gint64
empathy_time_get_current (void)
{
	GDateTime *now;
	gint64 result;

	now = g_date_time_new_now_utc ();
	result = g_date_time_to_unix (now);
	g_date_time_unref (now);

	return result;
}

/* Converts the UTC timestamp to a string, also in UTC. Returns NULL on failure. */
gchar *
empathy_time_to_string_utc (gint64       t,
			    const gchar *format)
{
	GDateTime *d;
	char *result;

	g_return_val_if_fail (format != NULL, NULL);

	d = g_date_time_new_from_unix_utc (t);
	result = g_date_time_format (d, format);
	g_date_time_unref (d);

	return result;
}

/* Converts the UTC timestamp to a string, in local time. Returns NULL on failure. */
gchar *
empathy_time_to_string_local (gint64 t,
			      const gchar *format)
{
	GDateTime *d, *local;
	gchar *result;

	g_return_val_if_fail (format != NULL, NULL);

	d = g_date_time_new_from_unix_utc (t);
	local = g_date_time_to_local (d);
	g_date_time_unref (d);

	result = g_date_time_format (local, format);
	g_date_time_unref (local);

	return result;
}

gchar  *
empathy_time_to_string_relative (gint64 t)
{
	GDateTime *now, *then;
	gint   seconds;
	GTimeSpan delta;
	gchar *result;

	now = g_date_time_new_now_utc ();
	then = g_date_time_new_from_unix_utc (t);

	delta = g_date_time_difference (now, then);
	seconds = delta / G_TIME_SPAN_SECOND;

	if (seconds > 0) {
		if (seconds < 60) {
			result = g_strdup_printf (ngettext ("%d second ago",
				"%d seconds ago", seconds), seconds);
		}
		else if (seconds < (60 * 60)) {
			seconds /= 60;
			result = g_strdup_printf (ngettext ("%d minute ago",
				"%d minutes ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24)) {
			seconds /= 60 * 60;
			result = g_strdup_printf (ngettext ("%d hour ago",
				"%d hours ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24 * 7)) {
			seconds /= 60 * 60 * 24;
			result = g_strdup_printf (ngettext ("%d day ago",
				"%d days ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24 * 30)) {
			seconds /= 60 * 60 * 24 * 7;
			result = g_strdup_printf (ngettext ("%d week ago",
				"%d weeks ago", seconds), seconds);
		}
		else {
			seconds /= 60 * 60 * 24 * 30;
			result = g_strdup_printf (ngettext ("%d month ago",
				"%d months ago", seconds), seconds);
		}
	}
	else {
		result = g_strdup (_("in the future"));
	}

	g_date_time_unref (now);
	g_date_time_unref (then);

	return result;
}

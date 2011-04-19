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

time_t
empathy_time_get_current (void)
{
	return time (NULL);
}

/* Converts the UTC timestamp to a string, also in UTC. Returns NULL on failure. */
gchar *
empathy_time_to_string_utc (time_t       t,
			    const gchar *format)
{
	gchar      stamp[128];
	struct tm *tm;

	g_return_val_if_fail (format != NULL, NULL);

	tm = gmtime (&t);
	if (strftime (stamp, sizeof (stamp), format, tm) == 0) {
		return NULL;
	}

	return g_strdup (stamp);
}

/* Converts the UTC timestamp to a string, in local time. Returns NULL on failure. */
gchar *
empathy_time_to_string_local (time_t       t,
			      const gchar *format)
{
	gchar      stamp[128];
	struct tm *tm;

	g_return_val_if_fail (format != NULL, NULL);

	tm = localtime (&t);
	if (strftime (stamp, sizeof (stamp), format, tm) == 0) {
		return NULL;
	}

	return g_strdup (stamp);
}

gchar  *
empathy_time_to_string_relative (time_t then)
{
	time_t now;
	gint   seconds;

	now = time (NULL);
	seconds = now - then;

	if (seconds > 0) {
		if (seconds < 60) {
			return g_strdup_printf (ngettext ("%d second ago",
				"%d seconds ago", seconds), seconds);
		}
		else if (seconds < (60 * 60)) {
			seconds /= 60;
			return g_strdup_printf (ngettext ("%d minute ago",
				"%d minutes ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24)) {
			seconds /= 60 * 60;
			return g_strdup_printf (ngettext ("%d hour ago",
				"%d hours ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24 * 7)) {
			seconds /= 60 * 60 * 24;
			return g_strdup_printf (ngettext ("%d day ago",
				"%d days ago", seconds), seconds);
		}
		else if (seconds < (60 * 60 * 24 * 30)) {
			seconds /= 60 * 60 * 24 * 7;
			return g_strdup_printf (ngettext ("%d week ago",
				"%d weeks ago", seconds), seconds);
		}
		else {
			seconds /= 60 * 60 * 24 * 30;
			return g_strdup_printf (ngettext ("%d month ago",
				"%d months ago", seconds), seconds);
		}
	}
	else {
		return g_strdup (_("in the future"));
	}
}

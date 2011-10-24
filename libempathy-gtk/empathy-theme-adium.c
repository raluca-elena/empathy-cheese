/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include "config.h"

#include <string.h>
#include <glib/gi18n-lib.h>

#include <webkit/webkit.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/util.h>

#include <pango/pango.h>
#include <gdk/gdk.h>

#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-time.h>
#include <libempathy/empathy-utils.h>

#include "empathy-theme-adium.h"
#include "empathy-smiley-manager.h"
#include "empathy-ui-utils.h"
#include "empathy-plist.h"
#include "empathy-images.h"
#include "empathy-webkit-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CHAT
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyThemeAdium)

#define BORING_DPI_DEFAULT 96

/* "Join" consecutive messages with timestamps within five minutes */
#define MESSAGE_JOIN_PERIOD 5*60

typedef struct {
	EmpathyAdiumData     *data;
	EmpathySmileyManager *smiley_manager;
	EmpathyContact       *last_contact;
	gint64                last_timestamp;
	gboolean              last_is_backlog;
	guint                 pages_loading;
	/* Queue of QueuedItem*s containing an EmpathyMessage or string */
	GQueue                message_queue;
	/* Queue of guint32 of pending message id to remove unread
	 * marker for when we lose focus. */
	GQueue                acked_messages;
	GtkWidget            *inspector_window;

	GSettings            *gsettings_chat;
	GSettings            *gsettings_desktop;

	gboolean              has_focus;
	gboolean              has_unread_message;
	gboolean              allow_scrolling;
	gchar                *variant;
	gboolean              in_construction;
} EmpathyThemeAdiumPriv;

struct _EmpathyAdiumData {
	gint  ref_count;
	gchar *path;
	gchar *basedir;
	gchar *default_avatar_filename;
	gchar *default_incoming_avatar_filename;
	gchar *default_outgoing_avatar_filename;
	GHashTable *info;
	guint version;
	gboolean custom_template;
	/* gchar* -> gchar* both owned */
	GHashTable *date_format_cache;

	/* HTML bits */
	const gchar *template_html;
	const gchar *content_html;
	const gchar *in_content_html;
	const gchar *in_context_html;
	const gchar *in_nextcontent_html;
	const gchar *in_nextcontext_html;
	const gchar *out_content_html;
	const gchar *out_context_html;
	const gchar *out_nextcontent_html;
	const gchar *out_nextcontext_html;
	const gchar *status_html;

	/* Above html strings are pointers to strings stored in this array.
	 * We do this because of fallbacks, some htmls could be pointing the
	 * same string. */
	GPtrArray *strings_to_free;
};

static void theme_adium_iface_init (EmpathyChatViewIface *iface);
static gchar * adium_info_dup_path_for_variant (GHashTable *info, const gchar *variant);

enum {
	PROP_0,
	PROP_ADIUM_DATA,
	PROP_VARIANT,
};

G_DEFINE_TYPE_WITH_CODE (EmpathyThemeAdium, empathy_theme_adium,
			 WEBKIT_TYPE_WEB_VIEW,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CHAT_VIEW,
						theme_adium_iface_init));

enum {
	QUEUED_EVENT,
	QUEUED_MESSAGE,
	QUEUED_EDIT
};

typedef struct {
	guint type;
	EmpathyMessage *msg;
	char *str;
} QueuedItem;

static QueuedItem *
queue_item (GQueue *queue,
	    guint type,
	    EmpathyMessage *msg,
	    const char *str)
{
	QueuedItem *item = g_slice_new0 (QueuedItem);

	item->type = type;
	if (msg != NULL)
		item->msg = g_object_ref (msg);
	item->str = g_strdup (str);

	g_queue_push_tail (queue, item);

	return item;
}

static void
free_queued_item (QueuedItem *item)
{
	tp_clear_object (&item->msg);
	g_free (item->str);

	g_slice_free (QueuedItem, item);
}

static void
theme_adium_update_enable_webkit_developer_tools (EmpathyThemeAdium *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	WebKitWebView  *web_view = WEBKIT_WEB_VIEW (theme);
	gboolean        enable_webkit_developer_tools;

	enable_webkit_developer_tools = g_settings_get_boolean (
			priv->gsettings_chat,
			EMPATHY_PREFS_CHAT_WEBKIT_DEVELOPER_TOOLS);

	g_object_set (G_OBJECT (webkit_web_view_get_settings (web_view)),
		      "enable-developer-extras",
		      enable_webkit_developer_tools,
		      NULL);
}

static void
theme_adium_notify_enable_webkit_developer_tools_cb (GSettings   *gsettings,
						     const gchar *key,
						     gpointer     user_data)
{
	EmpathyThemeAdium  *theme = user_data;

	theme_adium_update_enable_webkit_developer_tools (theme);
}

static gboolean
theme_adium_navigation_policy_decision_requested_cb (WebKitWebView             *view,
						     WebKitWebFrame            *web_frame,
						     WebKitNetworkRequest      *request,
						     WebKitWebNavigationAction *action,
						     WebKitWebPolicyDecision   *decision,
						     gpointer                   data)
{
	const gchar *uri;

	/* Only call url_show on clicks */
	if (webkit_web_navigation_action_get_reason (action) !=
	    WEBKIT_WEB_NAVIGATION_REASON_LINK_CLICKED) {
		webkit_web_policy_decision_use (decision);
		return TRUE;
	}

	uri = webkit_network_request_get_uri (request);
	empathy_url_show (GTK_WIDGET (view), uri);

	webkit_web_policy_decision_ignore (decision);
	return TRUE;
}

/* Replace each %@ in format with string passed in args */
static gchar *
string_with_format (const gchar *format,
		    const gchar *first_string,
		    ...)
{
	va_list args;
	const gchar *str;
	GString *result;

	va_start (args, first_string);
	result = g_string_sized_new (strlen (format));
	for (str = first_string; str != NULL; str = va_arg (args, const gchar *)) {
		const gchar *next;

		next = strstr (format, "%@");
		if (next == NULL) {
			break;
		}

		g_string_append_len (result, format, next - format);
		g_string_append (result, str);
		format = next + 2;
	}
	g_string_append (result, format);
	va_end (args);

	return g_string_free (result, FALSE);
}

static void
theme_adium_load_template (EmpathyThemeAdium *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	gchar                 *basedir_uri;
	gchar                 *variant_path;
	gchar                 *template;

	priv->pages_loading++;
	basedir_uri = g_strconcat ("file://", priv->data->basedir, NULL);
	variant_path = adium_info_dup_path_for_variant (priv->data->info,
		priv->variant);
	template = string_with_format (priv->data->template_html,
		variant_path, NULL);
	webkit_web_view_load_html_string (WEBKIT_WEB_VIEW (theme),
					  template, basedir_uri);
	g_free (basedir_uri);
	g_free (variant_path);
	g_free (template);
}

static gchar *
theme_adium_parse_body (EmpathyThemeAdium *self,
	const gchar *text,
	const gchar *token)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (self);
	EmpathyStringParser *parsers;
	GString *string;

	/* Check if we have to parse smileys */
	parsers = empathy_webkit_get_string_parser (
		g_settings_get_boolean (priv->gsettings_chat,
			EMPATHY_PREFS_CHAT_SHOW_SMILEYS));

	/* Parse text and construct string with links and smileys replaced
	 * by html tags. Also escape text to make sure html code is
	 * displayed verbatim. */
	string = g_string_sized_new (strlen (text));

	/* wrap this in HTML that allows us to find the message for later
	 * editing */
	if (!tp_str_empty (token))
		g_string_append_printf (string,
			"<span id=\"message-token-%s\">",
			token);

	empathy_string_parser_substr (text, -1, parsers, string);

	if (!tp_str_empty (token))
		g_string_append (string, "</span>");

	/* Wrap body in order to make tabs and multiple spaces displayed
	 * properly. See bug #625745. */
	g_string_prepend (string, "<div style=\"display: inline; "
					       "white-space: pre-wrap\"'>");
	g_string_append (string, "</div>");

	return g_string_free (string, FALSE);
}

static void
escape_and_append_len (GString *string, const gchar *str, gint len)
{
	while (str != NULL && *str != '\0' && len != 0) {
		switch (*str) {
		case '\\':
			/* \ becomes \\ */
			g_string_append (string, "\\\\");
			break;
		case '\"':
			/* " becomes \" */
			g_string_append (string, "\\\"");
			break;
		case '\n':
			/* Remove end of lines */
			break;
		default:
			g_string_append_c (string, *str);
		}

		str++;
		len--;
	}
}

/* If *str starts with match, returns TRUE and move pointer to the end */
static gboolean
theme_adium_match (const gchar **str,
		   const gchar *match)
{
	gint len;

	len = strlen (match);
	if (strncmp (*str, match, len) == 0) {
		*str += len - 1;
		return TRUE;
	}

	return FALSE;
}

/* Like theme_adium_match() but also return the X part if match is like %foo{X}% */
static gboolean
theme_adium_match_with_format (const gchar **str,
			       const gchar *match,
			       gchar **format)
{
	const gchar *cur = *str;
	const gchar *end;

	if (!theme_adium_match (&cur, match)) {
		return FALSE;
	}
	cur++;

	end = strstr (cur, "}%");
	if (!end) {
		return FALSE;
	}

	*format = g_strndup (cur , end - cur);
	*str = end + 1;
	return TRUE;
}

/* List of colors used by %senderColor%. Copied from
 * adium/Frameworks/AIUtilities\ Framework/Source/AIColorAdditions.m
 */
static gchar *colors[] = {
	"aqua", "aquamarine", "blue", "blueviolet", "brown", "burlywood", "cadetblue",
	"chartreuse", "chocolate", "coral", "cornflowerblue", "crimson", "cyan",
	"darkblue", "darkcyan", "darkgoldenrod", "darkgreen", "darkgrey", "darkkhaki",
	"darkmagenta", "darkolivegreen", "darkorange", "darkorchid", "darkred",
	"darksalmon", "darkseagreen", "darkslateblue", "darkslategrey",
	"darkturquoise", "darkviolet", "deeppink", "deepskyblue", "dimgrey",
	"dodgerblue", "firebrick", "forestgreen", "fuchsia", "gold", "goldenrod",
	"green", "greenyellow", "grey", "hotpink", "indianred", "indigo", "lawngreen",
	"lightblue", "lightcoral",
	"lightgreen", "lightgrey", "lightpink", "lightsalmon", "lightseagreen",
	"lightskyblue", "lightslategrey", "lightsteelblue", "lime", "limegreen",
	"magenta", "maroon", "mediumaquamarine", "mediumblue", "mediumorchid",
	"mediumpurple", "mediumseagreen", "mediumslateblue", "mediumspringgreen",
	"mediumturquoise", "mediumvioletred", "midnightblue", "navy", "olive",
	"olivedrab", "orange", "orangered", "orchid", "palegreen", "paleturquoise",
	"palevioletred", "peru", "pink", "plum", "powderblue", "purple", "red",
	"rosybrown", "royalblue", "saddlebrown", "salmon", "sandybrown", "seagreen",
	"sienna", "silver", "skyblue", "slateblue", "slategrey", "springgreen",
	"steelblue", "tan", "teal", "thistle", "tomato", "turquoise", "violet",
	"yellowgreen",
};

static const gchar *
nsdate_to_strftime (EmpathyAdiumData *data, const gchar *nsdate)
{
	/* Convert from NSDateFormatter (http://www.stepcase.com/blog/2008/12/02/format-string-for-the-iphone-nsdateformatter/)
	 * to strftime supported by g_date_time_format.
	 * FIXME: table is incomplete, doc of g_date_time_format has a table of
	 *        supported tags.
	 * FIXME: g_date_time_format in GLib 2.28 does 0 padding by default, but
	 *        in 2.29.x we have to explictely request padding with %0x */
	static const gchar *convert_table[] = {
		"a", "%p", // AM/PM
		"A", NULL, // 0~86399999 (Millisecond of Day)

		"cccc", "%A", // Sunday/Monday/Tuesday/Wednesday/Thursday/Friday/Saturday
		"ccc", "%a", // Sun/Mon/Tue/Wed/Thu/Fri/Sat
		"cc", "%u", // 1~7 (Day of Week)
		"c", "%u", // 1~7 (Day of Week)

		"dd", "%d", // 1~31 (0 padded Day of Month)
		"d", "%d", // 1~31 (0 padded Day of Month)
		"D", "%j", // 1~366 (0 padded Day of Year)

		"e", "%u", // 1~7 (0 padded Day of Week)
		"EEEE", "%A", // Sunday/Monday/Tuesday/Wednesday/Thursday/Friday/Saturday
		"EEE", "%a", // Sun/Mon/Tue/Wed/Thu/Fri/Sat
		"EE", "%a", // Sun/Mon/Tue/Wed/Thu/Fri/Sat
		"E", "%a", // Sun/Mon/Tue/Wed/Thu/Fri/Sat

		"F", NULL, // 1~5 (0 padded Week of Month, first day of week = Monday)

		"g", NULL, // Julian Day Number (number of days since 4713 BC January 1)
		"GGGG", NULL, // Before Christ/Anno Domini
		"GGG", NULL, // BC/AD (Era Designator Abbreviated)
		"GG", NULL, // BC/AD (Era Designator Abbreviated)
		"G", NULL, // BC/AD (Era Designator Abbreviated)

		"h", "%I", // 1~12 (0 padded Hour (12hr))
		"H", "%H", // 0~23 (0 padded Hour (24hr))

		"k", NULL, // 1~24 (0 padded Hour (24hr)
		"K", NULL, // 0~11 (0 padded Hour (12hr))

		"LLLL", "%B", // January/February/March/April/May/June/July/August/September/October/November/December
		"LLL", "%b", // Jan/Feb/Mar/Apr/May/Jun/Jul/Aug/Sep/Oct/Nov/Dec
		"LL", "%m", // 1~12 (0 padded Month)
		"L", "%m", // 1~12 (0 padded Month)

		"m", "%M", // 0~59 (0 padded Minute)
		"MMMM", "%B", // January/February/March/April/May/June/July/August/September/October/November/December
		"MMM", "%b", // Jan/Feb/Mar/Apr/May/Jun/Jul/Aug/Sep/Oct/Nov/Dec
		"MM", "%m", // 1~12 (0 padded Month)
		"M", "%m", // 1~12 (0 padded Month)

		"qqqq", NULL, // 1st quarter/2nd quarter/3rd quarter/4th quarter
		"qqq", NULL, // Q1/Q2/Q3/Q4
		"qq", NULL, // 1~4 (0 padded Quarter)
		"q", NULL, // 1~4 (0 padded Quarter)
		"QQQQ", NULL, // 1st quarter/2nd quarter/3rd quarter/4th quarter
		"QQQ", NULL, // Q1/Q2/Q3/Q4
		"QQ", NULL, // 1~4 (0 padded Quarter)
		"Q", NULL, // 1~4 (0 padded Quarter)

		"s", "%S", // 0~59 (0 padded Second)
		"S", NULL, // (rounded Sub-Second)

		"u", "%Y", // (0 padded Year)

		"vvvv", "%Z", // (General GMT Timezone Name)
		"vvv", "%Z", // (General GMT Timezone Abbreviation)
		"vv", "%Z", // (General GMT Timezone Abbreviation)
		"v", "%Z", // (General GMT Timezone Abbreviation)

		"w", "%W", // 1~53 (0 padded Week of Year, 1st day of week = Sunday, NB, 1st week of year starts from the last Sunday of last year)
		"W", NULL, // 1~5 (0 padded Week of Month, 1st day of week = Sunday)

		"yyyy", "%Y", // (Full Year)
		"yyy", "%y", // (2 Digits Year)
		"yy", "%y", // (2 Digits Year)
		"y", "%Y", // (Full Year)
		"YYYY", NULL, // (Full Year, starting from the Sunday of the 1st week of year)
		"YYY", NULL, // (2 Digits Year, starting from the Sunday of the 1st week of year)
		"YY", NULL, // (2 Digits Year, starting from the Sunday of the 1st week of year)
		"Y", NULL, // (Full Year, starting from the Sunday of the 1st week of year)

		"zzzz", NULL, // (Specific GMT Timezone Name)
		"zzz", NULL, // (Specific GMT Timezone Abbreviation)
		"zz", NULL, // (Specific GMT Timezone Abbreviation)
		"z", NULL, // (Specific GMT Timezone Abbreviation)
		"Z", "%z", // +0000 (RFC 822 Timezone)
	};
	const gchar *str;
	GString *string;
	guint i, j;

	if (nsdate == NULL) {
		return NULL;
	}

	str = g_hash_table_lookup (data->date_format_cache, nsdate);
	if (str != NULL) {
		return str;
	}

	/* Copy nsdate into string, replacing occurences of NSDateFormatter tags
	 * by corresponding strftime tag. */
	string = g_string_sized_new (strlen (nsdate));
	for (i = 0; nsdate[i] != '\0'; i++) {
		gboolean found = FALSE;

		/* even indexes are NSDateFormatter tag, odd indexes are the
		 * corresponding strftime tag */
		for (j = 0; j < G_N_ELEMENTS (convert_table); j += 2) {
			if (g_str_has_prefix (nsdate + i, convert_table[j])) {
				found = TRUE;
				break;
			}
		}
		if (found) {
			/* If we don't have a replacement, just ignore that tag */
			if (convert_table[j + 1] != NULL) {
				g_string_append (string, convert_table[j + 1]);
			}
			i += strlen (convert_table[j]) - 1;
		} else {
			g_string_append_c (string, nsdate[i]);
		}
	}

	DEBUG ("Date format converted '%s' → '%s'", nsdate, string->str);

	/* The cache takes ownership of string->str */
	g_hash_table_insert (data->date_format_cache, g_strdup (nsdate), string->str);
	return g_string_free (string, FALSE);
}


static void
theme_adium_append_html (EmpathyThemeAdium *theme,
			 const gchar       *func,
			 const gchar       *html,
		         const gchar       *message,
		         const gchar       *avatar_filename,
		         const gchar       *name,
		         const gchar       *contact_id,
		         const gchar       *service_name,
		         const gchar       *message_classes,
		         gint64             timestamp,
		         gboolean           is_backlog,
		         gboolean           outgoing)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	GString     *string;
	const gchar *cur = NULL;
	gchar       *script;

	/* Make some search-and-replace in the html code */
	string = g_string_sized_new (strlen (html) + strlen (message));
	g_string_append_printf (string, "%s(\"", func);
	for (cur = html; *cur != '\0'; cur++) {
		const gchar *replace = NULL;
		gchar       *dup_replace = NULL;
		gchar       *format = NULL;

		/* Those are all well known keywords that needs replacement in
		 * html files. Please keep them in the same order than the adium
		 * spec. See http://trac.adium.im/wiki/CreatingMessageStyles */
		if (theme_adium_match (&cur, "%userIconPath%")) {
			replace = avatar_filename;
		} else if (theme_adium_match (&cur, "%senderScreenName%")) {
			replace = contact_id;
		} else if (theme_adium_match (&cur, "%sender%")) {
			replace = name;
		} else if (theme_adium_match (&cur, "%senderColor%")) {
			/* A color derived from the user's name.
			 * FIXME: If a colon separated list of HTML colors is at
			 * Incoming/SenderColors.txt it will be used instead of
			 * the default colors.
			 */

			/* Ensure we always use the same color when sending messages
			 * (bgo #658821) */
			if (outgoing) {
				replace = "inherit";
			} else if (contact_id != NULL) {
				guint hash = g_str_hash (contact_id);
				replace = colors[hash % G_N_ELEMENTS (colors)];
			}
		} else if (theme_adium_match (&cur, "%senderStatusIcon%")) {
			/* FIXME: The path to the status icon of the sender
			 * (available, away, etc...)
			 */
		} else if (theme_adium_match (&cur, "%messageDirection%")) {
			/* FIXME: The text direction of the message
			 * (either rtl or ltr)
			 */
		} else if (theme_adium_match (&cur, "%senderDisplayName%")) {
			/* FIXME: The serverside (remotely set) name of the
			 * sender, such as an MSN display name.
			 *
			 *  We don't have access to that yet so we use
			 * local alias instead.
			 */
			replace = name;
		} else if (theme_adium_match_with_format (&cur, "%textbackgroundcolor{", &format)) {
			/* FIXME: This keyword is used to represent the
			 * highlight background color. "X" is the opacity of the
			 * background, ranges from 0 to 1 and can be any decimal
			 * between.
			 */
		} else if (theme_adium_match (&cur, "%message%")) {
			replace = message;
		} else if (theme_adium_match (&cur, "%time%") ||
			   theme_adium_match_with_format (&cur, "%time{", &format)) {
			const gchar *strftime_format;

			strftime_format = nsdate_to_strftime (priv->data, format);
			if (is_backlog) {
				dup_replace = empathy_time_to_string_local (timestamp,
					strftime_format ? strftime_format :
					EMPATHY_TIME_DATE_FORMAT_DISPLAY_SHORT);
			} else {
				dup_replace = empathy_time_to_string_local (timestamp,
					strftime_format ? strftime_format :
					EMPATHY_TIME_FORMAT_DISPLAY_SHORT);
			}
			replace = dup_replace;
		} else if (theme_adium_match (&cur, "%shortTime%")) {
			dup_replace = empathy_time_to_string_local (timestamp,
				EMPATHY_TIME_FORMAT_DISPLAY_SHORT);
			replace = dup_replace;
		} else if (theme_adium_match (&cur, "%service%")) {
			replace = service_name;
		} else if (theme_adium_match (&cur, "%variant%")) {
			/* FIXME: The name of the active message style variant,
			 * with all spaces replaced with an underscore.
			 * A variant named "Alternating Messages - Blue Red"
			 * will become "Alternating_Messages_-_Blue_Red".
			 */
		} else if (theme_adium_match (&cur, "%userIcons%")) {
			/* FIXME: mus t be "hideIcons" if use preference is set
			 * to hide avatars */
			replace = "showIcons";
		} else if (theme_adium_match (&cur, "%messageClasses%")) {
			replace = message_classes;
		} else if (theme_adium_match (&cur, "%status%")) {
			/* FIXME: A description of the status event. This is
			 * neither in the user's local language nor expected to
			 * be displayed; it may be useful to use a different div
			 * class to present different types of status messages.
			 * The following is a list of some of the more important
			 * status messages; your message style should be able to
			 * handle being shown a status message not in this list,
			 * as even at present the list is incomplete and is
			 * certain to become out of date in the future:
			 * 	online
			 *	offline
			 *	away
			 *	away_message
			 *	return_away
			 *	idle
			 *	return_idle
			 *	date_separator
			 *	contact_joined (group chats)
			 *	contact_left
			 *	error
			 *	timed_out
			 *	encryption (all OTR messages use this status)
			 *	purple (all IRC topic and join/part messages use this status)
			 *	fileTransferStarted
			 *	fileTransferCompleted
			 */
		} else {
			escape_and_append_len (string, cur, 1);
			continue;
		}

		/* Here we have a replacement to make */
		escape_and_append_len (string, replace, -1);

		g_free (dup_replace);
		g_free (format);
	}
	g_string_append (string, "\")");

	script = g_string_free (string, FALSE);
	webkit_web_view_execute_script (WEBKIT_WEB_VIEW (theme), script);
	g_free (script);
}

static void
theme_adium_append_event_escaped (EmpathyChatView *view,
				  const gchar     *escaped)
{
	EmpathyThemeAdium     *theme = EMPATHY_THEME_ADIUM (view);
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);

	theme_adium_append_html (theme, "appendMessage",
				 priv->data->status_html, escaped, NULL, NULL, NULL,
				 NULL, "event",
				 empathy_time_get_current (), FALSE, FALSE);

	/* There is no last contact */
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
	}
}

static void
theme_adium_remove_focus_marks (EmpathyThemeAdium *theme,
    WebKitDOMNodeList *nodes)
{
	guint i;

	/* Remove focus and firstFocus class */
	for (i = 0; i < webkit_dom_node_list_get_length (nodes); i++) {
		WebKitDOMNode *node = webkit_dom_node_list_item (nodes, i);
		WebKitDOMHTMLElement *element = WEBKIT_DOM_HTML_ELEMENT (node);
		gchar *class_name;
		gchar **classes, **iter;
		GString *new_class_name;
		gboolean first = TRUE;

		if (element == NULL) {
			continue;
		}

		class_name = webkit_dom_html_element_get_class_name (element);
		classes = g_strsplit (class_name, " ", -1);
		new_class_name = g_string_sized_new (strlen (class_name));
		for (iter = classes; *iter != NULL; iter++) {
			if (tp_strdiff (*iter, "focus") &&
			    tp_strdiff (*iter, "firstFocus")) {
				if (!first) {
					g_string_append_c (new_class_name, ' ');
				}
				g_string_append (new_class_name, *iter);
				first = FALSE;
			}
		}

		webkit_dom_html_element_set_class_name (element, new_class_name->str);

		g_free (class_name);
		g_strfreev (classes);
		g_string_free (new_class_name, TRUE);
	}
}

static void
theme_adium_remove_all_focus_marks (EmpathyThemeAdium *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	WebKitDOMDocument *dom;
	WebKitDOMNodeList *nodes;
	GError *error = NULL;

	if (!priv->has_unread_message)
		return;

	priv->has_unread_message = FALSE;

	dom = webkit_web_view_get_dom_document (WEBKIT_WEB_VIEW (theme));
	if (dom == NULL) {
		return;
	}

	/* Get all nodes with focus class */
	nodes = webkit_dom_document_query_selector_all (dom, ".focus", &error);
	if (nodes == NULL) {
		DEBUG ("Error getting focus nodes: %s",
			error ? error->message : "No error");
		g_clear_error (&error);
		return;
	}

	theme_adium_remove_focus_marks (theme, nodes);
}

static void
theme_adium_append_message (EmpathyChatView *view,
			    EmpathyMessage  *msg)
{
	EmpathyThemeAdium     *theme = EMPATHY_THEME_ADIUM (view);
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	EmpathyContact        *sender;
	TpMessage             *tp_msg;
	TpAccount             *account;
	gchar                 *body_escaped, *name_escaped;
	const gchar           *name;
	const gchar           *contact_id;
	EmpathyAvatar         *avatar;
	const gchar           *avatar_filename = NULL;
	gint64                 timestamp;
	const gchar           *html = NULL;
	const gchar           *func;
	const gchar           *service_name;
	GString               *message_classes = NULL;
	gboolean               is_backlog;
	gboolean               consecutive;
	gboolean               action;

	if (priv->pages_loading != 0) {
		queue_item (&priv->message_queue, QUEUED_MESSAGE, msg, NULL);
		return;
	}

	/* Get information */
	sender = empathy_message_get_sender (msg);
	account = empathy_contact_get_account (sender);
	service_name = empathy_protocol_name_to_display_name
		(tp_account_get_protocol (account));
	if (service_name == NULL)
		service_name = tp_account_get_protocol (account);
	timestamp = empathy_message_get_timestamp (msg);
	body_escaped = theme_adium_parse_body (theme,
		empathy_message_get_body (msg),
		empathy_message_get_token (msg));
	name = empathy_contact_get_logged_alias (sender);
	contact_id = empathy_contact_get_id (sender);
	action = (empathy_message_get_tptype (msg) == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION);

	name_escaped = g_markup_escape_text (name, -1);

	/* If this is a /me probably */
	if (action) {
		gchar *str;

		if (priv->data->version >= 4 || !priv->data->custom_template) {
			str = g_strdup_printf ("<span class='actionMessageUserName'>%s</span>"
					       "<span class='actionMessageBody'>%s</span>",
					       name_escaped, body_escaped);
		} else {
			str = g_strdup_printf ("*%s*", body_escaped);
		}
		g_free (body_escaped);
		body_escaped = str;
	}

	/* Get the avatar filename, or a fallback */
	avatar = empathy_contact_get_avatar (sender);
	if (avatar) {
		avatar_filename = avatar->filename;
	}
	if (!avatar_filename) {
		if (empathy_contact_is_user (sender)) {
			avatar_filename = priv->data->default_outgoing_avatar_filename;
		} else {
			avatar_filename = priv->data->default_incoming_avatar_filename;
		}
		if (!avatar_filename) {
			if (!priv->data->default_avatar_filename) {
				priv->data->default_avatar_filename =
					empathy_filename_from_icon_name (EMPATHY_IMAGE_AVATAR_DEFAULT,
									 GTK_ICON_SIZE_DIALOG);
			}
			avatar_filename = priv->data->default_avatar_filename;
		}
	}

	/* We want to join this message with the last one if
	 * - senders are the same contact,
	 * - last message was recieved recently,
	 * - last message and this message both are/aren't backlog, and
	 * - DisableCombineConsecutive is not set in theme's settings */
	is_backlog = empathy_message_is_backlog (msg);
	consecutive = empathy_contact_equal (priv->last_contact, sender) &&
		(timestamp - priv->last_timestamp < MESSAGE_JOIN_PERIOD) &&
		(is_backlog == priv->last_is_backlog) &&
		!tp_asv_get_boolean (priv->data->info,
				     "DisableCombineConsecutive", NULL);

	/* Define message classes */
	message_classes = g_string_new ("message");
	if (!priv->has_focus && !is_backlog) {
		if (!priv->has_unread_message) {
			g_string_append (message_classes, " firstFocus");
			priv->has_unread_message = TRUE;
		}
		g_string_append (message_classes, " focus");
	}
	if (is_backlog) {
		g_string_append (message_classes, " history");
	}
	if (consecutive) {
		g_string_append (message_classes, " consecutive");
	}
	if (empathy_contact_is_user (sender)) {
		g_string_append (message_classes, " outgoing");
	} else {
		g_string_append (message_classes, " incoming");
	}
	if (empathy_message_should_highlight (msg)) {
		g_string_append (message_classes, " mention");
	}
	if (empathy_message_get_tptype (msg) == TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY) {
		g_string_append (message_classes, " autoreply");
	}
	if (action) {
		g_string_append (message_classes, " action");
	}
	/* FIXME: other classes:
	 * status - the message is a status change
	 * event - the message is a notification of something happening
	 *         (for example, encryption being turned on)
	 * %status% - See %status% in theme_adium_append_html ()
	 */

	/* This is slightly a hack, but it's the only way to add
	 * arbitrary data to messages in the HTML. We add another
	 * class called "x-empathy-message-id-*" to the message. This
	 * way, we can remove the unread marker for this specific
	 * message later. */
	tp_msg = empathy_message_get_tp_message (msg);
	if (tp_msg != NULL) {
		guint32 id;
		gboolean valid;

		id = tp_message_get_pending_message_id (tp_msg, &valid);
		if (valid) {
			g_string_append_printf (message_classes,
			    " x-empathy-message-id-%u", id);
		}
	}

	/* Define javascript function to use */
	if (consecutive) {
		func = priv->allow_scrolling ? "appendNextMessage" : "appendNextMessageNoScroll";
	} else {
		func = priv->allow_scrolling ? "appendMessage" : "appendMessageNoScroll";
	}

	if (empathy_contact_is_user (sender)) {
		/* out */
		if (is_backlog) {
			/* context */
			html = consecutive ? priv->data->out_nextcontext_html : priv->data->out_context_html;
		} else {
			/* content */
			html = consecutive ? priv->data->out_nextcontent_html : priv->data->out_content_html;
		}

		/* remove all the unread marks when we are sending a message */
		theme_adium_remove_all_focus_marks (theme);
	} else {
		/* in */
		if (is_backlog) {
			/* context */
			html = consecutive ? priv->data->in_nextcontext_html : priv->data->in_context_html;
		} else {
			/* content */
			html = consecutive ? priv->data->in_nextcontent_html : priv->data->in_content_html;
		}
	}

	theme_adium_append_html (theme, func, html, body_escaped,
				 avatar_filename, name_escaped, contact_id,
				 service_name, message_classes->str,
				 timestamp, is_backlog, empathy_contact_is_user (sender));

	/* Keep the sender of the last displayed message */
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
	}
	priv->last_contact = g_object_ref (sender);
	priv->last_timestamp = timestamp;
	priv->last_is_backlog = is_backlog;

	g_free (body_escaped);
	g_free (name_escaped);
	g_string_free (message_classes, TRUE);
}

static void
theme_adium_append_event (EmpathyChatView *view,
			  const gchar     *str)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);
	gchar *str_escaped;

	if (priv->pages_loading != 0) {
		queue_item (&priv->message_queue, QUEUED_EVENT, NULL, str);
		return;
	}

	str_escaped = g_markup_escape_text (str, -1);
	theme_adium_append_event_escaped (view, str_escaped);
	g_free (str_escaped);
}

static void
theme_adium_append_event_markup (EmpathyChatView *view,
				 const gchar     *markup_text,
				 const gchar     *fallback_text)
{
	theme_adium_append_event_escaped (view, markup_text);
}

static void
theme_adium_edit_message (EmpathyChatView *view,
			  EmpathyMessage  *message)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);
	WebKitDOMDocument *doc;
	WebKitDOMElement *span;
	gchar *id, *parsed_body;
	gchar *tooltip, *timestamp;
	GtkIconInfo *icon_info;
	GError *error = NULL;

	if (priv->pages_loading != 0) {
		queue_item (&priv->message_queue, QUEUED_EDIT, message, NULL);
		return;
	}

	id = g_strdup_printf ("message-token-%s",
		empathy_message_get_supersedes (message));
	/* we don't pass a token here, because doing so will return another
	 * <span> element, and we don't want nested <span> elements */
	parsed_body = theme_adium_parse_body (EMPATHY_THEME_ADIUM (view),
		empathy_message_get_body (message), NULL);

	/* find the element */
	doc = webkit_web_view_get_dom_document (WEBKIT_WEB_VIEW (view));
	span = webkit_dom_document_get_element_by_id (doc, id);

	if (span == NULL) {
		DEBUG ("Failed to find id '%s'", id);
		goto except;
	}

	if (!WEBKIT_DOM_IS_HTML_ELEMENT (span)) {
		DEBUG ("Not a HTML element");
		goto except;
	}

	/* update the HTML */
	webkit_dom_html_element_set_inner_html (WEBKIT_DOM_HTML_ELEMENT (span),
		parsed_body, &error);

	if (error != NULL) {
		DEBUG ("Error setting new inner-HTML: %s", error->message);
		g_error_free (error);
		goto except;
	}

	/* set a tooltip */
	timestamp = empathy_time_to_string_local (
		empathy_message_get_timestamp (message),
		"%H:%M:%S");
	tooltip = g_strdup_printf (_("Message edited at %s"), timestamp);

	webkit_dom_html_element_set_title (WEBKIT_DOM_HTML_ELEMENT (span),
		tooltip);

	g_free (tooltip);
	g_free (timestamp);

	/* mark this message as edited */
	icon_info = gtk_icon_theme_lookup_icon (gtk_icon_theme_get_default (),
		EMPATHY_IMAGE_EDIT_MESSAGE, 16, 0);

	if (icon_info != NULL) {
		/* set the icon as a background image using CSS
		 * FIXME: the icon won't update in response to theme changes */
		gchar *style = g_strdup_printf (
			"background-image:url('%s');"
			"background-repeat:no-repeat;"
			"background-position:left center;"
			"padding-left:19px;", /* 16px icon + 3px padding */
			gtk_icon_info_get_filename (icon_info));

		webkit_dom_element_set_attribute (span, "style", style, &error);

		if (error != NULL) {
			DEBUG ("Error setting element style: %s",
				error->message);
			g_clear_error (&error);
			/* not fatal */
		}

		g_free (style);
		gtk_icon_info_free (icon_info);
	}

	goto finally;

except:
	DEBUG ("Could not find message to edit with: %s",
		empathy_message_get_body (message));

finally:
	g_free (id);
	g_free (parsed_body);
}

static void
theme_adium_scroll (EmpathyChatView *view,
		    gboolean         allow_scrolling)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);

	priv->allow_scrolling = allow_scrolling;
	if (allow_scrolling) {
		empathy_chat_view_scroll_down (view);
	}
}

static void
theme_adium_scroll_down (EmpathyChatView *view)
{
	webkit_web_view_execute_script (WEBKIT_WEB_VIEW (view), "alignChat(true);");
}

static gboolean
theme_adium_get_has_selection (EmpathyChatView *view)
{
	return webkit_web_view_has_selection (WEBKIT_WEB_VIEW (view));
}

static void
theme_adium_clear (EmpathyChatView *view)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);

	theme_adium_load_template (EMPATHY_THEME_ADIUM (view));

	/* Clear last contact to avoid trying to add a 'joined'
	 * message when we don't have an insertion point. */
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
	}
}

static gboolean
theme_adium_find_previous (EmpathyChatView *view,
			   const gchar     *search_criteria,
			   gboolean         new_search,
			   gboolean         match_case)
{
	/* FIXME: Doesn't respect new_search */
	return webkit_web_view_search_text (WEBKIT_WEB_VIEW (view),
					    search_criteria, match_case,
					    FALSE, TRUE);
}

static gboolean
theme_adium_find_next (EmpathyChatView *view,
		       const gchar     *search_criteria,
		       gboolean         new_search,
		       gboolean         match_case)
{
	/* FIXME: Doesn't respect new_search */
	return webkit_web_view_search_text (WEBKIT_WEB_VIEW (view),
					    search_criteria, match_case,
					    TRUE, TRUE);
}

static void
theme_adium_find_abilities (EmpathyChatView *view,
			    const gchar    *search_criteria,
                            gboolean        match_case,
			    gboolean       *can_do_previous,
			    gboolean       *can_do_next)
{
	/* FIXME: Does webkit provide an API for that? We have wrap=true in
	 * find_next and find_previous to work around this problem. */
	if (can_do_previous)
		*can_do_previous = TRUE;
	if (can_do_next)
		*can_do_next = TRUE;
}

static void
theme_adium_highlight (EmpathyChatView *view,
		       const gchar     *text,
		       gboolean         match_case)
{
	webkit_web_view_unmark_text_matches (WEBKIT_WEB_VIEW (view));
	webkit_web_view_mark_text_matches (WEBKIT_WEB_VIEW (view),
					   text, match_case, 0);
	webkit_web_view_set_highlight_text_matches (WEBKIT_WEB_VIEW (view),
						    TRUE);
}

static void
theme_adium_copy_clipboard (EmpathyChatView *view)
{
	webkit_web_view_copy_clipboard (WEBKIT_WEB_VIEW (view));
}

static void
theme_adium_remove_mark_from_message (EmpathyThemeAdium *self,
				      guint32 id)
{
	WebKitDOMDocument *dom;
	WebKitDOMNodeList *nodes;
	gchar *class;
	GError *error = NULL;

	dom = webkit_web_view_get_dom_document (WEBKIT_WEB_VIEW (self));
	if (dom == NULL) {
		return;
	}

	class = g_strdup_printf (".x-empathy-message-id-%u", id);

	/* Get all nodes with focus class */
	nodes = webkit_dom_document_query_selector_all (dom, class, &error);
	g_free (class);

	if (nodes == NULL) {
		DEBUG ("Error getting focus nodes: %s",
			error ? error->message : "No error");
		g_clear_error (&error);
		return;
	}

	theme_adium_remove_focus_marks (self, nodes);
}

static void
theme_adium_remove_acked_message_unread_mark_foreach (gpointer data,
						      gpointer user_data)
{
	EmpathyThemeAdium *self = user_data;
	guint32 id = GPOINTER_TO_UINT (data);

	theme_adium_remove_mark_from_message (self, id);
}

static void
theme_adium_focus_toggled (EmpathyChatView *view,
			   gboolean         has_focus)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);

	priv->has_focus = has_focus;
	if (!priv->has_focus) {
		/* We've lost focus, so let's make sure all the acked
		 * messages have lost their unread marker. */
		g_queue_foreach (&priv->acked_messages,
				 theme_adium_remove_acked_message_unread_mark_foreach,
				 view);
		g_queue_clear (&priv->acked_messages);

		priv->has_unread_message = FALSE;
	}
}

static void
theme_adium_message_acknowledged (EmpathyChatView *view,
				  EmpathyMessage  *message)
{
	EmpathyThemeAdium *self = (EmpathyThemeAdium *) view;
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);
	TpMessage *tp_msg;
	guint32 id;
	gboolean valid;

	tp_msg = empathy_message_get_tp_message (message);

	if (tp_msg == NULL) {
		return;
	}

	id = tp_message_get_pending_message_id (tp_msg, &valid);
	if (!valid) {
		g_warning ("Acknoledged message doesn't have a pending ID");
		return;
	}

	/* We only want to actually remove the unread marker if the
	 * view doesn't have focus. If we did it all the time we would
	 * never see the unread markers, ever! So, we'll queue these
	 * up, and when we lose focus, we'll remove the markers. */
	if (priv->has_focus) {
		g_queue_push_tail (&priv->acked_messages,
				   GUINT_TO_POINTER (id));
		return;
	}

	theme_adium_remove_mark_from_message (self, id);
}

static gboolean
theme_adium_button_press_event (GtkWidget *widget, GdkEventButton *event)
{
	if (event->button == 3) {
		gboolean developer_tools_enabled;

		g_object_get (G_OBJECT (webkit_web_view_get_settings (WEBKIT_WEB_VIEW (widget))),
			      "enable-developer-extras", &developer_tools_enabled, NULL);

		/* We currently have no way to add an inspector menu
		 * item ourselves, so we disable our customized menu
		 * if the developer extras are enabled. */
		if (!developer_tools_enabled) {
			empathy_webkit_context_menu_for_event (
				WEBKIT_WEB_VIEW (widget), event,
				EMPATHY_WEBKIT_MENU_CLEAR);
			return TRUE;
		}
	}

	return GTK_WIDGET_CLASS (empathy_theme_adium_parent_class)->button_press_event (widget, event);
}

static void
theme_adium_iface_init (EmpathyChatViewIface *iface)
{
	iface->append_message = theme_adium_append_message;
	iface->append_event = theme_adium_append_event;
	iface->append_event_markup = theme_adium_append_event_markup;
	iface->edit_message = theme_adium_edit_message;
	iface->scroll = theme_adium_scroll;
	iface->scroll_down = theme_adium_scroll_down;
	iface->get_has_selection = theme_adium_get_has_selection;
	iface->clear = theme_adium_clear;
	iface->find_previous = theme_adium_find_previous;
	iface->find_next = theme_adium_find_next;
	iface->find_abilities = theme_adium_find_abilities;
	iface->highlight = theme_adium_highlight;
	iface->copy_clipboard = theme_adium_copy_clipboard;
	iface->focus_toggled = theme_adium_focus_toggled;
	iface->message_acknowledged = theme_adium_message_acknowledged;
}

static void
theme_adium_load_finished_cb (WebKitWebView  *view,
			      WebKitWebFrame *frame,
			      gpointer        user_data)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);
	EmpathyChatView       *chat_view = EMPATHY_CHAT_VIEW (view);
	GList                 *l;

	DEBUG ("Page loaded");
	priv->pages_loading--;

	if (priv->pages_loading != 0)
		return;

	/* Display queued messages */
	for (l = priv->message_queue.head; l != NULL; l = l->next) {
		QueuedItem *item = l->data;

		switch (item->type)
		{
			case QUEUED_MESSAGE:
				theme_adium_append_message (chat_view, item->msg);
				break;

			case QUEUED_EDIT:
				theme_adium_edit_message (chat_view, item->msg);
				break;

			case QUEUED_EVENT:
				theme_adium_append_event (chat_view, item->str);
				break;
		}

		free_queued_item (item);
	}

	g_queue_clear (&priv->message_queue);
}

static void
theme_adium_finalize (GObject *object)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	empathy_adium_data_unref (priv->data);

	g_object_unref (priv->gsettings_chat);
	g_object_unref (priv->gsettings_desktop);

	G_OBJECT_CLASS (empathy_theme_adium_parent_class)->finalize (object);
}

static void
theme_adium_dispose (GObject *object)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	if (priv->smiley_manager) {
		g_object_unref (priv->smiley_manager);
		priv->smiley_manager = NULL;
	}

	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
	}

	if (priv->inspector_window) {
		gtk_widget_destroy (priv->inspector_window);
		priv->inspector_window = NULL;
	}

	if (priv->acked_messages.length > 0) {
		g_queue_clear (&priv->acked_messages);
	}

	G_OBJECT_CLASS (empathy_theme_adium_parent_class)->dispose (object);
}

static gboolean
theme_adium_inspector_show_window_cb (WebKitWebInspector *inspector,
				      EmpathyThemeAdium  *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);

	if (priv->inspector_window) {
		gtk_widget_show_all (priv->inspector_window);
	}

	return TRUE;
}

static gboolean
theme_adium_inspector_close_window_cb (WebKitWebInspector *inspector,
				       EmpathyThemeAdium  *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);

	if (priv->inspector_window) {
		gtk_widget_hide (priv->inspector_window);
	}

	return TRUE;
}

static WebKitWebView *
theme_adium_inspect_web_view_cb (WebKitWebInspector *inspector,
				 WebKitWebView      *web_view,
				 EmpathyThemeAdium  *theme)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	GtkWidget             *scrolled_window;
	GtkWidget             *inspector_web_view;

	if (!priv->inspector_window) {
		/* Create main window */
		priv->inspector_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_default_size (GTK_WINDOW (priv->inspector_window),
					     800, 600);
		g_signal_connect (priv->inspector_window, "delete-event",
				  G_CALLBACK (gtk_widget_hide_on_delete), NULL);

		/* Pack a scrolled window */
		scrolled_window = gtk_scrolled_window_new (NULL, NULL);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
						GTK_POLICY_AUTOMATIC,
						GTK_POLICY_AUTOMATIC);
		gtk_container_add (GTK_CONTAINER (priv->inspector_window),
				   scrolled_window);
		gtk_widget_show  (scrolled_window);

		/* Pack a webview in the scrolled window. That webview will be
		 * used to render the inspector tool.  */
		inspector_web_view = webkit_web_view_new ();
		gtk_container_add (GTK_CONTAINER (scrolled_window),
				   inspector_web_view);
		gtk_widget_show (scrolled_window);

		return WEBKIT_WEB_VIEW (inspector_web_view);
	}

	return NULL;
}

static void
theme_adium_constructed (GObject *object)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);
	const gchar           *font_family = NULL;
	gint                   font_size = 0;
	WebKitWebView         *webkit_view = WEBKIT_WEB_VIEW (object);
	WebKitWebInspector    *webkit_inspector;

	/* Set default settings */
	font_family = tp_asv_get_string (priv->data->info, "DefaultFontFamily");
	font_size = tp_asv_get_int32 (priv->data->info, "DefaultFontSize", NULL);

	if (font_family && font_size) {
		g_object_set (webkit_web_view_get_settings (webkit_view),
			"default-font-family", font_family,
			"default-font-size", font_size,
			NULL);
	} else {
		empathy_webkit_bind_font_setting (webkit_view,
			priv->gsettings_desktop,
			EMPATHY_PREFS_DESKTOP_INTERFACE_DOCUMENT_FONT_NAME);
	}

	/* Setup webkit inspector */
	webkit_inspector = webkit_web_view_get_inspector (webkit_view);
	g_signal_connect (webkit_inspector, "inspect-web-view",
			  G_CALLBACK (theme_adium_inspect_web_view_cb),
			  object);
	g_signal_connect (webkit_inspector, "show-window",
			  G_CALLBACK (theme_adium_inspector_show_window_cb),
			  object);
	g_signal_connect (webkit_inspector, "close-window",
			  G_CALLBACK (theme_adium_inspector_close_window_cb),
			  object);

	/* Load template */
	theme_adium_load_template (EMPATHY_THEME_ADIUM (object));

	priv->in_construction = FALSE;
}

static void
theme_adium_get_property (GObject    *object,
			  guint       param_id,
			  GValue     *value,
			  GParamSpec *pspec)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ADIUM_DATA:
		g_value_set_boxed (value, priv->data);
		break;
	case PROP_VARIANT:
		g_value_set_string (value, priv->variant);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
theme_adium_set_property (GObject      *object,
			  guint         param_id,
			  const GValue *value,
			  GParamSpec   *pspec)
{
	EmpathyThemeAdium *theme = EMPATHY_THEME_ADIUM (object);
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ADIUM_DATA:
		g_assert (priv->data == NULL);
		priv->data = g_value_dup_boxed (value);
		break;
	case PROP_VARIANT:
		empathy_theme_adium_set_variant (theme, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
empathy_theme_adium_class_init (EmpathyThemeAdiumClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass* widget_class = GTK_WIDGET_CLASS (klass);

	object_class->finalize = theme_adium_finalize;
	object_class->dispose = theme_adium_dispose;
	object_class->constructed = theme_adium_constructed;
	object_class->get_property = theme_adium_get_property;
	object_class->set_property = theme_adium_set_property;

	widget_class->button_press_event = theme_adium_button_press_event;

	g_object_class_install_property (object_class,
					 PROP_ADIUM_DATA,
					 g_param_spec_boxed ("adium-data",
							     "The theme data",
							     "Data for the adium theme",
							      EMPATHY_TYPE_ADIUM_DATA,
							      G_PARAM_CONSTRUCT_ONLY |
							      G_PARAM_READWRITE |
							      G_PARAM_STATIC_STRINGS));
	g_object_class_install_property (object_class,
					 PROP_VARIANT,
					 g_param_spec_string ("variant",
							      "The theme variant",
							      "Variant name for the theme",
							      NULL,
							      G_PARAM_CONSTRUCT |
							      G_PARAM_READWRITE |
							      G_PARAM_STATIC_STRINGS));

	g_type_class_add_private (object_class, sizeof (EmpathyThemeAdiumPriv));
}

static void
empathy_theme_adium_init (EmpathyThemeAdium *theme)
{
	EmpathyThemeAdiumPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (theme,
		EMPATHY_TYPE_THEME_ADIUM, EmpathyThemeAdiumPriv);

	theme->priv = priv;

	priv->in_construction = TRUE;
	g_queue_init (&priv->message_queue);
	priv->allow_scrolling = TRUE;
	priv->smiley_manager = empathy_smiley_manager_dup_singleton ();

	g_signal_connect (theme, "load-finished",
			  G_CALLBACK (theme_adium_load_finished_cb),
			  NULL);
	g_signal_connect (theme, "navigation-policy-decision-requested",
			  G_CALLBACK (theme_adium_navigation_policy_decision_requested_cb),
			  NULL);

	priv->gsettings_chat = g_settings_new (EMPATHY_PREFS_CHAT_SCHEMA);
	priv->gsettings_desktop = g_settings_new (
		EMPATHY_PREFS_DESKTOP_INTERFACE_SCHEMA);

	g_signal_connect (priv->gsettings_chat,
		"changed::" EMPATHY_PREFS_CHAT_WEBKIT_DEVELOPER_TOOLS,
		G_CALLBACK (theme_adium_notify_enable_webkit_developer_tools_cb),
		theme);

	theme_adium_update_enable_webkit_developer_tools (theme);
}

EmpathyThemeAdium *
empathy_theme_adium_new (EmpathyAdiumData *data,
			 const gchar *variant)
{
	g_return_val_if_fail (data != NULL, NULL);

	return g_object_new (EMPATHY_TYPE_THEME_ADIUM,
			     "adium-data", data,
			     "variant", variant,
			     NULL);
}

void
empathy_theme_adium_set_variant (EmpathyThemeAdium *theme,
				 const gchar *variant)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	gchar *variant_path;
	gchar *script;

	if (!tp_strdiff (priv->variant, variant)) {
		return;
	}

	g_free (priv->variant);
	priv->variant = g_strdup (variant);

	if (priv->in_construction) {
		return;
	}

	DEBUG ("Update view with variant: '%s'", variant);
	variant_path = adium_info_dup_path_for_variant (priv->data->info,
		priv->variant);
	script = g_strdup_printf ("setStylesheet(\"mainStyle\",\"%s\");", variant_path);

	webkit_web_view_execute_script (WEBKIT_WEB_VIEW (theme), script);

	g_free (variant_path);
	g_free (script);

	g_object_notify (G_OBJECT (theme), "variant");
}

void
empathy_theme_adium_show_inspector (EmpathyThemeAdium *theme)
{
	WebKitWebView      *web_view = WEBKIT_WEB_VIEW (theme);
	WebKitWebInspector *inspector;

	g_object_set (G_OBJECT (webkit_web_view_get_settings (web_view)),
		      "enable-developer-extras", TRUE,
		      NULL);

	inspector = webkit_web_view_get_inspector (web_view);
	webkit_web_inspector_show (inspector);
}

gboolean
empathy_adium_path_is_valid (const gchar *path)
{
	gboolean ret;
	gchar   *file;

	/* The theme is not valid if there is no Info.plist */
	file = g_build_filename (path, "Contents", "Info.plist",
				 NULL);
	ret = g_file_test (file, G_FILE_TEST_EXISTS);
	g_free (file);

	if (!ret)
		return FALSE;

	/* We ship a default Template.html as fallback if there is any problem
	 * with the one inside the theme. The only other required file is
	 * Content.html OR Incoming/Content.html*/
	file = g_build_filename (path, "Contents", "Resources", "Content.html",
				 NULL);
	ret = g_file_test (file, G_FILE_TEST_EXISTS);
	g_free (file);

	if (!ret) {
		file = g_build_filename (path, "Contents", "Resources", "Incoming",
					 "Content.html", NULL);
		ret = g_file_test (file, G_FILE_TEST_EXISTS);
		g_free (file);
	}

	return ret;
}

GHashTable *
empathy_adium_info_new (const gchar *path)
{
	gchar *file;
	GValue *value;
	GHashTable *info = NULL;

	g_return_val_if_fail (empathy_adium_path_is_valid (path), NULL);

	file = g_build_filename (path, "Contents", "Info.plist", NULL);
	value = empathy_plist_parse_from_file (file);
	g_free (file);

	if (value == NULL)
		return NULL;

	info = g_value_dup_boxed (value);
	tp_g_value_slice_free (value);

	/* Insert the theme's path into the hash table,
	 * keys have to be dupped */
	tp_asv_set_string (info, g_strdup ("path"), path);

	return info;
}

static guint
adium_info_get_version (GHashTable *info)
{
	return tp_asv_get_int32 (info, "MessageViewVersion", NULL);
}

static const gchar *
adium_info_get_no_variant_name (GHashTable *info)
{
	const gchar *name = tp_asv_get_string (info, "DisplayNameForNoVariant");
	return name ? name : _("Normal");
}

static gchar *
adium_info_dup_path_for_variant (GHashTable *info,
				 const gchar *variant)
{
	guint version = adium_info_get_version (info);
	const gchar *no_variant = adium_info_get_no_variant_name (info);
	GPtrArray *variants;
	guint i;

	if (version <= 2 && !tp_strdiff (variant, no_variant)) {
		return g_strdup ("main.css");
	}

	variants = empathy_adium_info_get_available_variants (info);
	if (variants->len == 0)
		return g_strdup ("main.css");

	/* Verify the variant exists, fallback to the first one */
	for (i = 0; i < variants->len; i++) {
		if (!tp_strdiff (variant, g_ptr_array_index (variants, i))) {
			break;
		}
	}
	if (i == variants->len) {
		DEBUG ("Variant %s does not exist", variant);
		variant = g_ptr_array_index (variants, 0);
	}

	return g_strdup_printf ("Variants/%s.css", variant);

}

const gchar *
empathy_adium_info_get_default_variant (GHashTable *info)
{
	if (adium_info_get_version (info) <= 2) {
		return adium_info_get_no_variant_name (info);
	}

	return tp_asv_get_string (info, "DefaultVariant");
}

GPtrArray *
empathy_adium_info_get_available_variants (GHashTable *info)
{
	GPtrArray *variants;
	const gchar *path;
	gchar *dirpath;
	GDir *dir;

	variants = tp_asv_get_boxed (info, "AvailableVariants", G_TYPE_PTR_ARRAY);
	if (variants != NULL) {
		return variants;
	}

	variants = g_ptr_array_new_with_free_func (g_free);
	tp_asv_take_boxed (info, g_strdup ("AvailableVariants"),
		G_TYPE_PTR_ARRAY, variants);

	path = tp_asv_get_string (info, "path");
	dirpath = g_build_filename (path, "Contents", "Resources", "Variants", NULL);
	dir = g_dir_open (dirpath, 0, NULL);
	if (dir != NULL) {
		const gchar *name;

		for (name = g_dir_read_name (dir);
		     name != NULL;
		     name = g_dir_read_name (dir)) {
			gchar *display_name;

			if (!g_str_has_suffix (name, ".css")) {
				continue;
			}

			display_name = g_strdup (name);
			strstr (display_name, ".css")[0] = '\0';
			g_ptr_array_add (variants, display_name);
		}
		g_dir_close (dir);
	}
	g_free (dirpath);

	if (adium_info_get_version (info) <= 2) {
		g_ptr_array_add (variants,
			g_strdup (adium_info_get_no_variant_name (info)));
	}

	return variants;
}

GType
empathy_adium_data_get_type (void)
{
  static GType type_id = 0;

  if (!type_id)
    {
      type_id = g_boxed_type_register_static ("EmpathyAdiumData",
          (GBoxedCopyFunc) empathy_adium_data_ref,
          (GBoxedFreeFunc) empathy_adium_data_unref);
    }

  return type_id;
}

EmpathyAdiumData  *
empathy_adium_data_new_with_info (const gchar *path, GHashTable *info)
{
	EmpathyAdiumData *data;
	gchar            *template_html = NULL;
	gchar            *footer_html = NULL;
	gchar            *tmp;

	g_return_val_if_fail (empathy_adium_path_is_valid (path), NULL);

	data = g_slice_new0 (EmpathyAdiumData);
	data->ref_count = 1;
	data->path = g_strdup (path);
	data->basedir = g_strconcat (path, G_DIR_SEPARATOR_S "Contents"
		G_DIR_SEPARATOR_S "Resources" G_DIR_SEPARATOR_S, NULL);
	data->info = g_hash_table_ref (info);
	data->version = adium_info_get_version (info);
	data->strings_to_free = g_ptr_array_new_with_free_func (g_free);
	data->date_format_cache = g_hash_table_new_full (g_str_hash,
		g_str_equal, g_free, g_free);

	DEBUG ("Loading theme at %s", path);

#define LOAD(path, var) \
		tmp = g_build_filename (data->basedir, path, NULL); \
		g_file_get_contents (tmp, &var, NULL, NULL); \
		g_free (tmp); \

#define LOAD_CONST(path, var) \
	{ \
		gchar *content; \
		LOAD (path, content); \
		if (content != NULL) { \
			g_ptr_array_add (data->strings_to_free, content); \
		} \
		var = content; \
	}

	/* Load html files */
	LOAD_CONST ("Content.html", data->content_html);
	LOAD_CONST ("Incoming/Content.html", data->in_content_html);
	LOAD_CONST ("Incoming/NextContent.html", data->in_nextcontent_html);
	LOAD_CONST ("Incoming/Context.html", data->in_context_html);
	LOAD_CONST ("Incoming/NextContext.html", data->in_nextcontext_html);
	LOAD_CONST ("Outgoing/Content.html", data->out_content_html);
	LOAD_CONST ("Outgoing/NextContent.html", data->out_nextcontent_html);
	LOAD_CONST ("Outgoing/Context.html", data->out_context_html);
	LOAD_CONST ("Outgoing/NextContext.html", data->out_nextcontext_html);
	LOAD_CONST ("Status.html", data->status_html);
	LOAD ("Template.html", template_html);
	LOAD ("Footer.html", footer_html);

#undef LOAD_CONST
#undef LOAD

	/* HTML fallbacks: If we have at least content OR in_content, then
	 * everything else gets a fallback */

#define FALLBACK(html, fallback) \
	if (html == NULL) { \
		html = fallback; \
	}

	/* in_nextcontent -> in_content -> content */
	FALLBACK (data->in_content_html,      data->content_html);
	FALLBACK (data->in_nextcontent_html,  data->in_content_html);

	/* context -> content */
	FALLBACK (data->in_context_html,      data->in_content_html);
	FALLBACK (data->in_nextcontext_html,  data->in_nextcontent_html);
	FALLBACK (data->out_context_html,     data->out_content_html);
	FALLBACK (data->out_nextcontext_html, data->out_nextcontent_html);

	/* out -> in */
	FALLBACK (data->out_content_html,     data->in_content_html);
	FALLBACK (data->out_nextcontent_html, data->in_nextcontent_html);
	FALLBACK (data->out_context_html,     data->in_context_html);
	FALLBACK (data->out_nextcontext_html, data->in_nextcontext_html);

	/* status -> in_content */
	FALLBACK (data->status_html,          data->in_content_html);

#undef FALLBACK

	/* template -> empathy's template */
	data->custom_template = (template_html != NULL);
	if (template_html == NULL) {
		GError *error = NULL;

		tmp = empathy_file_lookup ("Template.html", "data");

		if (!g_file_get_contents (tmp, &template_html, NULL, &error)) {
			g_warning ("couldn't load Empathy's default theme "
				"template: %s", error->message);
			g_return_val_if_reached (data);
		}

		g_free (tmp);
	}

	/* Default avatar */
	tmp = g_build_filename (data->basedir, "Incoming", "buddy_icon.png", NULL);
	if (g_file_test (tmp, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		data->default_incoming_avatar_filename = tmp;
	} else {
		g_free (tmp);
	}
	tmp = g_build_filename (data->basedir, "Outgoing", "buddy_icon.png", NULL);
	if (g_file_test (tmp, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		data->default_outgoing_avatar_filename = tmp;
	} else {
		g_free (tmp);
	}

	/* Old custom templates had only 4 parameters.
	 * New templates have 5 parameters */
	if (data->version <= 2 && data->custom_template) {
		tmp = string_with_format (template_html,
			data->basedir,
			"%@", /* Leave variant unset */
			"", /* The header */
			footer_html ? footer_html : "",
			NULL);
	} else {
		tmp = string_with_format (template_html,
			data->basedir,
			data->version <= 2 ? "" : "@import url( \"main.css\" );",
			"%@", /* Leave variant unset */
			"", /* The header */
			footer_html ? footer_html : "",
			NULL);
	}
	g_ptr_array_add (data->strings_to_free, tmp);
	data->template_html = tmp;

	g_free (template_html);
	g_free (footer_html);

	return data;
}

EmpathyAdiumData  *
empathy_adium_data_new (const gchar *path)
{
	EmpathyAdiumData *data;
	GHashTable *info;

	info = empathy_adium_info_new (path);
	data = empathy_adium_data_new_with_info (path, info);
	g_hash_table_unref (info);

	return data;
}

EmpathyAdiumData  *
empathy_adium_data_ref (EmpathyAdiumData *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	g_atomic_int_inc (&data->ref_count);

	return data;
}

void
empathy_adium_data_unref (EmpathyAdiumData *data)
{
	g_return_if_fail (data != NULL);

	if (g_atomic_int_dec_and_test (&data->ref_count)) {
		g_free (data->path);
		g_free (data->basedir);
		g_free (data->default_avatar_filename);
		g_free (data->default_incoming_avatar_filename);
		g_free (data->default_outgoing_avatar_filename);
		g_hash_table_unref (data->info);
		g_ptr_array_unref (data->strings_to_free);
		tp_clear_pointer (&data->date_format_cache, g_hash_table_unref);

		g_slice_free (EmpathyAdiumData, data);
	}
}

GHashTable *
empathy_adium_data_get_info (EmpathyAdiumData *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	return data->info;
}

const gchar *
empathy_adium_data_get_path (EmpathyAdiumData *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	return data->path;
}


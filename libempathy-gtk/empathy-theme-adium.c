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
#include <glib/gi18n.h>

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
#include "empathy-string-parser.h"
#include "empathy-images.h"

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
	GList                *message_queue;
	GtkWidget            *inspector_window;
	GSettings            *gsettings_chat;
	gboolean              has_focus;
	gboolean              has_unread_message;
} EmpathyThemeAdiumPriv;

struct _EmpathyAdiumData {
	gint  ref_count;
	gchar *path;
	gchar *basedir;
	gchar *default_avatar_filename;
	gchar *default_incoming_avatar_filename;
	gchar *default_outgoing_avatar_filename;
	gchar *template_html;
	gchar *content_html;
	gsize  content_len;
	GHashTable *info;

	/* Legacy themes */
	gchar *in_content_html;
	gsize  in_content_len;
	gchar *in_context_html;
	gsize  in_context_len;
	gchar *in_nextcontent_html;
	gsize  in_nextcontent_len;
	gchar *in_nextcontext_html;
	gsize  in_nextcontext_len;
	gchar *out_content_html;
	gsize  out_content_len;
	gchar *out_context_html;
	gsize  out_context_len;
	gchar *out_nextcontent_html;
	gsize  out_nextcontent_len;
	gchar *out_nextcontext_html;
	gsize  out_nextcontext_len;
	gchar *status_html;
	gsize  status_len;
};

static void theme_adium_iface_init (EmpathyChatViewIface *iface);

enum {
	PROP_0,
	PROP_ADIUM_DATA,
};

G_DEFINE_TYPE_WITH_CODE (EmpathyThemeAdium, empathy_theme_adium,
			 WEBKIT_TYPE_WEB_VIEW,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CHAT_VIEW,
						theme_adium_iface_init));

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

static void
theme_adium_copy_address_cb (GtkMenuItem *menuitem,
			     gpointer     user_data)
{
	WebKitHitTestResult   *hit_test_result = WEBKIT_HIT_TEST_RESULT (user_data);
	gchar                 *uri;
	GtkClipboard          *clipboard;

	g_object_get (G_OBJECT (hit_test_result), "link-uri", &uri, NULL);

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, uri, -1);

	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text (clipboard, uri, -1);

	g_free (uri);
}

static void
theme_adium_open_address_cb (GtkMenuItem *menuitem,
			     gpointer     user_data)
{
	WebKitHitTestResult   *hit_test_result = WEBKIT_HIT_TEST_RESULT (user_data);
	gchar                 *uri;

	g_object_get (G_OBJECT (hit_test_result), "link-uri", &uri, NULL);

	empathy_url_show (GTK_WIDGET (menuitem), uri);

	g_free (uri);
}

static void
theme_adium_match_newline (const gchar *text,
			   gssize len,
			   EmpathyStringReplace replace_func,
			   EmpathyStringParser *sub_parsers,
			   gpointer user_data)
{
	GString *string = user_data;
	gint i;
	gint prev = 0;

	if (len < 0) {
		len = G_MAXSSIZE;
	}

	/* Replace \n by <br/> */
	for (i = 0; i < len && text[i] != '\0'; i++) {
		if (text[i] == '\n') {
			empathy_string_parser_substr (text + prev,
						      i - prev, sub_parsers,
						      user_data);
			g_string_append (string, "<br/>");
			prev = i + 1;
		}
	}
	empathy_string_parser_substr (text + prev, i - prev,
				      sub_parsers, user_data);
}

static void
theme_adium_replace_smiley (const gchar *text,
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
	{empathy_string_match_link, empathy_string_replace_link},
	{theme_adium_match_newline, NULL},
	{empathy_string_match_all, empathy_string_replace_escaped},
	{NULL, NULL}
};

static EmpathyStringParser string_parsers_with_smiley[] = {
	{empathy_string_match_link, empathy_string_replace_link},
	{empathy_string_match_smiley, theme_adium_replace_smiley},
	{theme_adium_match_newline, NULL},
	{empathy_string_match_all, empathy_string_replace_escaped},
	{NULL, NULL}
};

static gchar *
theme_adium_parse_body (EmpathyThemeAdium *self,
	const gchar *text)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (self);
	EmpathyStringParser *parsers;
	GString *string;

	/* Check if we have to parse smileys */
	if (g_settings_get_boolean (priv->gsettings_chat,
	  EMPATHY_PREFS_CHAT_SHOW_SMILEYS))
		parsers = string_parsers_with_smiley;
	else
		parsers = string_parsers;

	/* Parse text and construct string with links and smileys replaced
	 * by html tags. Also escape text to make sure html code is
	 * displayed verbatim. */
	string = g_string_sized_new (strlen (text));
	empathy_string_parser_substr (text, -1, parsers, string);

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

static void
theme_adium_append_html (EmpathyThemeAdium *theme,
			 const gchar       *func,
			 const gchar       *html, gsize len,
		         const gchar       *message,
		         const gchar       *avatar_filename,
		         const gchar       *name,
		         const gchar       *contact_id,
		         const gchar       *service_name,
		         const gchar       *message_classes,
		         gint64             timestamp,
		         gboolean           is_backlog)
{
	GString     *string;
	const gchar *cur = NULL;
	gchar       *script;

	/* Make some search-and-replace in the html code */
	string = g_string_sized_new (len + strlen (message));
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
			guint hash = g_str_hash (contact_id);

			replace = colors[hash % G_N_ELEMENTS (colors)];
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
			/* FIXME: format is not exactly strftime.
			 * See NSDateFormatter spec:
			 * http://www.stepcase.com/blog/2008/12/02/format-string-for-the-iphone-nsdateformatter/
			 */
			if (is_backlog) {
				dup_replace = empathy_time_to_string_local (timestamp,
					format ? format : EMPATHY_TIME_DATE_FORMAT_DISPLAY_SHORT);
			} else {
				dup_replace = empathy_time_to_string_local (timestamp,
					format ? format : EMPATHY_TIME_FORMAT_DISPLAY_SHORT);
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
	gchar                 *html;
	gsize                  len;

	html = priv->data->content_html;
	len = priv->data->content_len;

	/* Fallback to legacy status_html */
	if (html == NULL) {
		html = priv->data->status_html;
		len = priv->data->status_len;
	}

	if (html != NULL) {
		theme_adium_append_html (theme, "appendMessage",
					 html, len, escaped, NULL, NULL, NULL,
					 NULL, "event",
					 empathy_time_get_current (), FALSE);
	} else {
		DEBUG ("Couldn't find HTML file for this event");
	}

	/* There is no last contact */
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
	}
}

static void
theme_adium_remove_focus_marks (EmpathyThemeAdium *theme)
{
	WebKitDOMDocument *dom;
	WebKitDOMNodeList *nodes;
	guint i;
	GError *error = NULL;

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
theme_adium_append_message (EmpathyChatView *view,
			    EmpathyMessage  *msg)
{
	EmpathyThemeAdium     *theme = EMPATHY_THEME_ADIUM (view);
	EmpathyThemeAdiumPriv *priv = GET_PRIV (theme);
	EmpathyContact        *sender;
	TpAccount             *account;
	gchar                 *body_escaped;
	const gchar           *body;
	const gchar           *name;
	const gchar           *contact_id;
	EmpathyAvatar         *avatar;
	const gchar           *avatar_filename = NULL;
	gint64                 timestamp;
	gchar                 *html = NULL;
	gsize                  len = 0;
	const gchar           *func;
	const gchar           *service_name;
	GString               *message_classes = NULL;
	gboolean               is_backlog;
	gboolean               consecutive;

	if (priv->pages_loading != 0) {
		priv->message_queue = g_list_prepend (priv->message_queue,
						      g_object_ref (msg));
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
	body = empathy_message_get_body (msg);
	body_escaped = theme_adium_parse_body (theme, body);
	name = empathy_contact_get_alias (sender);
	contact_id = empathy_contact_get_id (sender);

	/* If this is a /me, append an event */
	if (empathy_message_get_tptype (msg) == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION) {
		gchar *str;

		str = g_strdup_printf ("%s %s", name, body_escaped);
		theme_adium_append_event_escaped (view, str);

		g_free (str);
		g_free (body_escaped);
		return;
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
			/* This is the first message we receive since we lost
			 * focus; remove previous unread marks. */
			theme_adium_remove_focus_marks (theme);

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
	/* FIXME: other classes:
	 * autoreply - the message is an automatic response, generally due to an
	 *             away status
	 * mention - the incoming message (in groupchat) matches your username
	 *           or one of the mention keywords specified in Adium's
	 *           advanced prefs.
	 * status - the message is a status change
	 * event - the message is a notification of something happening
	 *         (for example, encryption being turned on)
	 * %status% - See %status% in theme_adium_append_html ()
	 */

	/* Define javascript function to use */
	if (consecutive) {
		func = "appendNextMessage";
	} else {
		func = "appendMessage";
	}

	html = priv->data->content_html;
	len = priv->data->content_len;

	/* Fallback to legacy Outgoing */
	if (html == NULL && empathy_contact_is_user (sender)) {
		if (consecutive) {
			if (is_backlog) {
				html = priv->data->out_nextcontext_html;
				len = priv->data->out_nextcontext_len;
			}

			/* Not backlog, or fallback if NextContext.html
			 * is missing */
			if (html == NULL) {
				html = priv->data->out_nextcontent_html;
				len = priv->data->out_nextcontent_len;
			}
		}

		/* Not consecutive, or fallback if NextContext.html and/or
		 * NextContent.html are missing */
		if (html == NULL) {
			if (is_backlog) {
				html = priv->data->out_context_html;
				len = priv->data->out_context_len;
			}

			if (html == NULL) {
				html = priv->data->out_content_html;
				len = priv->data->out_content_len;
			}
		}
	}

	/* Incoming, or fallback if outgoing files are missing */
	if (html == NULL) {
		if (consecutive) {
			if (is_backlog) {
				html = priv->data->in_nextcontext_html;
				len = priv->data->in_nextcontext_len;
			}

			/* Note backlog, or fallback if NextContext.html
			 * is missing */
			if (html == NULL) {
				html = priv->data->in_nextcontent_html;
				len = priv->data->in_nextcontent_len;
			}
		}

		/* Not consecutive, or fallback if NextContext.html and/or
		 * NextContent.html are missing */
		if (html == NULL) {
			if (is_backlog) {
				html = priv->data->in_context_html;
				len = priv->data->in_context_len;
			}

			if (html == NULL) {
				html = priv->data->in_content_html;
				len = priv->data->in_content_len;
			}
		}
	}

	if (html != NULL) {
		theme_adium_append_html (theme, func, html, len, body_escaped,
					 avatar_filename, name, contact_id,
					 service_name, message_classes->str,
					 timestamp, is_backlog);
	} else {
		DEBUG ("Couldn't find HTML file for this message");
	}

	/* Keep the sender of the last displayed message */
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
	}
	priv->last_contact = g_object_ref (sender);
	priv->last_timestamp = timestamp;
	priv->last_is_backlog = is_backlog;

	g_free (body_escaped);
	g_string_free (message_classes, TRUE);
}

static void
theme_adium_append_event (EmpathyChatView *view,
			  const gchar     *str)
{
	gchar *str_escaped;

	str_escaped = g_markup_escape_text (str, -1);
	theme_adium_append_event_escaped (view, str_escaped);
	g_free (str_escaped);
}

static void
theme_adium_scroll (EmpathyChatView *view,
		    gboolean         allow_scrolling)
{
	/* FIXME: Is it possible? I guess we need a js function, but I don't
	 * see any... */
}

static void
theme_adium_scroll_down (EmpathyChatView *view)
{
	webkit_web_view_execute_script (WEBKIT_WEB_VIEW (view), "scrollToBottom()");
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
	gchar *basedir_uri;

	priv->pages_loading++;
	basedir_uri = g_strconcat ("file://", priv->data->basedir, NULL);
	webkit_web_view_load_html_string (WEBKIT_WEB_VIEW (view),
					  priv->data->template_html,
					  basedir_uri);
	g_free (basedir_uri);

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
theme_adium_focus_toggled (EmpathyChatView *view,
			   gboolean         has_focus)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);

	priv->has_focus = has_focus;
	if (priv->has_focus) {
		priv->has_unread_message = FALSE;
	}
}

static void
theme_adium_context_menu_selection_done_cb (GtkMenuShell *menu, gpointer user_data)
{
	WebKitHitTestResult *hit_test_result = WEBKIT_HIT_TEST_RESULT (user_data);

	g_object_unref (hit_test_result);
}

static void
theme_adium_context_menu_for_event (EmpathyThemeAdium *theme, GdkEventButton *event)
{
	WebKitWebView              *view = WEBKIT_WEB_VIEW (theme);
	WebKitHitTestResult        *hit_test_result;
	WebKitHitTestResultContext  context;
	GtkWidget                  *menu;
	GtkWidget                  *item;

	hit_test_result = webkit_web_view_get_hit_test_result (view, event);
	g_object_get (G_OBJECT (hit_test_result), "context", &context, NULL);

	/* The menu */
	menu = empathy_context_menu_new (GTK_WIDGET (view));

	/* Select all item */
	item = gtk_image_menu_item_new_from_stock (GTK_STOCK_SELECT_ALL, NULL);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (webkit_web_view_select_all),
				  view);

	/* Copy menu item */
	if (webkit_web_view_can_copy_clipboard (view)) {
		item = gtk_image_menu_item_new_from_stock (GTK_STOCK_COPY, NULL);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

		g_signal_connect_swapped (item, "activate",
					  G_CALLBACK (webkit_web_view_copy_clipboard),
					  view);
	}

	/* Clear menu item */
	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

	item = gtk_image_menu_item_new_from_stock (GTK_STOCK_CLEAR, NULL);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

	g_signal_connect_swapped (item, "activate",
				  G_CALLBACK (empathy_chat_view_clear),
				  view);

	/* We will only add the following menu items if we are
	 * right-clicking a link */
	if (context & WEBKIT_HIT_TEST_RESULT_CONTEXT_LINK) {
		/* Separator */
		item = gtk_separator_menu_item_new ();
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

		/* Copy Link Address menu item */
		item = gtk_menu_item_new_with_mnemonic (_("_Copy Link Address"));
		g_signal_connect (item, "activate",
				  G_CALLBACK (theme_adium_copy_address_cb),
				  hit_test_result);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);

		/* Open Link menu item */
		item = gtk_menu_item_new_with_mnemonic (_("_Open Link"));
		g_signal_connect (item, "activate",
				  G_CALLBACK (theme_adium_open_address_cb),
				  hit_test_result);
		gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	}

	g_signal_connect (GTK_MENU_SHELL (menu), "selection-done",
			  G_CALLBACK (theme_adium_context_menu_selection_done_cb),
			  hit_test_result);

	/* Display the menu */
	gtk_widget_show_all (menu);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			event->button, event->time);
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
			theme_adium_context_menu_for_event (EMPATHY_THEME_ADIUM (widget), event);
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
}

static void
theme_adium_load_finished_cb (WebKitWebView  *view,
			      WebKitWebFrame *frame,
			      gpointer        user_data)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (view);
	EmpathyChatView       *chat_view = EMPATHY_CHAT_VIEW (view);

	DEBUG ("Page loaded");
	priv->pages_loading--;

	if (priv->pages_loading != 0)
		return;

	/* Display queued messages */
	priv->message_queue = g_list_reverse (priv->message_queue);
	while (priv->message_queue) {
		EmpathyMessage *message = priv->message_queue->data;

		theme_adium_append_message (chat_view, message);
		priv->message_queue = g_list_remove (priv->message_queue, message);
		g_object_unref (message);
	}
}

static void
theme_adium_finalize (GObject *object)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	empathy_adium_data_unref (priv->data);
	g_object_unref (priv->gsettings_chat);

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

static PangoFontDescription *
theme_adium_get_default_font (void)
{
	GSettings *gsettings;
	PangoFontDescription *pango_fd;
	gchar *font_family;

	gsettings = g_settings_new (EMPATHY_PREFS_DESKTOP_INTERFACE_SCHEMA);

	font_family = g_settings_get_string (gsettings,
		     EMPATHY_PREFS_DESKTOP_INTERFACE_DOCUMENT_FONT_NAME);

	if (font_family == NULL)
		return NULL;

	pango_fd = pango_font_description_from_string (font_family);
	g_free (font_family);
	g_object_unref (gsettings);
	return pango_fd;
}

static void
theme_adium_set_webkit_font (WebKitWebSettings *w_settings,
			     const gchar *name,
			     gint size)
{
	g_object_set (w_settings, "default-font-family", name, NULL);
	g_object_set (w_settings, "default-font-size", size, NULL);
}

static void
theme_adium_set_default_font (WebKitWebSettings *w_settings)
{
	PangoFontDescription *default_font_desc;
	GdkScreen *current_screen;
	gdouble dpi = 0;
	gint pango_font_size = 0;

	default_font_desc = theme_adium_get_default_font ();
	if (default_font_desc == NULL)
		return ;
	pango_font_size = pango_font_description_get_size (default_font_desc)
		/ PANGO_SCALE ;
	if (pango_font_description_get_size_is_absolute (default_font_desc)) {
		current_screen = gdk_screen_get_default ();
		if (current_screen != NULL) {
			dpi = gdk_screen_get_resolution (current_screen);
		} else {
			dpi = BORING_DPI_DEFAULT;
		}
		pango_font_size = (gint) (pango_font_size / (dpi / 72));
	}
	theme_adium_set_webkit_font (w_settings,
		pango_font_description_get_family (default_font_desc),
		pango_font_size);
	pango_font_description_free (default_font_desc);
}

static void
theme_adium_constructed (GObject *object)
{
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);
	gchar                 *basedir_uri;
	const gchar           *font_family = NULL;
	gint                   font_size = 0;
	WebKitWebView         *webkit_view = WEBKIT_WEB_VIEW (object);
	WebKitWebSettings     *webkit_settings;
	WebKitWebInspector    *webkit_inspector;

	/* Set default settings */
	font_family = tp_asv_get_string (priv->data->info, "DefaultFontFamily");
	font_size = tp_asv_get_int32 (priv->data->info, "DefaultFontSize", NULL);
	webkit_settings = webkit_web_view_get_settings (webkit_view);

	if (font_family && font_size) {
		theme_adium_set_webkit_font (webkit_settings, font_family, font_size);
	} else {
		theme_adium_set_default_font (webkit_settings);
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
	priv->pages_loading = 1;

	basedir_uri = g_strconcat ("file://", priv->data->basedir, NULL);
	webkit_web_view_load_html_string (WEBKIT_WEB_VIEW (object),
					  priv->data->template_html,
					  basedir_uri);
	g_free (basedir_uri);
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
	EmpathyThemeAdiumPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ADIUM_DATA:
		g_assert (priv->data == NULL);
		priv->data = g_value_dup_boxed (value);
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

	g_type_class_add_private (object_class, sizeof (EmpathyThemeAdiumPriv));
}

static void
empathy_theme_adium_init (EmpathyThemeAdium *theme)
{
	EmpathyThemeAdiumPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (theme,
		EMPATHY_TYPE_THEME_ADIUM, EmpathyThemeAdiumPriv);

	theme->priv = priv;

	priv->smiley_manager = empathy_smiley_manager_dup_singleton ();

	g_signal_connect (theme, "load-finished",
			  G_CALLBACK (theme_adium_load_finished_cb),
			  NULL);
	g_signal_connect (theme, "navigation-policy-decision-requested",
			  G_CALLBACK (theme_adium_navigation_policy_decision_requested_cb),
			  NULL);

	priv->gsettings_chat = g_settings_new (EMPATHY_PREFS_CHAT_SCHEMA);
	g_signal_connect (priv->gsettings_chat,
		"changed::" EMPATHY_PREFS_CHAT_WEBKIT_DEVELOPER_TOOLS,
		G_CALLBACK (theme_adium_notify_enable_webkit_developer_tools_cb),
		theme);

	theme_adium_update_enable_webkit_developer_tools (theme);
}

EmpathyThemeAdium *
empathy_theme_adium_new (EmpathyAdiumData *data)
{
	g_return_val_if_fail (data != NULL, NULL);

	return g_object_new (EMPATHY_TYPE_THEME_ADIUM,
			     "adium-data", data,
			     NULL);
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

	if (ret == FALSE)
		return ret;

	/* We ship a default Template.html as fallback if there is any problem
	 * with the one inside the theme. The only other required file is
	 * Content.html */
	file = g_build_filename (path, "Contents", "Resources", "Content.html",
				 NULL);
	ret = g_file_test (file, G_FILE_TEST_EXISTS);
	g_free (file);

	if (ret)
		return ret;

	/* Legacy themes have Incoming/Content.html (outgoing fallback to use
	 * incoming). */
	file = g_build_filename (path, "Contents", "Resources", "Incoming",
				 "Content.html", NULL);
	ret = g_file_test (file, G_FILE_TEST_EXISTS);
	g_free (file);

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
	gchar            *file;
	gchar            *template_html = NULL;
	gsize             template_len;
	gchar            *footer_html = NULL;
	gsize             footer_len;
	GString          *string;
	gchar           **strv = NULL;
	gchar            *css_path;
	guint             len = 0;
	guint             i = 0;

	g_return_val_if_fail (empathy_adium_path_is_valid (path), NULL);

	data = g_slice_new0 (EmpathyAdiumData);
	data->ref_count = 1;
	data->path = g_strdup (path);
	data->basedir = g_strconcat (path, G_DIR_SEPARATOR_S "Contents"
		G_DIR_SEPARATOR_S "Resources" G_DIR_SEPARATOR_S, NULL);
	data->info = g_hash_table_ref (info);

	DEBUG ("Loading theme at %s", path);

	/* Load html files */
	file = g_build_filename (data->basedir, "Content.html", NULL);
	g_file_get_contents (file, &data->content_html, &data->content_len, NULL);
	g_free (file);

	/* Fallback to legacy html files */
	if (data->content_html == NULL) {
		DEBUG ("  fallback to legacy theme");

		file = g_build_filename (data->basedir, "Incoming", "Content.html", NULL);
		g_file_get_contents (file, &data->in_content_html, &data->in_content_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Incoming", "NextContent.html", NULL);
		g_file_get_contents (file, &data->in_nextcontent_html, &data->in_nextcontent_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Incoming", "Context.html", NULL);
		g_file_get_contents (file, &data->in_context_html, &data->in_context_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Incoming", "NextContext.html", NULL);
		g_file_get_contents (file, &data->in_nextcontext_html, &data->in_nextcontext_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Outgoing", "Content.html", NULL);
		g_file_get_contents (file, &data->out_content_html, &data->out_content_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Outgoing", "NextContent.html", NULL);
		g_file_get_contents (file, &data->out_nextcontent_html, &data->out_nextcontent_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Outgoing", "Context.html", NULL);
		g_file_get_contents (file, &data->out_context_html, &data->out_context_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Outgoing", "NextContext.html", NULL);
		g_file_get_contents (file, &data->out_nextcontext_html, &data->out_nextcontext_len, NULL);
		g_free (file);

		file = g_build_filename (data->basedir, "Status.html", NULL);
		g_file_get_contents (file, &data->status_html, &data->status_len, NULL);
		g_free (file);
	}

	file = g_build_filename (data->basedir, "Footer.html", NULL);
	g_file_get_contents (file, &footer_html, &footer_len, NULL);
	g_free (file);

	file = g_build_filename (data->basedir, "Incoming", "buddy_icon.png", NULL);
	if (g_file_test (file, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		data->default_incoming_avatar_filename = file;
	} else {
		g_free (file);
	}

	file = g_build_filename (data->basedir, "Outgoing", "buddy_icon.png", NULL);
	if (g_file_test (file, G_FILE_TEST_EXISTS | G_FILE_TEST_IS_REGULAR)) {
		data->default_outgoing_avatar_filename = file;
	} else {
		g_free (file);
	}

	css_path = g_build_filename (data->basedir, "main.css", NULL);

	/* There is 2 formats for Template.html: The old one has 4 parameters,
	 * the new one has 5 parameters. */
	file = g_build_filename (data->basedir, "Template.html", NULL);
	if (g_file_get_contents (file, &template_html, &template_len, NULL)) {
		strv = g_strsplit (template_html, "%@", -1);
		len = g_strv_length (strv);
	}
	g_free (file);

	if (len != 5 && len != 6) {
		/* Either the theme has no template or it don't have the good
		 * number of parameters. Fallback to use our own template. */
		g_free (template_html);
		g_strfreev (strv);

		file = empathy_file_lookup ("Template.html", "data");
		g_file_get_contents (file, &template_html, &template_len, NULL);
		g_free (file);
		strv = g_strsplit (template_html, "%@", -1);
		len = g_strv_length (strv);
	}

	/* Replace %@ with the needed information in the template html. */
	string = g_string_sized_new (template_len);
	g_string_append (string, strv[i++]);
	g_string_append (string, data->basedir);
	g_string_append (string, strv[i++]);
	if (len == 6) {
		const gchar *variant;

		/* We include main.css by default */
		g_string_append_printf (string, "@import url(\"%s\");", css_path);
		g_string_append (string, strv[i++]);
		variant = tp_asv_get_string (data->info, "DefaultVariant");
		if (variant) {
			g_string_append (string, "Variants/");
			g_string_append (string, variant);
			g_string_append (string, ".css");
		}
	} else {
		/* FIXME: We should set main.css OR the variant css */
		g_string_append (string, css_path);
	}
	g_string_append (string, strv[i++]);
	g_string_append (string, ""); /* We don't want header */
	g_string_append (string, strv[i++]);
	/* FIXME: We should replace adium %macros% in footer */
	if (footer_html) {
		g_string_append (string, footer_html);
	}
	g_string_append (string, strv[i++]);
	data->template_html = g_string_free (string, FALSE);

	g_free (footer_html);
	g_free (template_html);
	g_free (css_path);
	g_strfreev (strv);

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
		g_free (data->template_html);
		g_free (data->content_html);
		g_hash_table_unref (data->info);

		g_free (data->in_content_html);
		g_free (data->in_nextcontent_html);
		g_free (data->in_context_html);
		g_free (data->in_nextcontext_html);
		g_free (data->out_content_html);
		g_free (data->out_nextcontent_html);
		g_free (data->out_context_html);
		g_free (data->out_nextcontext_html);
		g_free (data->status_html);

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


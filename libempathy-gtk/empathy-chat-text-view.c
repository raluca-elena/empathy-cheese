/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2002-2007 Imendio AB
 * Copyright (C) 2008 Collabora Ltd.
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
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <sys/types.h>
#include <string.h>
#include <time.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-utils.h>

#include "empathy-chat-text-view.h"
#include "empathy-chat.h"
#include "empathy-ui-utils.h"
#include "empathy-smiley-manager.h"
#include "empathy-string-parser.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CHAT
#include <libempathy/empathy-debug.h>

/* Number of seconds between timestamps when using normal mode, 5 minutes. */
#define TIMESTAMP_INTERVAL (5 * G_TIME_SPAN_MINUTE)

#define MAX_LINES 800
#define MAX_SCROLL_TIME 0.4 /* seconds */
#define SCROLL_DELAY 33     /* milliseconds */

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyChatTextView)

typedef struct {
	GtkTextBuffer        *buffer;
	guint                 scroll_timeout;
	GTimer               *scroll_time;
	GtkTextMark          *find_mark_previous;
	GtkTextMark          *find_mark_next;
	gboolean              find_wrapped;
	gboolean              find_last_direction;
	EmpathyContact       *last_contact;
	gint64                last_timestamp;
	gboolean              allow_scrolling;
	guint                 notify_system_fonts_id;
	GSettings            *gsettings_desktop;
	GSettings            *gsettings_chat;
	EmpathySmileyManager *smiley_manager;
	gboolean              only_if_date;
} EmpathyChatTextViewPriv;

static void chat_text_view_iface_init (EmpathyChatViewIface *iface);

static void chat_text_view_copy_clipboard (EmpathyChatView *view);

G_DEFINE_TYPE_WITH_CODE (EmpathyChatTextView, empathy_chat_text_view,
			 GTK_TYPE_TEXT_VIEW,
			 G_IMPLEMENT_INTERFACE (EMPATHY_TYPE_CHAT_VIEW,
						chat_text_view_iface_init));

enum {
	PROP_0,
	PROP_LAST_CONTACT,
	PROP_ONLY_IF_DATE
};

static gboolean
chat_text_view_url_event_cb (GtkTextTag          *tag,
			     GObject             *object,
			     GdkEvent            *event,
			     GtkTextIter         *iter,
			     EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextIter              start, end;
	gchar                   *str;

	priv = GET_PRIV (view);

	/* If the link is being selected, don't do anything. */
	gtk_text_buffer_get_selection_bounds (priv->buffer, &start, &end);
	if (gtk_text_iter_get_offset (&start) != gtk_text_iter_get_offset (&end)) {
		return FALSE;
	}

	if (event->type == GDK_BUTTON_RELEASE && event->button.button == 1) {
		start = end = *iter;

		if (gtk_text_iter_backward_to_tag_toggle (&start, tag) &&
		    gtk_text_iter_forward_to_tag_toggle (&end, tag)) {
			    str = gtk_text_buffer_get_text (priv->buffer,
							    &start,
							    &end,
							    FALSE);

			    empathy_url_show (GTK_WIDGET (view), str);
			    g_free (str);
		    }
	}

	return FALSE;
}

static gboolean
chat_text_view_event_cb (EmpathyChatTextView *view,
			 GdkEventMotion      *event,
			 GtkTextTag          *tag)
{
	static GdkCursor  *hand = NULL;
	static GdkCursor  *beam = NULL;
	GtkTextWindowType  type;
	GtkTextIter        iter;
	GdkWindow         *win;
	gint               x, y, buf_x, buf_y;

	type = gtk_text_view_get_window_type (GTK_TEXT_VIEW (view),
					      event->window);

	if (type != GTK_TEXT_WINDOW_TEXT) {
		return FALSE;
	}

	/* Get where the pointer really is. */
	win = gtk_text_view_get_window (GTK_TEXT_VIEW (view), type);
	if (!win) {
		return FALSE;
	}

	gdk_window_get_device_position (win, event->device, &x, &y, NULL);

	/* Get the iter where the cursor is at */
	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view), type,
					       x, y,
					       &buf_x, &buf_y);

	gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view),
					    &iter,
					    buf_x, buf_y);

	if (gtk_text_iter_has_tag (&iter, tag)) {
		if (!hand) {
			hand = gdk_cursor_new (GDK_HAND2);
			beam = gdk_cursor_new (GDK_XTERM);
		}
		gdk_window_set_cursor (win, hand);
	} else {
		if (!beam) {
			beam = gdk_cursor_new (GDK_XTERM);
		}
		gdk_window_set_cursor (win, beam);
	}

	return FALSE;
}

static void
chat_text_view_create_tags (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	GtkTextTag              *tag;

	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_CUT, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_HIGHLIGHT, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_SPACING, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_TIME, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_ACTION, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_BODY, NULL);
	gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_EVENT, NULL);

	tag = gtk_text_buffer_create_tag (priv->buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_LINK, NULL);
	g_signal_connect (tag, "event",
			  G_CALLBACK (chat_text_view_url_event_cb),
			  view);

	g_signal_connect (view, "motion-notify-event",
			  G_CALLBACK (chat_text_view_event_cb),
			  tag);
}

static void
chat_text_view_system_font_update (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	PangoFontDescription *font_description = NULL;
	gchar                *font_name;

	font_name = g_settings_get_string (priv->gsettings_desktop,
			EMPATHY_PREFS_DESKTOP_INTERFACE_DOCUMENT_FONT_NAME);

	if (font_name != NULL) {
		font_description = pango_font_description_from_string (font_name);
		g_free (font_name);
	} else {
		font_description = NULL;
	}

	gtk_widget_override_font (GTK_WIDGET (view), font_description);

	if (font_description) {
		pango_font_description_free (font_description);
	}
}

static void
chat_text_view_notify_system_font_cb (GSettings *gsettings,
				      const gchar *key,
				      EmpathyChatTextView *self)
{
	chat_text_view_system_font_update (self);
}

static void
chat_text_view_open_address_cb (GtkMenuItem *menuitem, const gchar *url)
{
	empathy_url_show (GTK_WIDGET (menuitem), url);
}

static void
chat_text_view_copy_address_cb (GtkMenuItem *menuitem, const gchar *url)
{
	GtkClipboard *clipboard;

	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
	gtk_clipboard_set_text (clipboard, url, -1);

	clipboard = gtk_clipboard_get (GDK_SELECTION_PRIMARY);
	gtk_clipboard_set_text (clipboard, url, -1);
}

static void
chat_text_view_populate_popup (EmpathyChatTextView *view,
			       GtkMenu        *menu,
			       gpointer        user_data)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextTagTable    *table;
	GtkTextTag         *tag;
	gint                x, y;
	GtkTextIter         iter, start, end;
	GtkWidget          *item;
	gchar              *str = NULL;

	priv = GET_PRIV (view);

	/* Clear menu item */
	if (gtk_text_buffer_get_char_count (priv->buffer) > 0) {
		item = gtk_separator_menu_item_new ();
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);

		item = gtk_image_menu_item_new_from_stock (GTK_STOCK_CLEAR, NULL);
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
		gtk_widget_show (item);

		g_signal_connect_swapped (item, "activate",
					  G_CALLBACK (empathy_chat_view_clear),
					  view);
	}

	/* Link context menu items */
	table = gtk_text_buffer_get_tag_table (priv->buffer);
	tag = gtk_text_tag_table_lookup (table, EMPATHY_CHAT_TEXT_VIEW_TAG_LINK);

	gdk_window_get_device_position (gtk_widget_get_window (GTK_WIDGET (view)),
		gdk_device_manager_get_client_pointer (gdk_display_get_device_manager (
			gtk_widget_get_display (GTK_WIDGET (view)))), &x, &y, NULL);

	gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW (view),
					       GTK_TEXT_WINDOW_WIDGET,
					       x, y,
					       &x, &y);

	gtk_text_view_get_iter_at_location (GTK_TEXT_VIEW (view), &iter, x, y);

	start = end = iter;

	if (gtk_text_iter_backward_to_tag_toggle (&start, tag) &&
	    gtk_text_iter_forward_to_tag_toggle (&end, tag)) {
		    str = gtk_text_buffer_get_text (priv->buffer,
						    &start, &end, FALSE);
	    }

	if (EMP_STR_EMPTY (str)) {
		g_free (str);
		return;
	}

	/* NOTE: Set data just to get the string freed when not needed. */
	g_object_set_data_full (G_OBJECT (menu),
				"url", str,
				(GDestroyNotify) g_free);

	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_menu_item_new_with_mnemonic (_("_Copy Link Address"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (chat_text_view_copy_address_cb),
			  str);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);

	item = gtk_menu_item_new_with_mnemonic (_("_Open Link"));
	g_signal_connect (item, "activate",
			  G_CALLBACK (chat_text_view_open_address_cb),
			  str);
	gtk_menu_shell_prepend (GTK_MENU_SHELL (menu), item);
	gtk_widget_show (item);
}

static gboolean
chat_text_view_is_scrolled_down (EmpathyChatTextView *view)
{
	GtkAdjustment *vadj;
	gdouble value;
	gdouble upper;
	gdouble page_size;

	vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view));
	value = gtk_adjustment_get_value (vadj);
	upper = gtk_adjustment_get_upper (vadj);
	page_size = gtk_adjustment_get_page_size (vadj);

	if (value < upper - page_size) {
		return FALSE;
	}

	return TRUE;
}

static void
chat_text_view_maybe_trim_buffer (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextIter         top, bottom;
	gint                line;
	gint                remove_;
	GtkTextTagTable    *table;
	GtkTextTag         *tag;

	priv = GET_PRIV (view);

	gtk_text_buffer_get_end_iter (priv->buffer, &bottom);
	line = gtk_text_iter_get_line (&bottom);
	if (line < MAX_LINES) {
		return;
	}

	remove_ = line - MAX_LINES;
	gtk_text_buffer_get_start_iter (priv->buffer, &top);

	bottom = top;
	if (!gtk_text_iter_forward_lines (&bottom, remove_)) {
		return;
	}

	/* Track backwords to a place where we can safely cut, we don't do it in
	  * the middle of a tag.
	  */
	table = gtk_text_buffer_get_tag_table (priv->buffer);
	tag = gtk_text_tag_table_lookup (table, EMPATHY_CHAT_TEXT_VIEW_TAG_CUT);
	if (!tag) {
		return;
	}

	if (!gtk_text_iter_forward_to_tag_toggle (&bottom, tag)) {
		return;
	}

	if (!gtk_text_iter_equal (&top, &bottom)) {
		gtk_text_buffer_delete (priv->buffer, &top, &bottom);
	}
}

static void
chat_text_view_append_timestamp (EmpathyChatTextView *view,
				 gint64               timestamp,
				 gboolean             show_date)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	GtkTextIter              iter;
	gchar                   *tmp;
	GString                 *str;

	str = g_string_new ("- ");

	/* Append date if needed */
	if (show_date) {
		/* Translators: timestamp displayed between conversations in
		 * chat windows (strftime format string) */
		tmp = empathy_time_to_string_utc (timestamp, _("%A %B %d %Y"));
		g_string_append (str, tmp);
		g_string_append (str, ", ");
		g_free (tmp);
	}

	/* Append time */
	tmp = empathy_time_to_string_local (timestamp, EMPATHY_TIME_FORMAT_DISPLAY_SHORT);
	g_string_append (str, tmp);
	g_free (tmp);

	g_string_append (str, " -\n");

	/* Insert the string in the buffer */
	empathy_chat_text_view_append_spacing (view);
	gtk_text_buffer_get_end_iter (priv->buffer, &iter);
	gtk_text_buffer_insert_with_tags_by_name (priv->buffer,
						  &iter,
						  str->str, -1,
						  EMPATHY_CHAT_TEXT_VIEW_TAG_TIME,
						  NULL);

	g_string_free (str, TRUE);
}

static void
chat_text_maybe_append_date_and_time (EmpathyChatTextView *view,
				      gint64               timestamp)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	GDateTime               *date, *last_date;
	gboolean                 append_date = FALSE;
	gboolean                 append_time = FALSE;
	GTimeSpan                delta;

	/* Get the date from last message */
	last_date = g_date_time_new_from_unix_utc (priv->last_timestamp);

	/* Get the date of the message we are appending */
	date = g_date_time_new_from_unix_utc (timestamp);

	delta = g_date_time_difference (date, last_date);
	/* If last message was from another day we append date and time */
	if (delta >= G_TIME_SPAN_DAY) {
		append_date = TRUE;
		append_time = TRUE;
	}

	g_date_time_unref (last_date);
	g_date_time_unref (date);

	/* If last message is 'old' append the time */
	if (delta >= TIMESTAMP_INTERVAL) {
		append_time = TRUE;
	}

	if (append_date || (!priv->only_if_date && append_time)) {
		chat_text_view_append_timestamp (view, timestamp, append_date);
	}
}

static void
chat_text_view_size_allocate (GtkWidget     *widget,
			      GtkAllocation *alloc)
{
	gboolean down;

	down = chat_text_view_is_scrolled_down (EMPATHY_CHAT_TEXT_VIEW (widget));

	GTK_WIDGET_CLASS (empathy_chat_text_view_parent_class)->size_allocate (widget, alloc);

	if (down) {
		GtkAdjustment *adj;

		adj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (widget));
		gtk_adjustment_set_value (adj,
					  gtk_adjustment_get_upper (adj) -
					  gtk_adjustment_get_page_size (adj));
	}
}

static gboolean
chat_text_view_drag_motion (GtkWidget      *widget,
			    GdkDragContext *context,
			    gint            x,
			    gint            y,
			    guint           time_)
{
	/* Don't handle drag motion, since we don't want the view to scroll as
	 * the result of dragging something across it. */

	return FALSE;
}

static void
chat_text_view_get_property (GObject    *object,
			     guint       param_id,
			     GValue     *value,
			     GParamSpec *pspec)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_LAST_CONTACT:
		g_value_set_object (value, priv->last_contact);
		break;
	case PROP_ONLY_IF_DATE:
		g_value_set_boolean (value, priv->only_if_date);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
chat_text_view_set_property (GObject      *object,
			     guint         param_id,
			     const GValue *value,
			     GParamSpec   *pspec)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (object);

	switch (param_id) {
	case PROP_ONLY_IF_DATE:
		priv->only_if_date = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	};
}

static void
chat_text_view_finalize (GObject *object)
{
	EmpathyChatTextView     *view;
	EmpathyChatTextViewPriv *priv;

	view = EMPATHY_CHAT_TEXT_VIEW (object);
	priv = GET_PRIV (view);

	DEBUG ("%p", object);

	g_object_unref (priv->gsettings_chat);
	g_object_unref (priv->gsettings_desktop);

	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
	}
	if (priv->scroll_time) {
		g_timer_destroy (priv->scroll_time);
	}
	if (priv->scroll_timeout) {
		g_source_remove (priv->scroll_timeout);
	}
	g_object_unref (priv->smiley_manager);

	G_OBJECT_CLASS (empathy_chat_text_view_parent_class)->finalize (object);
}

static void
text_view_copy_clipboard (GtkTextView *text_view)
{
	chat_text_view_copy_clipboard (EMPATHY_CHAT_VIEW (text_view));
}

static void
empathy_chat_text_view_class_init (EmpathyChatTextViewClass *klass)
{
	GObjectClass   *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
	GtkTextViewClass *text_view_class = GTK_TEXT_VIEW_CLASS (klass);

	object_class->finalize = chat_text_view_finalize;
	object_class->get_property = chat_text_view_get_property;
	object_class->set_property = chat_text_view_set_property;

	widget_class->size_allocate = chat_text_view_size_allocate;
	widget_class->drag_motion = chat_text_view_drag_motion;

	text_view_class->copy_clipboard = text_view_copy_clipboard;

	g_object_class_install_property (object_class,
					 PROP_LAST_CONTACT,
					 g_param_spec_object ("last-contact",
							      "Last contact",
							      "The sender of the last received message",
							      EMPATHY_TYPE_CONTACT,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_ONLY_IF_DATE,
					 g_param_spec_boolean ("only-if-date",
							      "Only if date",
							      "Display timestamp only if the date changes",
							      FALSE,
							      G_PARAM_READWRITE));


	g_type_class_add_private (object_class, sizeof (EmpathyChatTextViewPriv));
}

static void
empathy_chat_text_view_init (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (view,
		EMPATHY_TYPE_CHAT_TEXT_VIEW, EmpathyChatTextViewPriv);

	view->priv = priv;
	priv->buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	priv->last_timestamp = 0;
	priv->allow_scrolling = TRUE;
	priv->smiley_manager = empathy_smiley_manager_dup_singleton ();

	g_object_set (view,
		      "wrap-mode", GTK_WRAP_WORD_CHAR,
		      "editable", FALSE,
		      "cursor-visible", FALSE,
		      NULL);

	priv->gsettings_chat = g_settings_new (EMPATHY_PREFS_CHAT_SCHEMA);

	priv->gsettings_desktop = g_settings_new (
			  EMPATHY_PREFS_DESKTOP_INTERFACE_SCHEMA);
	g_signal_connect (priv->gsettings_desktop,
			  "changed::" EMPATHY_PREFS_DESKTOP_INTERFACE_DOCUMENT_FONT_NAME,
			  G_CALLBACK (chat_text_view_notify_system_font_cb),
			  view);
	chat_text_view_system_font_update (view);
	chat_text_view_create_tags (view);

	g_signal_connect (view,
			  "populate-popup",
			  G_CALLBACK (chat_text_view_populate_popup),
			  NULL);
}

static void
chat_text_view_scroll_stop (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_timer_destroy (priv->scroll_time);
	priv->scroll_time = NULL;

	g_source_remove (priv->scroll_timeout);
	priv->scroll_timeout = 0;
}

/* Code stolen from pidgin/gtkimhtml.c */
static gboolean
chat_text_view_scroll_cb (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv;
	GtkAdjustment      *adj;
	gdouble             max_val;

	priv = GET_PRIV (view);

	adj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (view));
	max_val = gtk_adjustment_get_upper (adj) - gtk_adjustment_get_page_size (adj);

	g_return_val_if_fail (priv->scroll_time != NULL, FALSE);

	if (g_timer_elapsed (priv->scroll_time, NULL) > MAX_SCROLL_TIME) {
		/* time's up. jump to the end and kill the timer */
		gtk_adjustment_set_value (adj, max_val);
		chat_text_view_scroll_stop (view);
		return FALSE;
	}

	/* scroll by 1/3rd the remaining distance */
	gtk_adjustment_set_value (adj, gtk_adjustment_get_value (adj) + ((max_val - gtk_adjustment_get_value (adj)) / 3));
	return TRUE;
}

static void
chat_text_view_scroll_down (EmpathyChatView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	if (!priv->allow_scrolling) {
		return;
	}

	DEBUG ("Scrolling down");

	if (priv->scroll_time) {
		g_timer_reset (priv->scroll_time);
	} else {
		priv->scroll_time = g_timer_new ();
	}
	if (!priv->scroll_timeout) {
		priv->scroll_timeout = g_timeout_add (SCROLL_DELAY,
						      (GSourceFunc) chat_text_view_scroll_cb,
						      view);
	}
}

static void
chat_text_view_append_message (EmpathyChatView *view,
			       EmpathyMessage  *msg)
{
	EmpathyChatTextView     *text_view = EMPATHY_CHAT_TEXT_VIEW (view);
	EmpathyChatTextViewPriv *priv = GET_PRIV (text_view);
	gboolean                 bottom;
	gint64                   timestamp;

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));
	g_return_if_fail (EMPATHY_IS_MESSAGE (msg));

	if (!empathy_message_get_body (msg)) {
		return;
	}

	bottom = chat_text_view_is_scrolled_down (text_view);

	chat_text_view_maybe_trim_buffer (EMPATHY_CHAT_TEXT_VIEW (view));

	timestamp = empathy_message_get_timestamp (msg);
	chat_text_maybe_append_date_and_time (text_view, timestamp);
	if (EMPATHY_CHAT_TEXT_VIEW_GET_CLASS (view)->append_message) {
		EMPATHY_CHAT_TEXT_VIEW_GET_CLASS (view)->append_message (text_view,
									 msg);
	}

	if (bottom) {
		chat_text_view_scroll_down (view);
	}

	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
	}
	priv->last_contact = g_object_ref (empathy_message_get_sender (msg));
	g_object_notify (G_OBJECT (view), "last-contact");

	priv->last_timestamp = timestamp;
}

static void
chat_text_view_append_event (EmpathyChatView *view,
			     const gchar     *str)
{
	EmpathyChatTextView     *text_view = EMPATHY_CHAT_TEXT_VIEW (view);
	EmpathyChatTextViewPriv *priv = GET_PRIV (text_view);
	gboolean                 bottom;
	GtkTextIter              iter;
	gchar                   *msg;


	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));
	g_return_if_fail (!EMP_STR_EMPTY (str));

	bottom = chat_text_view_is_scrolled_down (text_view);
	chat_text_view_maybe_trim_buffer (EMPATHY_CHAT_TEXT_VIEW (view));
	chat_text_maybe_append_date_and_time (text_view,
					      empathy_time_get_current ());

	gtk_text_buffer_get_end_iter (priv->buffer, &iter);
	msg = g_strdup_printf (" - %s\n", str);
	gtk_text_buffer_insert_with_tags_by_name (priv->buffer, &iter,
						  msg, -1,
						  EMPATHY_CHAT_TEXT_VIEW_TAG_EVENT,
						  NULL);
	g_free (msg);

	if (bottom) {
		chat_text_view_scroll_down (view);
	}

	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
		g_object_notify (G_OBJECT (view), "last-contact");
	}
}

static void
chat_text_view_scroll (EmpathyChatView *view,
		       gboolean         allow_scrolling)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	DEBUG ("Scrolling %s", allow_scrolling ? "enabled" : "disabled");

	priv->allow_scrolling = allow_scrolling;
	if (allow_scrolling) {
		empathy_chat_view_scroll_down (view);
	}
}

static gboolean
chat_text_view_get_has_selection (EmpathyChatView *view)
{
	GtkTextBuffer *buffer;

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), FALSE);

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

	return gtk_text_buffer_get_has_selection (buffer);
}

static void
chat_text_view_clear (EmpathyChatView *view)
{
	GtkTextBuffer      *buffer;
	EmpathyChatTextViewPriv *priv;

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	gtk_text_buffer_set_text (buffer, "", -1);

	/* We set these back to the initial values so we get
	  * timestamps when clearing the window to know when
	  * conversations start.
	  */
	priv = GET_PRIV (view);

	priv->last_timestamp = 0;
	if (priv->last_contact) {
		g_object_unref (priv->last_contact);
		priv->last_contact = NULL;
	}
}

static gboolean
chat_text_view_find_previous (EmpathyChatView *view,
				const gchar     *search_criteria,
				gboolean         new_search,
				gboolean         match_case)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextBuffer      *buffer;
	GtkTextIter         iter_at_mark;
	GtkTextIter         iter_match_start;
	GtkTextIter         iter_match_end;
	gboolean            found;
	gboolean            from_start = FALSE;

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), FALSE);
	g_return_val_if_fail (search_criteria != NULL, FALSE);

	priv = GET_PRIV (view);

	buffer = priv->buffer;

	if (EMP_STR_EMPTY (search_criteria)) {
		if (priv->find_mark_previous) {
			gtk_text_buffer_get_start_iter (buffer, &iter_at_mark);

			gtk_text_buffer_move_mark (buffer,
						   priv->find_mark_previous,
						   &iter_at_mark);
			gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (view),
						      priv->find_mark_previous,
						      0.0,
						      TRUE,
						      0.0,
						      0.0);
			gtk_text_buffer_select_range (buffer,
						      &iter_at_mark,
						      &iter_at_mark);
		}

		return FALSE;
	}

	if (new_search) {
		from_start = TRUE;
	}

	if (!new_search && priv->find_mark_previous) {
		gtk_text_buffer_get_iter_at_mark (buffer,
						  &iter_at_mark,
						  priv->find_mark_previous);
	} else {
		gtk_text_buffer_get_end_iter (buffer, &iter_at_mark);
		from_start = TRUE;
	}

	priv->find_last_direction = FALSE;

	/* Use the standard GTK+ method for case sensitive searches. It can't do
	 * case insensitive searches (see bug #61852), so keep the custom method
	 * around for case insensitive searches. */
	if (match_case) {
		found = gtk_text_iter_backward_search (&iter_at_mark,
		                                       search_criteria,
		                                       0, /* no text search flags, we want exact matches */
		                                       &iter_match_start,
		                                       &iter_match_end,
		                                       NULL);
	} else {
		found = empathy_text_iter_backward_search (&iter_at_mark,
		                                           search_criteria,
		                                           &iter_match_start,
		                                           &iter_match_end,
		                                           NULL);
	}

	if (!found) {
		gboolean result = FALSE;

		if (from_start) {
			return result;
		}

		/* Here we wrap around. */
		if (!new_search && !priv->find_wrapped) {
			priv->find_wrapped = TRUE;
			result = chat_text_view_find_previous (view,
								 search_criteria,
								 FALSE,
								 match_case);
			priv->find_wrapped = FALSE;
		}

		return result;
	}

	/* Set new mark and show on screen */
	if (!priv->find_mark_previous) {
		priv->find_mark_previous = gtk_text_buffer_create_mark (buffer, NULL,
									&iter_match_start,
									TRUE);
	} else {
		gtk_text_buffer_move_mark (buffer,
					   priv->find_mark_previous,
					   &iter_match_start);
	}

	if (!priv->find_mark_next) {
		priv->find_mark_next = gtk_text_buffer_create_mark (buffer, NULL,
								    &iter_match_end,
								    TRUE);
	} else {
		gtk_text_buffer_move_mark (buffer,
					   priv->find_mark_next,
					   &iter_match_end);
	}

	gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (view),
				      priv->find_mark_previous,
				      0.0,
				      TRUE,
				      0.5,
				      0.5);

	gtk_text_buffer_move_mark_by_name (buffer, "selection_bound", &iter_match_start);
	gtk_text_buffer_move_mark_by_name (buffer, "insert", &iter_match_end);

	return TRUE;
}

static gboolean
chat_text_view_find_next (EmpathyChatView *view,
			    const gchar     *search_criteria,
			    gboolean         new_search,
			    gboolean         match_case)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextBuffer      *buffer;
	GtkTextIter         iter_at_mark;
	GtkTextIter         iter_match_start;
	GtkTextIter         iter_match_end;
	gboolean            found;
	gboolean            from_start = FALSE;

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), FALSE);
	g_return_val_if_fail (search_criteria != NULL, FALSE);

	priv = GET_PRIV (view);

	buffer = priv->buffer;

	if (EMP_STR_EMPTY (search_criteria)) {
		if (priv->find_mark_next) {
			gtk_text_buffer_get_start_iter (buffer, &iter_at_mark);

			gtk_text_buffer_move_mark (buffer,
						   priv->find_mark_next,
						   &iter_at_mark);
			gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (view),
						      priv->find_mark_next,
						      0.0,
						      TRUE,
						      0.0,
						      0.0);
			gtk_text_buffer_select_range (buffer,
						      &iter_at_mark,
						      &iter_at_mark);
		}

		return FALSE;
	}

	if (new_search) {
		from_start = TRUE;
	}

	if (!new_search && priv->find_mark_next) {
		gtk_text_buffer_get_iter_at_mark (buffer,
						  &iter_at_mark,
						  priv->find_mark_next);
	} else {
		gtk_text_buffer_get_start_iter (buffer, &iter_at_mark);
		from_start = TRUE;
	}

	priv->find_last_direction = TRUE;

	/* Use the standard GTK+ method for case sensitive searches. It can't do
	 * case insensitive searches (see bug #61852), so keep the custom method
	 * around for case insensitive searches. */
	if (match_case) {
		found = gtk_text_iter_forward_search (&iter_at_mark,
		                                      search_criteria,
		                                      0,
		                                      &iter_match_start,
		                                      &iter_match_end,
		                                      NULL);
	} else {
		found = empathy_text_iter_forward_search (&iter_at_mark,
		                                          search_criteria,
		                                          &iter_match_start,
		                                          &iter_match_end,
		                                          NULL);
	}

	if (!found) {
		gboolean result = FALSE;

		if (from_start) {
			return result;
		}

		/* Here we wrap around. */
		if (!new_search && !priv->find_wrapped) {
			priv->find_wrapped = TRUE;
			result = chat_text_view_find_next (view,
							     search_criteria,
							     FALSE,
							     match_case);
			priv->find_wrapped = FALSE;
		}

		return result;
	}

	/* Set new mark and show on screen */
	if (!priv->find_mark_next) {
		priv->find_mark_next = gtk_text_buffer_create_mark (buffer, NULL,
								    &iter_match_end,
								    TRUE);
	} else {
		gtk_text_buffer_move_mark (buffer,
					   priv->find_mark_next,
					   &iter_match_end);
	}

	if (!priv->find_mark_previous) {
		priv->find_mark_previous = gtk_text_buffer_create_mark (buffer, NULL,
									&iter_match_start,
									TRUE);
	} else {
		gtk_text_buffer_move_mark (buffer,
					   priv->find_mark_previous,
					   &iter_match_start);
	}

	gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW (view),
				      priv->find_mark_next,
				      0.0,
				      TRUE,
				      0.5,
				      0.5);

	gtk_text_buffer_move_mark_by_name (buffer, "selection_bound", &iter_match_start);
	gtk_text_buffer_move_mark_by_name (buffer, "insert", &iter_match_end);

	return TRUE;
}

static void
chat_text_view_find_abilities (EmpathyChatView *view,
				 const gchar    *search_criteria,
				 gboolean        match_case,
				 gboolean       *can_do_previous,
				 gboolean       *can_do_next)
{
	EmpathyChatTextViewPriv *priv;
	GtkTextBuffer           *buffer;
	GtkTextIter              iter_at_mark;
	GtkTextIter              iter_match_start;
	GtkTextIter              iter_match_end;

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));
	g_return_if_fail (search_criteria != NULL);
	g_return_if_fail (can_do_previous != NULL && can_do_next != NULL);

	priv = GET_PRIV (view);

	buffer = priv->buffer;

	if (can_do_previous) {
		if (priv->find_mark_previous) {
			gtk_text_buffer_get_iter_at_mark (buffer,
							  &iter_at_mark,
							  priv->find_mark_previous);
		} else {
			gtk_text_buffer_get_start_iter (buffer, &iter_at_mark);
		}

		if (match_case) {
			*can_do_previous = gtk_text_iter_backward_search (&iter_at_mark,
								          search_criteria,
								          0,
								          &iter_match_start,
								          &iter_match_end,
								          NULL);
		} else {
			*can_do_previous = empathy_text_iter_backward_search (&iter_at_mark,
									      search_criteria,
									      &iter_match_start,
									      &iter_match_end,
									      NULL);
		}
	}

	if (can_do_next) {
		if (priv->find_mark_next) {
			gtk_text_buffer_get_iter_at_mark (buffer,
							  &iter_at_mark,
							  priv->find_mark_next);
		} else {
			gtk_text_buffer_get_start_iter (buffer, &iter_at_mark);
		}

		if (match_case) {
			*can_do_next = gtk_text_iter_forward_search (&iter_at_mark,
								     search_criteria,
								     0,
								     &iter_match_start,
								     &iter_match_end,
								     NULL);
		} else {
			*can_do_next = empathy_text_iter_forward_search (&iter_at_mark,
									 search_criteria,
									 &iter_match_start,
									 &iter_match_end,
									 NULL);
		}
	}
}

static void
chat_text_view_highlight (EmpathyChatView *view,
			    const gchar     *text,
			    gboolean         match_case)
{
	GtkTextBuffer *buffer;
	GtkTextIter    iter;
	GtkTextIter    iter_start;
	GtkTextIter    iter_end;
	GtkTextIter    iter_match_start;
	GtkTextIter    iter_match_end;
	gboolean       found;

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));

	gtk_text_buffer_get_start_iter (buffer, &iter);

	gtk_text_buffer_get_bounds (buffer, &iter_start, &iter_end);
	gtk_text_buffer_remove_tag_by_name (buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_HIGHLIGHT,
					    &iter_start,
					    &iter_end);

	if (EMP_STR_EMPTY (text)) {
		return;
	}

	while (1) {
		if (match_case) {
			found = gtk_text_iter_forward_search (&iter,
							      text,
							      0,
							      &iter_match_start,
							      &iter_match_end,
							      NULL);
		} else {
			found = empathy_text_iter_forward_search (&iter,
								  text,
								  &iter_match_start,
								  &iter_match_end,
								  NULL);
		}
		if (!found) {
			break;
		}

		gtk_text_buffer_apply_tag_by_name (buffer, EMPATHY_CHAT_TEXT_VIEW_TAG_HIGHLIGHT,
						   &iter_match_start,
						   &iter_match_end);

		iter = iter_match_end;
	}
}

static void
chat_text_view_copy_clipboard (EmpathyChatView *view)
{
	GtkTextBuffer *buffer;
	GtkTextIter start, iter, end;
	GtkClipboard  *clipboard;
	GdkPixbuf *pixbuf;
	gunichar c;
	GtkTextChildAnchor *anchor = NULL;
	GString *str;
	GList *list;
	gboolean ignore_newlines = FALSE;

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
	clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

	if (!gtk_text_buffer_get_selection_bounds (buffer, &start, &end))
		return;

	str = g_string_new ("");

	for (iter = start; !gtk_text_iter_equal (&iter, &end); gtk_text_iter_forward_char (&iter)) {
		c = gtk_text_iter_get_char (&iter);
		/* 0xFFFC is the 'object replacement' unicode character,
		 * it indicates the presence of a pixbuf or a widget. */
		if (c == 0xFFFC) {
			ignore_newlines = FALSE;
			if ((pixbuf = gtk_text_iter_get_pixbuf (&iter))) {
				gchar *text;
				text = g_object_get_data (G_OBJECT(pixbuf),
							  "smiley_str");
				if (text)
					str = g_string_append (str, text);
			} else if ((anchor = gtk_text_iter_get_child_anchor (&iter))) {
				gchar *text;
				list = gtk_text_child_anchor_get_widgets (anchor);
				if (list) {
					text = g_object_get_data (G_OBJECT(list->data),
								  "str_obj");
					if (text)
						str = g_string_append (str, text);
				}
				g_list_free (list);
			}
		} else if (c == '\n') {
			if (!ignore_newlines) {
				ignore_newlines = TRUE;
				str = g_string_append_unichar (str, c);
			}
		} else {
			ignore_newlines = FALSE;
			str = g_string_append_unichar (str, c);
		}
	}

	gtk_clipboard_set_text (clipboard, str->str, str->len);
	g_string_free (str, TRUE);
}

static void
chat_text_view_iface_init (EmpathyChatViewIface *iface)
{
	iface->append_message = chat_text_view_append_message;
	iface->append_event = chat_text_view_append_event;
	iface->scroll = chat_text_view_scroll;
	iface->scroll_down = chat_text_view_scroll_down;
	iface->get_has_selection = chat_text_view_get_has_selection;
	iface->clear = chat_text_view_clear;
	iface->find_previous = chat_text_view_find_previous;
	iface->find_next = chat_text_view_find_next;
	iface->find_abilities = chat_text_view_find_abilities;
	iface->highlight = chat_text_view_highlight;
	iface->copy_clipboard = chat_text_view_copy_clipboard;
}

EmpathyContact *
empathy_chat_text_view_get_last_contact (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), NULL);

	return priv->last_contact;
}

gint64
empathy_chat_text_view_get_last_timestamp (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), 0);

	return priv->last_timestamp;
}

void
empathy_chat_text_view_set_only_if_date (EmpathyChatTextView *view,
					 gboolean             only_if_date)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);

	g_return_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view));

	if (only_if_date != priv->only_if_date) {
		priv->only_if_date = only_if_date;
		g_object_notify (G_OBJECT (view), "only-if-date");
	}
}

static void
chat_text_view_replace_link (const gchar *text,
			     gssize len,
			     gpointer match_data,
			     gpointer user_data)
{
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER (user_data);
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert_with_tags_by_name (buffer, &iter,
						  text, len,
						  EMPATHY_CHAT_TEXT_VIEW_TAG_LINK,
						  NULL);
}

static void
chat_text_view_replace_smiley (const gchar *text,
			       gssize len,
			       gpointer match_data,
			       gpointer user_data)
{
	EmpathySmileyHit *hit = match_data;
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER (user_data);
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert_pixbuf (buffer, &iter, hit->pixbuf);
}

static void
chat_text_view_replace_verbatim (const gchar *text,
				 gssize len,
				 gpointer match_data,
				 gpointer user_data)
{
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER (user_data);
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter (buffer, &iter);
	gtk_text_buffer_insert (buffer, &iter, text, len);
}

static EmpathyStringParser string_parsers[] = {
	{empathy_string_match_link, chat_text_view_replace_link},
	{empathy_string_match_all, chat_text_view_replace_verbatim},
	{NULL, NULL}
};

static EmpathyStringParser string_parsers_with_smiley[] = {
	{empathy_string_match_link, chat_text_view_replace_link},
	{empathy_string_match_smiley, chat_text_view_replace_smiley},
	{empathy_string_match_all, chat_text_view_replace_verbatim},
	{NULL, NULL}
};

void
empathy_chat_text_view_append_body (EmpathyChatTextView *view,
				    const gchar         *body,
				    const gchar         *tag)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	EmpathyStringParser     *parsers;
	gboolean                 use_smileys;
	GtkTextIter              start_iter;
	GtkTextIter              iter;
	GtkTextMark             *mark;

	/* Check if we have to parse smileys */
	use_smileys = g_settings_get_boolean (priv->gsettings_chat,
			EMPATHY_PREFS_CHAT_SHOW_SMILEYS);

	if (use_smileys)
		parsers = string_parsers_with_smiley;
	else
		parsers = string_parsers;

	/* Create a mark at the place we'll start inserting */
	gtk_text_buffer_get_end_iter (priv->buffer, &start_iter);
	mark = gtk_text_buffer_create_mark (priv->buffer, NULL, &start_iter, TRUE);

	/* Parse text for links/smileys and insert in the buffer */
	empathy_string_parser_substr (body, -1, parsers, priv->buffer);

	/* Insert a newline after the text inserted */
	gtk_text_buffer_get_end_iter (priv->buffer, &iter);
	gtk_text_buffer_insert (priv->buffer, &iter, "\n", 1);

	/* Apply the style to the inserted text. */
	gtk_text_buffer_get_iter_at_mark (priv->buffer, &start_iter, mark);
	gtk_text_buffer_get_end_iter (priv->buffer, &iter);
	gtk_text_buffer_apply_tag_by_name (priv->buffer, tag,
					   &start_iter,
					   &iter);

	gtk_text_buffer_delete_mark (priv->buffer, mark);
}

void
empathy_chat_text_view_append_spacing (EmpathyChatTextView *view)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	GtkTextIter              iter;

	gtk_text_buffer_get_end_iter (priv->buffer, &iter);
	gtk_text_buffer_insert_with_tags_by_name (priv->buffer,
						  &iter,
						  "\n",
						  -1,
						  EMPATHY_CHAT_TEXT_VIEW_TAG_CUT,
						  EMPATHY_CHAT_TEXT_VIEW_TAG_SPACING,
						  NULL);
}

GtkTextTag *
empathy_chat_text_view_tag_set (EmpathyChatTextView *view,
				const gchar         *tag_name,
				const gchar         *first_property_name,
				...)
{
	EmpathyChatTextViewPriv *priv = GET_PRIV (view);
	GtkTextTag              *tag;
	GtkTextTagTable         *table;
	va_list                  list;

	g_return_val_if_fail (EMPATHY_IS_CHAT_TEXT_VIEW (view), NULL);
	g_return_val_if_fail (tag_name != NULL, NULL);

	table = gtk_text_buffer_get_tag_table (priv->buffer);
	tag = gtk_text_tag_table_lookup (table, tag_name);

	if (tag && first_property_name) {
		va_start (list, first_property_name);
		g_object_set_valist (G_OBJECT (tag), first_property_name, list);
		va_end (list);
	}

	return tag;
}


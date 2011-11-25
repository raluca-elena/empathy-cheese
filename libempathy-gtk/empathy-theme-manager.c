/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005-2007 Imendio AB
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
 * Authors: Xavier Claessens <xclaesse@gmail.com>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <telepathy-glib/dbus.h>
#include <gtk/gtk.h>

#include <telepathy-glib/util.h>

#include <libempathy/empathy-gsettings.h>
#include <libempathy/empathy-utils.h>

#include "empathy-theme-manager.h"
#include "empathy-chat-view.h"
#include "empathy-chat-text-view.h"
#include "empathy-theme-boxes.h"
#include "empathy-theme-irc.h"
#include "empathy-theme-adium.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyThemeManager)
typedef struct {
	GSettings   *gsettings_chat;
	gchar       *name;
	GtkSettings *settings;
	GList       *boxes_views;
	guint        emit_changed_idle;
	gboolean     in_constructor;

	EmpathyAdiumData *adium_data;
	gchar *adium_variant;
	/* list of weakref to EmpathyThemeAdium objects */
	GList *adium_views;
} EmpathyThemeManagerPriv;

enum {
	THEME_CHANGED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static const gchar *themes[] = {
	"classic", N_("Classic"),
	"simple", N_("Simple"),
	"clean", N_("Clean"),
	"blue", N_("Blue"),
	NULL
};

G_DEFINE_TYPE (EmpathyThemeManager, empathy_theme_manager, G_TYPE_OBJECT);

static gboolean
theme_manager_emit_changed_idle_cb (gpointer manager)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	const gchar *adium_path = NULL;

	if (priv->adium_data) {
		adium_path = empathy_adium_data_get_path (priv->adium_data);
	}
	DEBUG ("Emit theme-changed with: name='%s' adium_path='%s' "
	       "adium_variant='%s'", priv->name, adium_path,
	       priv->adium_variant);

	g_signal_emit (manager, signals[THEME_CHANGED], 0, NULL);
	priv->emit_changed_idle = 0;

	return FALSE;
}

static void
theme_manager_emit_changed (EmpathyThemeManager *manager)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);

	/* We emit the signal in idle callback to be sure we emit it only once
	 * in the case both the name and adium_path changed */
	if (priv->emit_changed_idle == 0 && !priv->in_constructor) {
		priv->emit_changed_idle = g_idle_add (
			theme_manager_emit_changed_idle_cb, manager);
	}
}

static void
theme_manager_view_weak_notify_cb (gpointer data,
				    GObject *where_the_object_was)
{
	GList **list = data;
	*list = g_list_remove (*list, where_the_object_was);
}

static void
clear_list_of_views (GList **views)
{
	while (*views) {
		g_object_weak_unref ((*views)->data,
				     theme_manager_view_weak_notify_cb,
				     views);
		*views = g_list_delete_link (*views, *views);
	}
}

static void
theme_manager_gdk_color_to_hex (GdkColor *gdk_color, gchar *str_color)
{
	g_snprintf (str_color, 10,
		    "#%02x%02x%02x",
		    gdk_color->red >> 8,
		    gdk_color->green >> 8,
		    gdk_color->blue >> 8);
}

static EmpathyThemeIrc *
theme_manager_create_irc_view (EmpathyThemeManager *manager)
{
	EmpathyChatTextView *view;
	EmpathyThemeIrc     *theme;

	theme = empathy_theme_irc_new ();
	view = EMPATHY_CHAT_TEXT_VIEW (theme);

	/* Define base tags */
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_SPACING,
					"size", 2000,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_TIME,
					"foreground", "darkgrey",
					"justification", GTK_JUSTIFY_CENTER,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_ACTION,
					"foreground", "brown4",
					"style", PANGO_STYLE_ITALIC,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_BODY,
					"foreground-set", FALSE,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_EVENT,
					"foreground", "PeachPuff4",
					"justification", GTK_JUSTIFY_LEFT,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_LINK,
					"foreground", "steelblue",
					"underline", PANGO_UNDERLINE_SINGLE,
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_HIGHLIGHT,
					"background", "yellow",
					NULL);

	/* Define IRC tags */
	empathy_chat_text_view_tag_set (view, EMPATHY_THEME_IRC_TAG_NICK_SELF,
					"foreground", "sea green",
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_THEME_IRC_TAG_NICK_OTHER,
					"foreground", "skyblue4",
					NULL);
	empathy_chat_text_view_tag_set (view, EMPATHY_THEME_IRC_TAG_NICK_HIGHLIGHT,
					"foreground", "indian red",
					"weight", PANGO_WEIGHT_BOLD,
					NULL);

	return theme;
}

static void on_style_set_cb (GtkWidget *widget, GtkStyle *previous_style, EmpathyThemeManager *self);

static EmpathyThemeBoxes *
theme_manager_create_boxes_view (EmpathyThemeManager *manager)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	EmpathyThemeBoxes       *theme;

	theme = empathy_theme_boxes_new ();
	priv->boxes_views = g_list_prepend (priv->boxes_views, theme);
	g_object_weak_ref (G_OBJECT (theme),
			   theme_manager_view_weak_notify_cb,
			   &priv->boxes_views);

	g_signal_connect (G_OBJECT (theme), "style-set",
			  G_CALLBACK (on_style_set_cb), manager);

	return theme;
}

static void
theme_manager_update_boxes_tags (EmpathyThemeBoxes *theme,
				 const gchar       *header_foreground,
				 const gchar       *header_background,
				 const gchar       *header_line_background,
				 const gchar       *action_foreground,
				 const gchar       *time_foreground,
				 const gchar       *event_foreground,
				 const gchar       *link_foreground,
				 const gchar       *text_foreground,
				 const gchar       *text_background,
				 const gchar       *highlight_foreground)

{
	EmpathyChatTextView *view = EMPATHY_CHAT_TEXT_VIEW (theme);
	GtkTextTag          *tag;

	DEBUG ("Update view with new colors:\n"
		"header_foreground = %s\n"
		"header_background = %s\n"
		"header_line_background = %s\n"
		"action_foreground = %s\n"
		"time_foreground = %s\n"
		"event_foreground = %s\n"
		"link_foreground = %s\n"
		"text_foreground = %s\n"
		"text_background = %s\n"
		"highlight_foreground = %s\n",
		header_foreground, header_background, header_line_background,
		action_foreground, time_foreground, event_foreground,
		link_foreground, text_foreground, text_background,
		highlight_foreground);


	/* FIXME: GtkTextTag don't support to set color properties to NULL.
	 * See bug #542523 */

	#define TAG_SET(prop, prop_set, value) \
		if (value != NULL) { \
			g_object_set (tag, prop, value, NULL); \
		} else { \
			g_object_set (tag, prop_set, FALSE, NULL); \
		}

	/* Define base tags */
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_HIGHLIGHT,
					      "weight", PANGO_WEIGHT_BOLD,
					      "pixels-above-lines", 4,
					      NULL);
	TAG_SET ("paragraph-background", "paragraph-background-set", text_background);
	TAG_SET ("foreground", "foreground-set", highlight_foreground);

	empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_SPACING,
					"size", 3000,
					"pixels-above-lines", 8,
					NULL);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_TIME,
					      "justification", GTK_JUSTIFY_CENTER,
					      NULL);
	TAG_SET ("foreground", "foreground-set", time_foreground);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_ACTION,
					      "style", PANGO_STYLE_ITALIC,
					      "pixels-above-lines", 4,
					      NULL);
	TAG_SET ("paragraph-background", "paragraph-background-set", text_background);
	TAG_SET ("foreground", "foreground-set", action_foreground);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_BODY,
					      "pixels-above-lines", 4,
					      NULL);
	TAG_SET ("paragraph-background", "paragraph-background-set", text_background);
	TAG_SET ("foreground", "foreground-set", text_foreground);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_EVENT,
					      "justification", GTK_JUSTIFY_LEFT,
					      NULL);
	TAG_SET ("foreground", "foreground-set", event_foreground);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_CHAT_TEXT_VIEW_TAG_LINK,
					      "underline", PANGO_UNDERLINE_SINGLE,
					      NULL);
	TAG_SET ("foreground", "foreground-set", link_foreground);

	/* Define BOXES tags */
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_THEME_BOXES_TAG_HEADER,
					      "weight", PANGO_WEIGHT_BOLD,
					      NULL);
	TAG_SET ("foreground", "foreground-set", header_foreground);
	TAG_SET ("paragraph-background", "paragraph-background-set", header_background);
	tag = empathy_chat_text_view_tag_set (view, EMPATHY_THEME_BOXES_TAG_HEADER_LINE,
					      "size", 1,
					      NULL);
	TAG_SET ("paragraph-background", "paragraph-background-set", header_line_background);

	#undef TAG_SET
}

static void
on_style_set_cb (GtkWidget *widget, GtkStyle *previous_style, EmpathyThemeManager *self)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (self);
	GtkStyle *style;
	gchar     color1[10];
	gchar     color2[10];
	gchar     color3[10];
	gchar     color4[10];

	/* The simple theme depends on the current GTK+ theme so it has to be
	 * updated if the theme changes. */
	if (tp_strdiff (priv->name, "simple"))
		return;

	style = gtk_widget_get_style (GTK_WIDGET (widget));

	theme_manager_gdk_color_to_hex (&style->base[GTK_STATE_SELECTED], color1);
	theme_manager_gdk_color_to_hex (&style->bg[GTK_STATE_SELECTED], color2);
	theme_manager_gdk_color_to_hex (&style->dark[GTK_STATE_SELECTED], color3);
	theme_manager_gdk_color_to_hex (&style->fg[GTK_STATE_SELECTED], color4);

	theme_manager_update_boxes_tags (EMPATHY_THEME_BOXES (widget),
					 color4,     /* header_foreground */
					 color2,     /* header_background */
					 color3,     /* header_line_background */
					 color1,     /* action_foreground */
					 "darkgrey", /* time_foreground */
					 "darkgrey", /* event_foreground */
					 color1,     /* link_foreground */
					 NULL,       /* text_foreground */
					 NULL,       /* text_background */
					 NULL);      /* highlight_foreground */
}

static void
theme_manager_update_boxes_theme (EmpathyThemeManager *manager,
				  EmpathyThemeBoxes   *theme)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);

	if (strcmp (priv->name, "simple") == 0) {
		on_style_set_cb (GTK_WIDGET (theme), NULL, manager);
	}
	else if (strcmp (priv->name, "clean") == 0) {
		theme_manager_update_boxes_tags (theme,
						 "black",    /* header_foreground */
						 "#efefdf",  /* header_background */
						 "#e3e3d3",  /* header_line_background */
						 "brown4",   /* action_foreground */
						 "darkgrey", /* time_foreground */
						 "darkgrey", /* event_foreground */
						 "#49789e",  /* link_foreground */
						 NULL,       /* text_foreground */
						 NULL,       /* text_background */
						 NULL);      /* highlight_foreground */
	}
	else if (strcmp (priv->name, "blue") == 0) {
		theme_manager_update_boxes_tags (theme,
						 "black",    /* header_foreground */
						 "#88a2b4",  /* header_background */
						 "#7f96a4",  /* header_line_background */
						 "brown4",   /* action_foreground */
						 "darkgrey", /* time_foreground */
						 "#7f96a4",  /* event_foreground */
						 "#49789e",  /* link_foreground */
						 "black",    /* text_foreground */
						 "#adbdc8",  /* text_background */
						 "black");   /* highlight_foreground */
	}
}

static EmpathyThemeAdium *
theme_manager_create_adium_view (EmpathyThemeManager *manager)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	EmpathyThemeAdium *theme;

	theme = empathy_theme_adium_new (priv->adium_data, priv->adium_variant);
	priv->adium_views = g_list_prepend (priv->adium_views, theme);
	g_object_weak_ref (G_OBJECT (theme),
			   theme_manager_view_weak_notify_cb,
			   &priv->adium_views);

	return theme;
}

static void
theme_manager_notify_adium_path_cb (GSettings   *gsettings_chat,
				    const gchar *key,
				    gpointer     user_data)
{
	EmpathyThemeManager     *manager = EMPATHY_THEME_MANAGER (user_data);
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	const gchar             *current_path = NULL;
	gchar                   *new_path;

	new_path = g_settings_get_string (gsettings_chat, key);

	if (priv->adium_data != NULL) {
		current_path = empathy_adium_data_get_path (priv->adium_data);
	}

	/* If path did not really changed, ignore */
	if (!tp_strdiff (current_path, new_path)) {
		g_free (new_path);
		return;
	}

	/* If path does not really contains an adium path, ignore */
	if (!empathy_adium_path_is_valid (new_path)) {
		DEBUG ("Invalid theme path set: %s", new_path);
		g_free (new_path);
		return;
	}

	/* Load new theme data, we can stop tracking existing views since we
	 * won't be able to change them live anymore */
	clear_list_of_views (&priv->adium_views);
	tp_clear_pointer (&priv->adium_data, empathy_adium_data_unref);
	priv->adium_data = empathy_adium_data_new (new_path);

	theme_manager_emit_changed (manager);

	g_free (new_path);
}

static void
theme_manager_notify_adium_variant_cb (GSettings   *gsettings_chat,
				       const gchar *key,
				       gpointer     user_data)
{
	EmpathyThemeManager     *manager = EMPATHY_THEME_MANAGER (user_data);
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	gchar                   *new_variant;
	GList                   *l;

	new_variant = g_settings_get_string (gsettings_chat, key);
	if (!tp_strdiff (priv->adium_variant, new_variant)) {
		g_free (new_variant);
		return;
	}

	g_free (priv->adium_variant);
	priv->adium_variant = new_variant;

	for (l = priv->adium_views; l; l = l->next) {
		empathy_theme_adium_set_variant (EMPATHY_THEME_ADIUM (l->data),
			priv->adium_variant);
	}
}

EmpathyChatView *
empathy_theme_manager_create_view (EmpathyThemeManager *manager)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	EmpathyThemeBoxes       *theme;

	g_return_val_if_fail (EMPATHY_IS_THEME_MANAGER (manager), NULL);

	DEBUG ("Using theme %s", priv->name);

	if (strcmp (priv->name, "adium") == 0 && priv->adium_data != NULL)  {
		return EMPATHY_CHAT_VIEW (theme_manager_create_adium_view (manager));
	}

	if (strcmp (priv->name, "classic") == 0)  {
		return EMPATHY_CHAT_VIEW (theme_manager_create_irc_view (manager));
	}

	theme = theme_manager_create_boxes_view (manager);
	theme_manager_update_boxes_theme (manager, theme);

	return EMPATHY_CHAT_VIEW (theme);
}

static gboolean
theme_manager_ensure_theme_exists (const gchar *name)
{
	gint i;

	if (EMP_STR_EMPTY (name)) {
		return FALSE;
	}

	if (strcmp ("adium", name) == 0) {
		return TRUE;
	}

	for (i = 0; themes[i]; i += 2) {
		if (strcmp (themes[i], name) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

typedef enum {
	THEME_TYPE_IRC,
	THEME_TYPE_BOXED,
	THEME_TYPE_ADIUM,
} ThemeType;

static ThemeType
theme_type (const gchar *name)
{
	if (!tp_strdiff (name, "classic")) {
		return THEME_TYPE_IRC;
	} else if (!tp_strdiff (name, "adium")) {
		return THEME_TYPE_ADIUM;
	} else {
		return THEME_TYPE_BOXED;
	}
}

static void
theme_manager_notify_name_cb (GSettings   *gsettings_chat,
			      const gchar *key,
			      gpointer     user_data)
{
	EmpathyThemeManager     *manager = EMPATHY_THEME_MANAGER (user_data);
	EmpathyThemeManagerPriv *priv = GET_PRIV (manager);
	gchar                   *name;
	ThemeType                old_type;
	ThemeType                new_type;

	name = g_settings_get_string (gsettings_chat, key);

	/* Fallback to classic theme if current setting does not exist */
	if (!theme_manager_ensure_theme_exists (name)) {
		g_free (name);
		name = g_strdup ("classic");
	}

	/* If theme did not change, nothing to do */
	if (!tp_strdiff (priv->name, name)) {
		g_free (name);
		return;
	}

	old_type = theme_type (priv->name);
	g_free (priv->name);
	priv->name = name;
	new_type = theme_type (priv->name);

	if (new_type == THEME_TYPE_BOXED) {
		GList *l;

		/* The theme changes to a boxed one, we can update boxed views */
		for (l = priv->boxes_views; l; l = l->next) {
			theme_manager_update_boxes_theme (manager,
							  EMPATHY_THEME_BOXES (l->data));
		}
	}

	/* Do not emit theme-changed if theme type didn't change. If theme
	 * changed from a boxed to another boxed, all view are updated in place.
	 * If theme changed from an adium to another adium, the signal will be
	 * emited from theme_manager_notify_adium_path_cb ()
	 */
	if (old_type != new_type) {
		theme_manager_emit_changed (manager);
	}
}

static void
theme_manager_finalize (GObject *object)
{
	EmpathyThemeManagerPriv *priv = GET_PRIV (object);

	g_object_unref (priv->gsettings_chat);
	g_free (priv->name);

	if (priv->emit_changed_idle != 0) {
		g_source_remove (priv->emit_changed_idle);
	}

	clear_list_of_views (&priv->boxes_views);

	clear_list_of_views (&priv->adium_views);
	g_free (priv->adium_variant);
	tp_clear_pointer (&priv->adium_data, empathy_adium_data_unref);

	G_OBJECT_CLASS (empathy_theme_manager_parent_class)->finalize (object);
}

static void
empathy_theme_manager_class_init (EmpathyThemeManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	signals[THEME_CHANGED] =
		g_signal_new ("theme-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      g_cclosure_marshal_generic,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (EmpathyThemeManagerPriv));

	object_class->finalize = theme_manager_finalize;
}

static void
empathy_theme_manager_init (EmpathyThemeManager *manager)
{
	EmpathyThemeManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
		EMPATHY_TYPE_THEME_MANAGER, EmpathyThemeManagerPriv);

	manager->priv = priv;
	priv->in_constructor = TRUE;

	priv->gsettings_chat = g_settings_new (EMPATHY_PREFS_CHAT_SCHEMA);

	/* Take the theme name and track changes */
	g_signal_connect (priv->gsettings_chat,
			  "changed::" EMPATHY_PREFS_CHAT_THEME,
			  G_CALLBACK (theme_manager_notify_name_cb),
			  manager);
	theme_manager_notify_name_cb (priv->gsettings_chat,
				      EMPATHY_PREFS_CHAT_THEME,
				      manager);

	/* Take the adium path/variant and track changes */
	g_signal_connect (priv->gsettings_chat,
			  "changed::" EMPATHY_PREFS_CHAT_ADIUM_PATH,
			  G_CALLBACK (theme_manager_notify_adium_path_cb),
			  manager);
	theme_manager_notify_adium_path_cb (priv->gsettings_chat,
					    EMPATHY_PREFS_CHAT_ADIUM_PATH,
					    manager);

	g_signal_connect (priv->gsettings_chat,
			  "changed::" EMPATHY_PREFS_CHAT_THEME_VARIANT,
			  G_CALLBACK (theme_manager_notify_adium_variant_cb),
			  manager);
	theme_manager_notify_adium_variant_cb (priv->gsettings_chat,
					       EMPATHY_PREFS_CHAT_THEME_VARIANT,
					       manager);
	priv->in_constructor = FALSE;
}

EmpathyThemeManager *
empathy_theme_manager_dup_singleton (void)
{
	static EmpathyThemeManager *manager = NULL;

	if (manager == NULL) {
		manager = g_object_new (EMPATHY_TYPE_THEME_MANAGER, NULL);
		g_object_add_weak_pointer (G_OBJECT (manager), (gpointer *) &manager);

		return manager;
	}

	return g_object_ref (manager);
}

const gchar **
empathy_theme_manager_get_themes (void)
{
	return themes;
}

static void
find_themes (GList **list, const gchar *dirpath)
{
	GDir *dir;
	GError *error = NULL;
	const gchar *name = NULL;
	GHashTable *info = NULL;

	dir = g_dir_open (dirpath, 0, &error);
	if (dir != NULL) {
		name = g_dir_read_name (dir);
		while (name != NULL) {
			gchar *path;

			path = g_build_path (G_DIR_SEPARATOR_S, dirpath, name, NULL);
			if (empathy_adium_path_is_valid (path)) {
				info = empathy_adium_info_new (path);
				if (info != NULL) {
					*list = g_list_prepend (*list, info);
				}
			}
			g_free (path);
			name = g_dir_read_name (dir);
		}
		g_dir_close (dir);
	} else {
		DEBUG ("Error opening %s: %s\n", dirpath, error->message);
		g_error_free (error);
	}
}

GList *
empathy_theme_manager_get_adium_themes (void)
{
	GList *themes_list = NULL;
	gchar *userpath = NULL;
	const gchar *const *paths = NULL;
	gint i = 0;

	userpath = g_build_path (G_DIR_SEPARATOR_S, g_get_user_data_dir (), "adium/message-styles", NULL);
	find_themes (&themes_list, userpath);
	g_free (userpath);

	paths = g_get_system_data_dirs ();
	for (i = 0; paths[i] != NULL; i++) {
		userpath = g_build_path (G_DIR_SEPARATOR_S, paths[i],
			"adium/message-styles", NULL);
		find_themes (&themes_list, userpath);
		g_free (userpath);
	}

	return themes_list;
}

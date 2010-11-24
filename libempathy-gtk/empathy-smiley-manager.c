/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 * Authors: Dafydd Harrie <dafydd.harries@collabora.co.uk>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#include <config.h>

#include <string.h>

#include <libempathy/empathy-utils.h>
#include "empathy-smiley-manager.h"
#include "empathy-ui-utils.h"

typedef struct _SmileyManagerTree SmileyManagerTree;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathySmileyManager)
typedef struct {
	SmileyManagerTree *tree;
	GSList            *smileys;
} EmpathySmileyManagerPriv;

struct _SmileyManagerTree {
	gunichar     c;
	GdkPixbuf   *pixbuf;
	gchar       *path;
	GSList      *childrens;
};

G_DEFINE_TYPE (EmpathySmileyManager, empathy_smiley_manager, G_TYPE_OBJECT);

static EmpathySmileyManager *manager_singleton = NULL;

static SmileyManagerTree *
smiley_manager_tree_new (gunichar c)
{
	SmileyManagerTree *tree;

	tree = g_slice_new0 (SmileyManagerTree);
	tree->c = c;
	tree->pixbuf = NULL;
	tree->childrens = NULL;
	tree->path = NULL;

	return tree;
}

static void
smiley_manager_tree_free (SmileyManagerTree *tree)
{
	GSList *l;

	if (!tree) {
		return;
	}

	for (l = tree->childrens; l; l = l->next) {
		smiley_manager_tree_free (l->data);
	}

	if (tree->pixbuf) {
		g_object_unref (tree->pixbuf);
	}
	g_slist_free (tree->childrens);
	g_free (tree->path);
	g_slice_free (SmileyManagerTree, tree);
}

static EmpathySmiley *
smiley_new (GdkPixbuf *pixbuf, const gchar *str)
{
	EmpathySmiley *smiley;

	smiley = g_slice_new0 (EmpathySmiley);
	smiley->pixbuf = g_object_ref (pixbuf);
	smiley->str = g_strdup (str);

	return smiley;
}

static void
smiley_free (EmpathySmiley *smiley)
{
	g_object_unref (smiley->pixbuf);
	g_free (smiley->str);
	g_slice_free (EmpathySmiley, smiley);
}

static void
smiley_manager_finalize (GObject *object)
{
	EmpathySmileyManagerPriv *priv = GET_PRIV (object);

	smiley_manager_tree_free (priv->tree);
	g_slist_foreach (priv->smileys, (GFunc) smiley_free, NULL);
	g_slist_free (priv->smileys);
}

static GObject *
smiley_manager_constructor (GType type,
			    guint n_props,
			    GObjectConstructParam *props)
{
	GObject *retval;

	if (manager_singleton) {
		retval = g_object_ref (manager_singleton);
	} else {
		retval = G_OBJECT_CLASS (empathy_smiley_manager_parent_class)->constructor
			(type, n_props, props);

		manager_singleton = EMPATHY_SMILEY_MANAGER (retval);
		g_object_add_weak_pointer (retval, (gpointer) &manager_singleton);
	}

	return retval;
}

static void
empathy_smiley_manager_class_init (EmpathySmileyManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = smiley_manager_finalize;
	object_class->constructor = smiley_manager_constructor;

	g_type_class_add_private (object_class, sizeof (EmpathySmileyManagerPriv));
}

static void
empathy_smiley_manager_init (EmpathySmileyManager *manager)
{
	EmpathySmileyManagerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (manager,
		EMPATHY_TYPE_SMILEY_MANAGER, EmpathySmileyManagerPriv);

	manager->priv = priv;
	priv->tree = smiley_manager_tree_new ('\0');
	priv->smileys = NULL;

	empathy_smiley_manager_load (manager);
}

EmpathySmileyManager *
empathy_smiley_manager_dup_singleton (void)
{
	return g_object_new (EMPATHY_TYPE_SMILEY_MANAGER, NULL);
}

static SmileyManagerTree *
smiley_manager_tree_find_child (SmileyManagerTree *tree, gunichar c)
{
	GSList *l;

	for (l = tree->childrens; l; l = l->next) {
		SmileyManagerTree *child = l->data;

		if (child->c == c) {
			return child;
		}
	}

	return NULL;
}

static SmileyManagerTree *
smiley_manager_tree_find_or_insert_child (SmileyManagerTree *tree, gunichar c)
{
	SmileyManagerTree *child;

	child = smiley_manager_tree_find_child (tree, c);

	if (!child) {
		child = smiley_manager_tree_new (c);
		tree->childrens = g_slist_prepend (tree->childrens, child);
	}

	return child;
}

static void
smiley_manager_tree_insert (SmileyManagerTree *tree,
			    GdkPixbuf         *pixbuf,
			    const gchar       *str,
			    const gchar       *path)
{
	SmileyManagerTree *child;

	child = smiley_manager_tree_find_or_insert_child (tree, g_utf8_get_char (str));

	str = g_utf8_next_char (str);
	if (*str) {
		smiley_manager_tree_insert (child, pixbuf, str, path);
		return;
	}

	child->pixbuf = g_object_ref (pixbuf);
	child->path = g_strdup (path);
}

static void
smiley_manager_add_valist (EmpathySmileyManager *manager,
			   GdkPixbuf            *pixbuf,
			   const gchar          *path,
			   const gchar          *first_str,
			   va_list               var_args)
{
	EmpathySmileyManagerPriv *priv = GET_PRIV (manager);
	const gchar              *str;
	EmpathySmiley            *smiley;

	for (str = first_str; str; str = va_arg (var_args, gchar*)) {
		smiley_manager_tree_insert (priv->tree, pixbuf, str, path);
	}

	g_object_set_data_full (G_OBJECT (pixbuf), "smiley_str",
				g_strdup (first_str), g_free);
	smiley = smiley_new (pixbuf, first_str);
	priv->smileys = g_slist_prepend (priv->smileys, smiley);
}

void
empathy_smiley_manager_add (EmpathySmileyManager *manager,
			    const gchar          *icon_name,
			    const gchar          *first_str,
			    ...)
{
	GdkPixbuf *pixbuf;
	va_list    var_args;

	g_return_if_fail (EMPATHY_IS_SMILEY_MANAGER (manager));
	g_return_if_fail (!EMP_STR_EMPTY (icon_name));
	g_return_if_fail (!EMP_STR_EMPTY (first_str));

	pixbuf = empathy_pixbuf_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
	if (pixbuf) {
		gchar *path;

		va_start (var_args, first_str);
		path = empathy_filename_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
		smiley_manager_add_valist (manager, pixbuf, path, first_str, var_args);
		va_end (var_args);
		g_object_unref (pixbuf);
		g_free (path);
	}
}

void
empathy_smiley_manager_load (EmpathySmileyManager *manager)
{
	g_return_if_fail (EMPATHY_IS_SMILEY_MANAGER (manager));

	/* From fd.o icon-naming spec */
	empathy_smiley_manager_add (manager, "face-angel",      "O:-)",  "O:)",  NULL);
	empathy_smiley_manager_add (manager, "face-angry",      "X-(",   ":@",   NULL);
	empathy_smiley_manager_add (manager, "face-cool",       "B-)",   NULL);
	empathy_smiley_manager_add (manager, "face-crying",     ":'(",           NULL);
	empathy_smiley_manager_add (manager, "face-devilish",   ">:-)",  ">:)",  NULL);
	empathy_smiley_manager_add (manager, "face-embarrassed",":-[",   ":[",   ":-$", ":$", NULL);
	empathy_smiley_manager_add (manager, "face-kiss",       ":-*",   ":*",   NULL);
	empathy_smiley_manager_add (manager, "face-laugh",      ":-))",  ":))",  NULL);
	empathy_smiley_manager_add (manager, "face-monkey",     ":-(|)", ":(|)", NULL);
	empathy_smiley_manager_add (manager, "face-plain",      ":-|",   ":|",   NULL);
	empathy_smiley_manager_add (manager, "face-raspberry",  ":-P",   ":P",	 ":-p", ":p", NULL);
	empathy_smiley_manager_add (manager, "face-sad",        ":-(",   ":(",   NULL);
	empathy_smiley_manager_add (manager, "face-sick",       ":-&",   ":&",   NULL);
	empathy_smiley_manager_add (manager, "face-smile",      ":-)",   ":)",   NULL);
	empathy_smiley_manager_add (manager, "face-smile-big",  ":-D",   ":D",   ":-d", ":d", NULL);
	empathy_smiley_manager_add (manager, "face-smirk",      ":-!",   ":!",   NULL);
	empathy_smiley_manager_add (manager, "face-surprise",   ":-O",   ":O",   ":-o", ":o", NULL);
	empathy_smiley_manager_add (manager, "face-tired",      "|-)",   "|)",   NULL);
	empathy_smiley_manager_add (manager, "face-uncertain",  ":-/",   ":/",   NULL);
	empathy_smiley_manager_add (manager, "face-wink",       ";-)",   ";)",   NULL);
	empathy_smiley_manager_add (manager, "face-worried",    ":-S",   ":S",   ":-s", ":s", NULL);
}

static EmpathySmileyHit *
smiley_hit_new (SmileyManagerTree *tree,
		guint              start,
		guint              end)
{
	EmpathySmileyHit *hit;

	hit = g_slice_new (EmpathySmileyHit);
	hit->pixbuf = tree->pixbuf;
	hit->path = tree->path;
	hit->start = start;
	hit->end = end;

	return hit;
}

void
empathy_smiley_hit_free (EmpathySmileyHit *hit)
{
	g_return_if_fail (hit != NULL);

	g_slice_free (EmpathySmileyHit, hit);
}

GSList *
empathy_smiley_manager_parse_len (EmpathySmileyManager *manager,
				  const gchar          *text,
				  gssize                len)
{
	EmpathySmileyManagerPriv *priv = GET_PRIV (manager);
	EmpathySmileyHit         *hit;
	GSList                   *hits = NULL;
	SmileyManagerTree        *cur_tree = priv->tree;
	const gchar              *cur_str;
	const gchar              *start = NULL;

	g_return_val_if_fail (EMPATHY_IS_SMILEY_MANAGER (manager), NULL);
	g_return_val_if_fail (text != NULL, NULL);

	/* If len is negative, parse the string until we find '\0' */
	if (len < 0) {
		len = G_MAXSSIZE;
	}

	/* Parse the len first bytes of text to find smileys. Each time a smiley
	 * is detected, append a EmpathySmileyHit struct to the returned list,
	 * containing the smiley pixbuf and the position of the text to be
	 * replaced by it.
	 * cur_str is a pointer in the text showing the current position
	 * of the parsing. It is always at the begining of an UTF-8 character,
	 * because we support unicode smileys! For example we could want to
	 * replace ™ by an image. */

	for (cur_str = text;
	     *cur_str != '\0' && cur_str - text < len;
	     cur_str = g_utf8_next_char (cur_str)) {
		SmileyManagerTree *child;
		gunichar           c;

		c = g_utf8_get_char (cur_str);
		child = smiley_manager_tree_find_child (cur_tree, c);

		/* If we have a child it means c is part of a smiley */
		if (child) {
			if (cur_tree == priv->tree) {
				/* c is the first char of some smileys, keep
				 * the begining position */
				start = cur_str;
			}
			cur_tree = child;
			continue;
		}

		/* c is not part of a smiley. let's check if we found a smiley
		 * before it. */
		if (cur_tree->pixbuf != NULL) {
			/* found! */
			hit = smiley_hit_new (cur_tree, start - text,
					      cur_str - text);
			hits = g_slist_prepend (hits, hit);

			/* c was not part of this smiley, check if a new smiley
			 * start with it. */
			cur_tree = smiley_manager_tree_find_child (priv->tree, c);
			if (cur_tree) {
				start = cur_str;
			} else {
				cur_tree = priv->tree;
			}
		} else if (cur_tree != priv->tree) {
			/* We searched a smiley starting at 'start' but we ended
			 * with no smiley. Look again starting from next char.
			 *
			 * For example ">:)" and ":(" are both valid smileys,
			 * when parsing text ">:(" we first see '>' which could
			 * be the start of a smiley. 'start' variable is set to
			 * that position and we parse next char which is ':' and
			 * is still potential smiley. Then we see '(' which is
			 * NOT part of the smiley, ">:(" does not exist, so we
			 * have to start again from ':' to find ":(" which is
			 * correct smiley. */
			cur_str = start;
			cur_tree = priv->tree;
		}
	}

	/* Check if last char of the text was the end of a smiley */
	if (cur_tree->pixbuf != NULL) {
		hit = smiley_hit_new (cur_tree, start - text, cur_str - text);
		hits = g_slist_prepend (hits, hit);
	}

	return g_slist_reverse (hits);
}

GSList *
empathy_smiley_manager_get_all (EmpathySmileyManager *manager)
{
	EmpathySmileyManagerPriv *priv = GET_PRIV (manager);

	return priv->smileys;
}

typedef struct {
	EmpathySmileyManager *manager;
	EmpathySmiley        *smiley;
	EmpathySmileyMenuFunc func;
	gpointer              user_data;
} ActivateData;

static void
smiley_menu_data_free (gpointer  user_data,
		       GClosure *closure)
{
	ActivateData *data = (ActivateData *) user_data;

	g_object_unref (data->manager);
	g_slice_free (ActivateData, data);
}

static void
smiley_menu_activate_cb (GtkMenuItem *menuitem,
			 gpointer     user_data)
{
	ActivateData *data = (ActivateData *) user_data;

	data->func (data->manager, data->smiley, data->user_data);
}

GtkWidget *
empathy_smiley_menu_new (EmpathySmileyManager *manager,
			 EmpathySmileyMenuFunc func,
			 gpointer              user_data)
{
	EmpathySmileyManagerPriv *priv = GET_PRIV (manager);
	GSList                   *l;
	GtkWidget                *menu;
	gint                      x = 0;
	gint                      y = 0;

	g_return_val_if_fail (EMPATHY_IS_SMILEY_MANAGER (manager), NULL);
	g_return_val_if_fail (func != NULL, NULL);

	menu = gtk_menu_new ();

	for (l = priv->smileys; l; l = l->next) {
		EmpathySmiley *smiley;
		GtkWidget     *item;
		GtkWidget     *image;
		ActivateData  *data;

		smiley = l->data;
		image = gtk_image_new_from_pixbuf (smiley->pixbuf);

		item = gtk_image_menu_item_new_with_label ("");
		gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
		gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (item), TRUE);

		gtk_menu_attach (GTK_MENU (menu), item,
				 x, x + 1, y, y + 1);

		gtk_widget_set_tooltip_text (item, smiley->str);

		data = g_slice_new (ActivateData);
		data->manager = g_object_ref (manager);
		data->smiley = smiley;
		data->func = func;
		data->user_data = user_data;

		g_signal_connect_data (item, "activate",
				       G_CALLBACK (smiley_menu_activate_cb),
				       data,
				       smiley_menu_data_free,
				       0);

		if (x > 3) {
			y++;
			x = 0;
		} else {
			x++;
		}
	}

	gtk_widget_show_all (menu);

	return menu;
}


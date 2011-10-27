/*
 * empathy-contact-chooser.h
 *
 * EmpathyContactChooser
 *
 * (c) 2010, Collabora Ltd.
 *
 * Authors:
 *    Guillaume Desmottes <guillaume.desmottes@collabora.com>
 */

#ifndef __EMPATHY_CONTACT_CHOOSER_H__
#define __EMPATHY_CONTACT_CHOOSER_H__

#include <gtk/gtk.h>

#include <telepathy-glib/account.h>

#include "libempathy/empathy-tp-chat.h"

G_BEGIN_DECLS

#define EMPATHY_TYPE_CONTACT_CHOOSER	(empathy_contact_chooser_get_type ())
#define EMPATHY_CONTACT_CHOOSER(obj)	(G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_CONTACT_CHOOSER, EmpathyContactChooser))
#define EMPATHY_CONTACT_CHOOSER_CLASS(obj)	(G_TYPE_CHECK_CLASS_CAST ((obj), EMPATHY_TYPE_CONTACT_CHOOSER, EmpathyContactChooserClass))
#define EMPATHY_IS_CONTACT_CHOOSER(obj)	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_CONTACT_CHOOSER))
#define EMPATHY_IS_CONTACT_CHOOSER_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE ((obj), EMPATHY_TYPE_CONTACT_CHOOSER))
#define EMPATHY_CONTACT_CHOOSER_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_CONTACT_CHOOSER, EmpathyContactChooserClass))

typedef struct _EmpathyContactChooser EmpathyContactChooser;
typedef struct _EmpathyContactChooserClass EmpathyContactChooserClass;
typedef struct _EmpathyContactChooserPrivate EmpathyContactChooserPrivate;

typedef gboolean (*EmpathyContactChooserFilterFunc) (
    EmpathyContactChooser *self,
    FolksIndividual *individual,
    gboolean is_online,
    gboolean searching,
    gpointer user_data);

struct _EmpathyContactChooser
{
  GtkBox parent;

  EmpathyContactChooserPrivate *priv;
};

struct _EmpathyContactChooserClass
{
  GtkBoxClass parent_class;
};

GType empathy_contact_chooser_get_type (void);

GtkWidget * empathy_contact_chooser_new (void);

FolksIndividual * empathy_contact_chooser_dup_selected (
    EmpathyContactChooser *self);

void empathy_contact_chooser_set_filter_func (EmpathyContactChooser *self,
    EmpathyContactChooserFilterFunc func,
    gpointer user_data);

void empathy_contact_chooser_show_search_entry (EmpathyContactChooser *self,
    gboolean show);

void empathy_contact_chooser_show_tree_view (EmpathyContactChooser *self,
    gboolean show);

G_END_DECLS

#endif

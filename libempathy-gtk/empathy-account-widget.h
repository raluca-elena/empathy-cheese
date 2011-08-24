/*
 * Copyright (C) 2006-2007 Imendio AB
 * Copyright (C) 2007-2008 Collabora Ltd.
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
 *          Martyn Russell <martyn@imendio.com>
 */

#ifndef __EMPATHY_ACCOUNT_WIDGET_H__
#define __EMPATHY_ACCOUNT_WIDGET_H__

#include <gtk/gtk.h>

#include <libempathy/empathy-account-settings.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_ACCOUNT_WIDGET empathy_account_widget_get_type()
#define EMPATHY_ACCOUNT_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), EMPATHY_TYPE_ACCOUNT_WIDGET, EmpathyAccountWidget))
#define EMPATHY_ACCOUNT_WIDGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), EMPATHY_TYPE_ACCOUNT_WIDGET, EmpathyAccountWidgetClass))
#define EMPATHY_IS_ACCOUNT_WIDGET(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), EMPATHY_TYPE_ACCOUNT_WIDGET))
#define EMPATHY_IS_ACCOUNT_WIDGET_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), EMPATHY_TYPE_ACCOUNT_WIDGET))
#define EMPATHY_ACCOUNT_WIDGET_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), EMPATHY_TYPE_ACCOUNT_WIDGET, EmpathyAccountWidgetClass))

typedef struct _EmpathyAccountWidgetUIDetails EmpathyAccountWidgetUIDetails;

typedef struct {
  GObject parent;

  EmpathyAccountWidgetUIDetails *ui_details;

  /* private */
  gpointer priv;
} EmpathyAccountWidget;

typedef struct {
  GObjectClass parent_class;
} EmpathyAccountWidgetClass;

GType empathy_account_widget_get_type (void);

GtkWidget *empathy_account_widget_get_widget (EmpathyAccountWidget *widget);

EmpathyAccountWidget * empathy_account_widget_new_for_protocol (
    EmpathyAccountSettings *settings,
    gboolean simple);

gboolean empathy_account_widget_contains_pending_changes
    (EmpathyAccountWidget *widget);
void empathy_account_widget_discard_pending_changes
    (EmpathyAccountWidget *widget);

gchar * empathy_account_widget_get_default_display_name (
    EmpathyAccountWidget *widget);

void empathy_account_widget_set_account_param (EmpathyAccountWidget *widget,
    const gchar *account);

void empathy_account_widget_set_password_param (EmpathyAccountWidget *self,
    const gchar *password);

void empathy_account_widget_set_other_accounts_exist (
    EmpathyAccountWidget *self, gboolean others_exist);

/* protected methods */
void empathy_account_widget_changed (EmpathyAccountWidget *widget);

EmpathyAccountSettings * empathy_account_widget_get_settings (
    EmpathyAccountWidget *self);

G_END_DECLS

#endif /* __EMPATHY_ACCOUNT_WIDGET_H__ */

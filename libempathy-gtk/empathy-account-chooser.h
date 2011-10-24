/*
 * Copyright (C) 2005-2007 Imendio AB
 * Copyright (C) 2007-2011 Collabora Ltd.
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
 * Authors: Martyn Russell <martyn@imendio.com>
 *          Xavier Claessens <xclaesse@gmail.com>
 */

#ifndef __EMPATHY_ACCOUNT_CHOOSER_H__
#define __EMPATHY_ACCOUNT_CHOOSER_H__

#include <gtk/gtk.h>

#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

#define EMPATHY_TYPE_ACCOUNT_CHOOSER (empathy_account_chooser_get_type ())
#define EMPATHY_ACCOUNT_CHOOSER(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), EMPATHY_TYPE_ACCOUNT_CHOOSER, EmpathyAccountChooser))
#define EMPATHY_ACCOUNT_CHOOSER_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), EMPATHY_TYPE_ACCOUNT_CHOOSER, EmpathyAccountChooserClass))
#define EMPATHY_IS_ACCOUNT_CHOOSER(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), EMPATHY_TYPE_ACCOUNT_CHOOSER))
#define EMPATHY_IS_ACCOUNT_CHOOSER_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), EMPATHY_TYPE_ACCOUNT_CHOOSER))
#define EMPATHY_ACCOUNT_CHOOSER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), EMPATHY_TYPE_ACCOUNT_CHOOSER, EmpathyAccountChooserClass))

/**
 * EmpathyAccountChooserFilterResultCallback:
 * @is_enabled: indicated whether the account should be enabled
 * @user_data: user data passed to the callback
 */
typedef void (* EmpathyAccountChooserFilterResultCallback) (gboolean is_enabled,
    gpointer user_data);

/**
 * EmpathyAccountChooserFilterFunc:
 * @account: a #TpAccount
 * @callback: an #EmpathyAccountChooserFilterResultCallback accepting the result
 * @callback_data: data passed to the @callback
 * @user_data: user data passed to the callback
 */
typedef void (* EmpathyAccountChooserFilterFunc) (
  TpAccount *account,
  EmpathyAccountChooserFilterResultCallback callback,
  gpointer callback_data,
  gpointer user_data);


typedef struct _EmpathyAccountChooser EmpathyAccountChooser;
typedef struct _EmpathyAccountChooserClass EmpathyAccountChooserClass;
typedef struct _EmpathyAccountChooserPriv EmpathyAccountChooserPriv;

struct _EmpathyAccountChooser
{
  GtkComboBox parent;

  /*<private>*/
  EmpathyAccountChooserPriv *priv;
};

struct _EmpathyAccountChooserClass
{
  GtkComboBoxClass parent_class;
};

GType empathy_account_chooser_get_type (void) G_GNUC_CONST;

GtkWidget * empathy_account_chooser_new (void);

TpAccount * empathy_account_chooser_dup_account (EmpathyAccountChooser *self);
TpAccount * empathy_account_chooser_get_account (EmpathyAccountChooser *self);

gboolean empathy_account_chooser_set_account (EmpathyAccountChooser *self,
    TpAccount *account);

TpConnection * empathy_account_chooser_get_connection (
    EmpathyAccountChooser *self);

void empathy_account_chooser_set_all (EmpathyAccountChooser *self);

TpAccountManager * empathy_account_chooser_get_account_manager (
    EmpathyAccountChooser *self);

gboolean empathy_account_chooser_get_has_all_option (
    EmpathyAccountChooser *self);

void empathy_account_chooser_set_has_all_option (EmpathyAccountChooser *self,
    gboolean has_all_option);

gboolean empathy_account_chooser_has_all_selected (EmpathyAccountChooser *self);

void empathy_account_chooser_set_filter (EmpathyAccountChooser *self,
    EmpathyAccountChooserFilterFunc filter,
    gpointer user_data);

gboolean empathy_account_chooser_is_ready (EmpathyAccountChooser *self);

void empathy_account_chooser_refilter (EmpathyAccountChooser *self);

/* Pre-defined filters */

void empathy_account_chooser_filter_is_connected (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data);

void empathy_account_chooser_filter_supports_chatrooms (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data);

G_END_DECLS

#endif /* __EMPATHY_ACCOUNT_CHOOSER_H__ */

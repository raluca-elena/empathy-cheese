/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2008-2010 Collabora Ltd.
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
 *          Travis Reitter <travis.reitter@collabora.co.uk>
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <telepathy-glib/util.h>

#include <folks/folks.h>
#include <folks/folks-telepathy.h>

#include <libempathy/empathy-camera-monitor.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-individual-manager.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-utils.h>

#include "empathy-account-selector-dialog.h"
#include "empathy-individual-menu.h"
#include "empathy-images.h"
#include "empathy-log-window.h"
#include "empathy-contact-dialogs.h"
#include "empathy-gtk-enum-types.h"
#include "empathy-individual-edit-dialog.h"
#include "empathy-individual-information-dialog.h"
#include "empathy-ui-utils.h"
#include "empathy-share-my-desktop.h"
#include "empathy-linking-dialog.h"
#include "empathy-call-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_CONTACT
#include <libempathy/empathy-debug.h>

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyIndividualMenu)

typedef struct {
  FolksIndividual *individual; /* owned */
  EmpathyIndividualFeatureFlags features;
} EmpathyIndividualMenuPriv;

enum {
  PROP_INDIVIDUAL = 1,
  PROP_FEATURES,
};

enum {
  SIGNAL_LINK_CONTACTS_ACTIVATED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE (EmpathyIndividualMenu, empathy_individual_menu, GTK_TYPE_MENU);

static void
individual_menu_add_personas (GtkMenuShell *menu,
    FolksIndividual *individual,
    EmpathyIndividualFeatureFlags features)
{
  GtkWidget *item;
  GeeSet *personas;
  GeeIterator *iter;
  guint persona_count = 0;
  gboolean c;

  g_return_if_fail (GTK_IS_MENU (menu));
  g_return_if_fail (FOLKS_IS_INDIVIDUAL (individual));
  g_return_if_fail (empathy_folks_individual_contains_contact (individual));

  personas = folks_individual_get_personas (individual);
  /* we'll re-use this iterator throughout */
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));

  /* Make sure we've got enough valid entries for these menu items to add
   * functionality */
  while (gee_iterator_next (iter))
    {
      FolksPersona *persona = gee_iterator_get (iter);
      if (empathy_folks_persona_is_interesting (persona))
        persona_count++;

      g_clear_object (&persona);
    }

  /* return early if these entries would add nothing beyond the "quick" items */
  if (persona_count <= 1)
    return;

  /* add a separator before the list of personas */
  item = gtk_separator_menu_item_new ();
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
  gtk_widget_show (item);

  personas = folks_individual_get_personas (individual);
  for (c = gee_iterator_first (iter); c; c = gee_iterator_next (iter))
    {
      GtkWidget *image;
      GtkWidget *contact_item;
      GtkWidget *contact_submenu;
      TpContact *tp_contact;
      EmpathyContact *contact;
      TpfPersona *persona = gee_iterator_get (iter);
      gchar *label;
      FolksPersonaStore *store;
      const gchar *account;
      GtkWidget *action;

      if (!empathy_folks_persona_is_interesting (FOLKS_PERSONA (persona)))
        goto while_finish;

      tp_contact = tpf_persona_get_contact (persona);
      if (tp_contact == NULL)
        goto while_finish;

      contact = empathy_contact_dup_from_tp_contact (tp_contact);

      store = folks_persona_get_store (FOLKS_PERSONA (persona));
      account = folks_persona_store_get_display_name (store);

      /* Translators: this is used in the context menu for a contact. The first
       * parameter is a contact ID (e.g. foo@jabber.org) and the second is one
       * of the user's account IDs (e.g. me@hotmail.com). */
      label = g_strdup_printf (_("%s (%s)"),
          folks_persona_get_display_id (FOLKS_PERSONA (persona)), account);

      contact_item = gtk_image_menu_item_new_with_label (label);
      gtk_image_menu_item_set_always_show_image (GTK_IMAGE_MENU_ITEM (contact_item),
                                                 TRUE);
      contact_submenu = gtk_menu_new ();
      gtk_menu_item_set_submenu (GTK_MENU_ITEM (contact_item), contact_submenu);
      image = gtk_image_new_from_icon_name (
          empathy_icon_name_for_contact (contact), GTK_ICON_SIZE_MENU);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (contact_item), image);
      gtk_widget_show (image);

      /* Chat */
      if (features & EMPATHY_INDIVIDUAL_FEATURE_CHAT)
        {
          action = empathy_individual_chat_menu_item_new (NULL, contact);
          gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
          gtk_widget_show (action);
        }

      /* SMS */
      if (features & EMPATHY_INDIVIDUAL_FEATURE_SMS)
        {
          action = empathy_individual_sms_menu_item_new (NULL, contact);
          gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
          gtk_widget_show (action);
        }

      if (features & EMPATHY_INDIVIDUAL_FEATURE_CALL)
        {
          /* Audio Call */
          action = empathy_individual_audio_call_menu_item_new (NULL, contact);
          gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
          gtk_widget_show (action);

          /* Video Call */
          action = empathy_individual_video_call_menu_item_new (NULL, contact);
          gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
          gtk_widget_show (action);
        }

      /* Log */
      if (features & EMPATHY_INDIVIDUAL_FEATURE_LOG)
        {
          action = empathy_individual_log_menu_item_new (NULL, contact);
          gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
          gtk_widget_show (action);
        }

      /* Invite */
      action = empathy_individual_invite_menu_item_new (NULL, contact);
      gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
      gtk_widget_show (action);

      /* File transfer */
      action = empathy_individual_file_transfer_menu_item_new (NULL, contact);
      gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
      gtk_widget_show (action);

      /* Share my desktop */
      action = empathy_individual_share_my_desktop_menu_item_new (NULL,
          contact);
      gtk_menu_shell_append (GTK_MENU_SHELL (contact_submenu), action);
      gtk_widget_show (action);

      gtk_menu_shell_append (GTK_MENU_SHELL (menu), contact_item);
      gtk_widget_show (contact_item);

      g_free (label);
      g_object_unref (contact);

while_finish:
      g_clear_object (&persona);
    }

  g_clear_object (&iter);
}

static void
individual_link_menu_item_activate_cb (EmpathyIndividualMenu *self)
{
  EmpathyIndividualMenuPriv *priv = GET_PRIV (self);
  GtkWidget *dialog;

  dialog = empathy_linking_dialog_show (priv->individual, NULL);
  g_signal_emit (self, signals[SIGNAL_LINK_CONTACTS_ACTIVATED], 0, dialog);
}

static void
empathy_individual_menu_init (EmpathyIndividualMenu *self)
{
  EmpathyIndividualMenuPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_INDIVIDUAL_MENU, EmpathyIndividualMenuPriv);

  self->priv = priv;
}

static GList *
find_phone_accounts (void)
{
  TpAccountManager *am;
  GList *accounts, *l;
  GList *found = NULL;

  am = tp_account_manager_dup ();
  g_return_val_if_fail (am != NULL, NULL);

  accounts = tp_account_manager_get_valid_accounts (am);
  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      TpAccount *account = l->data;

      if (tp_account_get_connection_status (account, NULL) !=
          TP_CONNECTION_STATUS_CONNECTED)
        continue;

      if (!empathy_account_has_uri_scheme_tel (account))
        continue;

      found = g_list_prepend (found, g_object_ref (account));
    }

  g_list_free (accounts);
  g_object_unref (am);

  return found;
}

static gboolean
has_phone_account (void)
{
  GList *accounts;
  gboolean result;

  accounts = find_phone_accounts ();
  result = (accounts != NULL);

  g_list_free_full (accounts, (GDestroyNotify) g_object_unref);

  return result;
}

static void
call_phone_number (FolksPhoneFieldDetails *details,
    TpAccount *account)
{
  DEBUG ("Try to call %s", folks_phone_field_details_get_normalised (details));

  empathy_call_new_with_streams (
      folks_phone_field_details_get_normalised (details),
      account, TRUE, FALSE, empathy_get_current_action_time ());
}

static void
display_call_phone_dialog (FolksPhoneFieldDetails *details,
    GList *accounts)
{
  GtkWidget *dialog;
  gint response;

  dialog = empathy_account_selector_dialog_new (accounts);

  gtk_window_set_title (GTK_WINDOW (dialog),
      _("Select account to use to place the call"));

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      _("Call"), GTK_RESPONSE_OK,
      NULL);

  response = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response == GTK_RESPONSE_OK)
    {
      TpAccount *account;

      account = empathy_account_selector_dialog_dup_selected (
           EMPATHY_ACCOUNT_SELECTOR_DIALOG (dialog));

      if (account != NULL)
        {
          call_phone_number (details, account);

          g_object_unref (account);
        }
    }

  gtk_widget_destroy (dialog);
}

static void
call_phone_number_cb (GtkMenuItem *item,
      FolksPhoneFieldDetails *details)
{
  GList *accounts;

  accounts = find_phone_accounts ();
  if (accounts == NULL)
    {
      DEBUG ("No phone aware account connected; can't call");
    }
  else if (g_list_length (accounts) == 1)
    {
      call_phone_number (details, accounts->data);
    }
  else
    {
      /* Ask which account to use */
      display_call_phone_dialog (details, accounts);
    }

  g_list_free_full (accounts, (GDestroyNotify) g_object_unref);
}

static const gchar *
find_phone_type (FolksPhoneFieldDetails *details)
{
  GeeCollection *types;
  GeeIterator *iter;

  types = folks_abstract_field_details_get_parameter_values (
      FOLKS_ABSTRACT_FIELD_DETAILS (details), "type");

  if (types == NULL)
    return NULL;

  iter = gee_iterable_iterator (GEE_ITERABLE (types));
  while (gee_iterator_next (iter))
    {
      const gchar *type = gee_iterator_get (iter);

      if (!tp_strdiff (type, "CELL"))
        return _("Mobile");
      else if (!tp_strdiff (type, "WORK"))
        return _("Work");
      else if (!tp_strdiff (type, "HOME"))
        return _("HOME");
    }

  return NULL;
}

static void
add_phone_numbers (EmpathyIndividualMenu *self)
{
  EmpathyIndividualMenuPriv *priv = GET_PRIV (self);
  GeeSet *all_numbers;
  GeeIterator *iter;
  gboolean sensitive;

  all_numbers = folks_phone_details_get_phone_numbers (
      FOLKS_PHONE_DETAILS (priv->individual));

  sensitive = has_phone_account ();

  iter = gee_iterable_iterator (GEE_ITERABLE (all_numbers));
  while (gee_iterator_next (iter))
    {
      FolksPhoneFieldDetails *details = gee_iterator_get (iter);
      GtkWidget *item, *image;
      gchar *tmp;
      const gchar *type;

      type = find_phone_type (details);

      if (type != NULL)
        {
          tmp = g_strdup_printf ("Call %s (%s)",
              folks_phone_field_details_get_normalised (details),
              type);
        }
      else
        {
          tmp = g_strdup_printf ("Call %s",
              folks_phone_field_details_get_normalised (details));
        }

      item = gtk_image_menu_item_new_with_mnemonic (tmp);
      g_free (tmp);

      g_signal_connect_data (item, "activate",
          G_CALLBACK (call_phone_number_cb), g_object_ref (details),
          (GClosureNotify) g_object_unref, 0);

      gtk_widget_set_sensitive (item, sensitive);

      image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_CALL,
          GTK_ICON_SIZE_MENU);
      gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
      gtk_widget_show (image);

      gtk_menu_shell_append (GTK_MENU_SHELL (self), item);
      gtk_widget_show (item);
    }
}

static void
constructed (GObject *object)
{
  EmpathyIndividualMenu *self = (EmpathyIndividualMenu *) object;
  EmpathyIndividualMenuPriv *priv = GET_PRIV (object);
  GtkMenuShell *shell;
  GtkWidget *item;
  FolksIndividual *individual;
  EmpathyIndividualFeatureFlags features;

  /* Build the menu */
  shell = GTK_MENU_SHELL (object);
  individual = priv->individual;
  features = priv->features;

  /* Chat */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_CHAT)
    {
      item = empathy_individual_chat_menu_item_new (individual, NULL);
      if (item != NULL)
        {
          gtk_menu_shell_append (shell, item);
          gtk_widget_show (item);
        }
    }

  /* SMS */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_SMS)
    {
      item = empathy_individual_sms_menu_item_new (individual, NULL);
      if (item != NULL)
        {
          gtk_menu_shell_append (shell, item);
          gtk_widget_show (item);
        }
    }

  if (features & EMPATHY_INDIVIDUAL_FEATURE_CALL)
    {
      /* Audio Call */
      item = empathy_individual_audio_call_menu_item_new (individual, NULL);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);

      /* Video Call */
      item = empathy_individual_video_call_menu_item_new (individual, NULL);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  if (features & EMPATHY_INDIVIDUAL_FEATURE_CALL_PHONE)
    add_phone_numbers (self);

  /* Invite */
  item = empathy_individual_invite_menu_item_new (individual, NULL);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* File transfer */
  item = empathy_individual_file_transfer_menu_item_new (individual, NULL);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* Share my desktop */
  /* FIXME we should add the "Share my desktop" menu item if Vino is
  a registered handler in MC5 */
  item = empathy_individual_share_my_desktop_menu_item_new (individual, NULL);
  gtk_menu_shell_append (shell, item);
  gtk_widget_show (item);

  /* Menu items to target specific contacts */
  individual_menu_add_personas (GTK_MENU_SHELL (object), individual, features);

  /* Separator */
  if (features & (EMPATHY_INDIVIDUAL_FEATURE_EDIT |
      EMPATHY_INDIVIDUAL_FEATURE_INFO |
      EMPATHY_INDIVIDUAL_FEATURE_FAVOURITE |
      EMPATHY_INDIVIDUAL_FEATURE_LINK))
    {
      item = gtk_separator_menu_item_new ();
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Edit */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_EDIT)
    {
      item = empathy_individual_edit_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Link */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_LINK)
    {
      item = empathy_individual_link_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);

      g_signal_connect_swapped (item, "activate",
          (GCallback) individual_link_menu_item_activate_cb, object);

      gtk_widget_show (item);
    }

  /* Log */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_LOG)
    {
      item = empathy_individual_log_menu_item_new (individual, NULL);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Info */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_INFO)
    {
      item = empathy_individual_info_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }

  /* Favorite checkbox */
  if (features & EMPATHY_INDIVIDUAL_FEATURE_FAVOURITE)
    {
      item = empathy_individual_favourite_menu_item_new (individual);
      gtk_menu_shell_append (shell, item);
      gtk_widget_show (item);
    }
}

static void
get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualMenuPriv *priv;

  priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        g_value_set_object (value, priv->individual);
        break;
      case PROP_FEATURES:
        g_value_set_flags (value, priv->features);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyIndividualMenuPriv *priv;

  priv = GET_PRIV (object);

  switch (param_id)
    {
      case PROP_INDIVIDUAL:
        priv->individual = g_value_dup_object (value);
        break;
      case PROP_FEATURES:
        priv->features = g_value_get_flags (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
        break;
    }
}

static void
dispose (GObject *object)
{
  EmpathyIndividualMenuPriv *priv = GET_PRIV (object);

  tp_clear_object (&priv->individual);

  G_OBJECT_CLASS (empathy_individual_menu_parent_class)->dispose (object);
}

static void
empathy_individual_menu_class_init (EmpathyIndividualMenuClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = constructed;
  object_class->get_property = get_property;
  object_class->set_property = set_property;
  object_class->dispose = dispose;

  /**
   * EmpathyIndividualMenu:individual:
   *
   * The #FolksIndividual the menu is for.
   */
  g_object_class_install_property (object_class, PROP_INDIVIDUAL,
      g_param_spec_object ("individual",
          "Individual",
          "The #FolksIndividual the menu is for.",
          FOLKS_TYPE_INDIVIDUAL,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  /**
   * EmpathyIndividualMenu:features:
   *
   * A set of feature flags controlling which entries are shown.
   */
  g_object_class_install_property (object_class, PROP_FEATURES,
      g_param_spec_flags ("features",
          "Features",
          "A set of feature flags controlling which entries are shown.",
          EMPATHY_TYPE_INDIVIDUAL_FEATURE_FLAGS,
          EMPATHY_INDIVIDUAL_FEATURE_NONE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_LINK_CONTACTS_ACTIVATED] =
      g_signal_new ("link-contacts-activated", G_OBJECT_CLASS_TYPE (klass),
          G_SIGNAL_RUN_LAST, 0, NULL, NULL,
          g_cclosure_marshal_generic,
          G_TYPE_NONE, 1, EMPATHY_TYPE_LINKING_DIALOG);

  g_type_class_add_private (object_class, sizeof (EmpathyIndividualMenuPriv));
}

GtkWidget *
empathy_individual_menu_new (FolksIndividual *individual,
    EmpathyIndividualFeatureFlags features)
{
  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);
  g_return_val_if_fail (features != EMPATHY_INDIVIDUAL_FEATURE_NONE, NULL);

  return g_object_new (EMPATHY_TYPE_INDIVIDUAL_MENU,
      "individual", individual,
      "features", features,
      NULL);
}

/* Like menu_item_set_first_contact(), but always operates upon the given
 * contact. If the contact is non-NULL, it is assumed that the menu entry should
 * be sensitive. */
static gboolean
menu_item_set_contact (GtkWidget *item,
    EmpathyContact *contact,
    GCallback activate_callback,
    EmpathyActionType action_type)
{
  gboolean can_do_action = FALSE;

  if (contact != NULL)
    can_do_action = empathy_contact_can_do_action (contact, action_type);
  gtk_widget_set_sensitive (item, can_do_action);

  if (can_do_action == TRUE)
    {
      /* We want to make sure that the EmpathyContact stays alive while the
       * signal is connected. */
      g_signal_connect_data (item, "activate", G_CALLBACK (activate_callback),
          g_object_ref (contact), (GClosureNotify) g_object_unref, 0);
    }

  return can_do_action;
}

/**
 * Set the given menu @item to call @activate_callback using the TpContact
 * (associated with @individual) with the highest availability who is also valid
 * whenever @item is activated.
 *
 * @action_type is the type of action performed by the menu entry; this is used
 * so that only contacts which can perform that action (e.g. are capable of
 * receiving video calls) are selected, as appropriate.
 */
static GtkWidget *
menu_item_set_first_contact (GtkWidget *item,
    FolksIndividual *individual,
    GCallback activate_callback,
    EmpathyActionType action_type)
{
  EmpathyContact *best_contact;

  best_contact = empathy_contact_dup_best_for_action (individual, action_type);
  menu_item_set_contact (item, best_contact, G_CALLBACK (activate_callback),
      action_type);
  tp_clear_object (&best_contact);

  return item;
}

static void
empathy_individual_chat_menu_item_activated (GtkMenuItem *item,
  EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_chat_with_contact (contact, empathy_get_current_action_time ());
}

GtkWidget *
empathy_individual_chat_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail ((FOLKS_IS_INDIVIDUAL (individual) &&
      empathy_folks_individual_contains_contact (individual)) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Chat"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_MESSAGE,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_chat_menu_item_activated),
          EMPATHY_ACTION_CHAT);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_chat_menu_item_activated),
          EMPATHY_ACTION_CHAT);
    }

  return item;
}

static void
empathy_individual_sms_menu_item_activated (GtkMenuItem *item,
  EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_sms_contact_id (
      empathy_contact_get_account (contact),
      empathy_contact_get_id (contact),
      empathy_get_current_action_time (),
      NULL, NULL);
}

GtkWidget *
empathy_individual_sms_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail ((FOLKS_IS_INDIVIDUAL (individual) &&
      empathy_folks_individual_contains_contact (individual)) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("_SMS"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_SMS,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_sms_menu_item_activated),
          EMPATHY_ACTION_SMS);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_sms_menu_item_activated),
          EMPATHY_ACTION_SMS);
    }

  return item;
}

static void
empathy_individual_audio_call_menu_item_activated (GtkMenuItem *item,
  EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_call_new_with_streams (empathy_contact_get_id (contact),
      empathy_contact_get_account (contact),
      TRUE, FALSE,
      empathy_get_current_action_time ());
}

GtkWidget *
empathy_individual_audio_call_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Audio Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VOIP, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_audio_call_menu_item_activated),
          EMPATHY_ACTION_AUDIO_CALL);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_audio_call_menu_item_activated),
          EMPATHY_ACTION_AUDIO_CALL);
    }

  return item;
}

static void
empathy_individual_video_call_menu_item_activated (GtkMenuItem *item,
  EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_call_new_with_streams (empathy_contact_get_id (contact),
      empathy_contact_get_account (contact),
      TRUE, TRUE,
      empathy_get_current_action_time ());
}

GtkWidget *
empathy_individual_video_call_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;
  EmpathyCameraMonitor *monitor;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (C_("menu item", "_Video Call"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_VIDEO_CALL,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_video_call_menu_item_activated),
          EMPATHY_ACTION_VIDEO_CALL);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_video_call_menu_item_activated),
          EMPATHY_ACTION_VIDEO_CALL);
    }

  /* Only follow available cameras if the contact can do Video calls */
  if (gtk_widget_get_sensitive (item))
    {
      monitor = empathy_camera_monitor_dup_singleton ();
      g_object_set_data_full (G_OBJECT (item),
          "monitor", monitor, g_object_unref);
      g_object_bind_property (monitor, "available", item, "sensitive",
          G_BINDING_SYNC_CREATE);
    }

  return item;
}

static void
empathy_individual_log_menu_item_activated (GtkMenuItem *item,
  EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_log_window_show (empathy_contact_get_account (contact),
      empathy_contact_get_id (contact), FALSE, NULL);
}

GtkWidget *
empathy_individual_log_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Previous Conversations"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_LOG, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_log_menu_item_activated),
          EMPATHY_ACTION_VIEW_LOGS);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_log_menu_item_activated),
          EMPATHY_ACTION_VIEW_LOGS);
    }

  return item;
}

static void
empathy_individual_file_transfer_menu_item_activated (GtkMenuItem *item,
    EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_send_file_with_file_chooser (contact);
}

GtkWidget *
empathy_individual_file_transfer_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Send File"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_DOCUMENT_SEND,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_file_transfer_menu_item_activated),
          EMPATHY_ACTION_SEND_FILE);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_file_transfer_menu_item_activated),
          EMPATHY_ACTION_SEND_FILE);
    }

  return item;
}

static void
empathy_individual_share_my_desktop_menu_item_activated (GtkMenuItem *item,
    EmpathyContact *contact)
{
  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  empathy_share_my_desktop_share_with_contact (contact);
}

GtkWidget *
empathy_individual_share_my_desktop_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Share My Desktop"));
  image = gtk_image_new_from_icon_name (GTK_STOCK_NETWORK, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  if (contact != NULL)
    {
      menu_item_set_contact (item, contact,
          G_CALLBACK (empathy_individual_share_my_desktop_menu_item_activated),
          EMPATHY_ACTION_SHARE_MY_DESKTOP);
    }
  else
    {
      menu_item_set_first_contact (item, individual,
          G_CALLBACK (empathy_individual_share_my_desktop_menu_item_activated),
          EMPATHY_ACTION_SHARE_MY_DESKTOP);
    }

  return item;
}

static void
favourite_menu_item_toggled_cb (GtkCheckMenuItem *item,
  FolksIndividual *individual)
{
  folks_favourite_details_set_is_favourite (
      FOLKS_FAVOURITE_DETAILS (individual),
      gtk_check_menu_item_get_active (item));
}

GtkWidget *
empathy_individual_favourite_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;

  item = gtk_check_menu_item_new_with_label (_("Favorite"));

  gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
      folks_favourite_details_get_is_favourite (
          FOLKS_FAVOURITE_DETAILS (individual)));

  g_signal_connect (item, "toggled",
      G_CALLBACK (favourite_menu_item_toggled_cb), individual);

  return item;
}

static void
individual_info_menu_item_activate_cb (FolksIndividual *individual)
{
  empathy_individual_information_dialog_show (individual, NULL);
}

GtkWidget *
empathy_individual_info_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  GtkWidget *image;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);
  g_return_val_if_fail (empathy_folks_individual_contains_contact (individual),
      NULL);

  item = gtk_image_menu_item_new_with_mnemonic (_("Infor_mation"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_CONTACT_INFORMATION,
                GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  g_signal_connect_swapped (item, "activate",
          G_CALLBACK (individual_info_menu_item_activate_cb),
          individual);

  return item;
}

static void
individual_edit_menu_item_activate_cb (FolksIndividual *individual)
{
  empathy_individual_edit_dialog_show (individual, NULL);
}

GtkWidget *
empathy_individual_edit_menu_item_new (FolksIndividual *individual)
{
  EmpathyIndividualManager *manager;
  GtkWidget *item;
  GtkWidget *image;
  gboolean enable = FALSE;
  EmpathyContact *contact;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  contact = empathy_contact_dup_from_folks_individual (individual);

  g_return_val_if_fail (EMPATHY_IS_CONTACT (contact), NULL);

  if (empathy_individual_manager_initialized ())
    {
      TpConnection *connection;

      manager = empathy_individual_manager_dup_singleton ();
      connection = empathy_contact_get_connection (contact);

      enable = (empathy_connection_can_alias_personas (connection,
						       individual) &&
                empathy_connection_can_group_personas (connection, individual));

      g_object_unref (manager);
    }

  item = gtk_image_menu_item_new_with_mnemonic (
      C_("Edit individual (contextual menu)", "_Edit"));
  image = gtk_image_new_from_icon_name (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);

  gtk_widget_set_sensitive (item, enable);

  g_signal_connect_swapped (item, "activate",
      G_CALLBACK (individual_edit_menu_item_activate_cb), individual);

  g_object_unref (contact);

  return item;
}

GtkWidget *
empathy_individual_link_menu_item_new (FolksIndividual *individual)
{
  GtkWidget *item;
  /*GtkWidget *image;*/

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual), NULL);

  item = gtk_image_menu_item_new_with_mnemonic (
      /* Translators: this is a verb meaning "to connect two contacts together
       * to form a meta-contact". */
      C_("Link individual (contextual menu)", "_Link Contacts…"));
  /* TODO */
  /*image = gtk_image_new_from_icon_name (GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);
  gtk_widget_show (image);*/

  /* Only allow trusted Individuals to be linked */
  gtk_widget_set_sensitive (item,
      folks_individual_get_trust_level (individual) ==
          FOLKS_TRUST_LEVEL_PERSONAS);

  return item;
}

typedef struct
{
  FolksIndividual *individual;
  EmpathyContact *contact;
  EmpathyChatroom *chatroom;
} RoomSubMenuData;

static RoomSubMenuData *
room_sub_menu_data_new (FolksIndividual *individual,
    EmpathyContact *contact,
    EmpathyChatroom *chatroom)
{
  RoomSubMenuData *data;

  data = g_slice_new0 (RoomSubMenuData);
  if (individual != NULL)
    data->individual = g_object_ref (individual);
  if (contact != NULL)
    data->contact = g_object_ref (contact);
  data->chatroom = g_object_ref (chatroom);

  return data;
}

static void
room_sub_menu_data_free (RoomSubMenuData *data)
{
  tp_clear_object (&data->individual);
  tp_clear_object (&data->contact);
  g_object_unref (data->chatroom);
  g_slice_free (RoomSubMenuData, data);
}

static void
room_sub_menu_activate_cb (GtkWidget *item,
         RoomSubMenuData *data)
{
  EmpathyTpChat *chat;
  EmpathyChatroomManager *mgr;
  EmpathyContact *contact = NULL;

  chat = empathy_chatroom_get_tp_chat (data->chatroom);
  if (chat == NULL)
    {
      /* channel was invalidated. Ignoring */
      return;
    }

  mgr = empathy_chatroom_manager_dup_singleton (NULL);

  if (data->contact != NULL)
    contact = g_object_ref (data->contact);
  else
    {
      GeeSet *personas;
      GeeIterator *iter;

      /* find the first of this Individual's contacts who can join this room */
      personas = folks_individual_get_personas (data->individual);

      iter = gee_iterable_iterator (GEE_ITERABLE (personas));
      while (gee_iterator_next (iter) && (contact == NULL))
        {
          TpfPersona *persona = gee_iterator_get (iter);
          TpContact *tp_contact;
          GList *rooms;

          if (empathy_folks_persona_is_interesting (FOLKS_PERSONA (persona)))
            {
              tp_contact = tpf_persona_get_contact (persona);
              if (tp_contact != NULL)
                {
                  contact = empathy_contact_dup_from_tp_contact (tp_contact);

                  rooms = empathy_chatroom_manager_get_chatrooms (mgr,
                      empathy_contact_get_account (contact));

                  if (g_list_find (rooms, data->chatroom) == NULL)
                    g_clear_object (&contact);

                  /* if contact != NULL here, we've found our match */

                  g_list_free (rooms);
                }
            }
          g_clear_object (&persona);
        }
      g_clear_object (&iter);
    }

  g_object_unref (mgr);

  if (contact == NULL)
    {
      /* contact disappeared. Ignoring */
      goto out;
    }

  g_return_if_fail (EMPATHY_IS_CONTACT (contact));

  /* send invitation */
  empathy_contact_list_add (EMPATHY_CONTACT_LIST (chat),
      contact, _("Inviting you to this room"));

out:
  g_object_unref (contact);
}

static GtkWidget *
create_room_sub_menu (FolksIndividual *individual,
                      EmpathyContact *contact,
                      EmpathyChatroom *chatroom)
{
  GtkWidget *item;
  RoomSubMenuData *data;

  item = gtk_menu_item_new_with_label (empathy_chatroom_get_name (chatroom));
  data = room_sub_menu_data_new (individual, contact, chatroom);
  g_signal_connect_data (item, "activate",
      G_CALLBACK (room_sub_menu_activate_cb), data,
      (GClosureNotify) room_sub_menu_data_free, 0);

  return item;
}

GtkWidget *
empathy_individual_invite_menu_item_new (FolksIndividual *individual,
    EmpathyContact *contact)
{
  GtkWidget *item;
  GtkWidget *image;
  GtkWidget *room_item;
  EmpathyChatroomManager *mgr;
  GList *rooms = NULL;
  GList *names = NULL;
  GList *l;
  GtkWidget *submenu = NULL;
  /* map of chat room names to their objects; just a utility to remove
   * duplicates and to make construction of the alphabetized list easier */
  GHashTable *name_room_map;

  g_return_val_if_fail (FOLKS_IS_INDIVIDUAL (individual) ||
      EMPATHY_IS_CONTACT (contact),
      NULL);

  name_room_map = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      g_object_unref);

  item = gtk_image_menu_item_new_with_mnemonic (_("_Invite to Chat Room"));
  image = gtk_image_new_from_icon_name (EMPATHY_IMAGE_GROUP_MESSAGE,
      GTK_ICON_SIZE_MENU);
  gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (item), image);

  mgr = empathy_chatroom_manager_dup_singleton (NULL);

  if (contact != NULL)
    {
      rooms = empathy_chatroom_manager_get_chatrooms (mgr,
          empathy_contact_get_account (contact));
    }
  else
    {
      GeeSet *personas;
      GeeIterator *iter;

      /* find the first of this Individual's contacts who can join this room */
      personas = folks_individual_get_personas (individual);
      iter = gee_iterable_iterator (GEE_ITERABLE (personas));
      while (gee_iterator_next (iter))
        {
          TpfPersona *persona = gee_iterator_get (iter);
          GList *rooms_cur;
          TpContact *tp_contact;
          EmpathyContact *contact_cur;

          if (empathy_folks_persona_is_interesting (FOLKS_PERSONA (persona)))
            {
              tp_contact = tpf_persona_get_contact (persona);
              if (tp_contact != NULL)
                {
                  contact_cur = empathy_contact_dup_from_tp_contact (
                      tp_contact);

                  rooms_cur = empathy_chatroom_manager_get_chatrooms (mgr,
                      empathy_contact_get_account (contact_cur));
                  rooms = g_list_concat (rooms, rooms_cur);

                  g_object_unref (contact_cur);
                }
            }
          g_clear_object (&persona);
        }
      g_clear_object (&iter);
    }

  /* alphabetize the rooms */
  for (l = rooms; l != NULL; l = g_list_next (l))
    {
      EmpathyChatroom *chatroom = l->data;
      gboolean existed;

      if (empathy_chatroom_get_tp_chat (chatroom) != NULL)
        {
          const gchar *name;

          name = empathy_chatroom_get_name (chatroom);
          existed = (g_hash_table_lookup (name_room_map, name) != NULL);
          g_hash_table_insert (name_room_map, (gpointer) name,
              g_object_ref (chatroom));

          /* this will take care of duplicates in rooms */
          if (!existed)
            {
              names = g_list_insert_sorted (names, (gpointer) name,
                  (GCompareFunc) g_strcmp0);
            }
        }
    }

  for (l = names; l != NULL; l = g_list_next (l))
    {
      const gchar *name = l->data;
      EmpathyChatroom *chatroom;

      if (G_UNLIKELY (submenu == NULL))
        submenu = gtk_menu_new ();

      chatroom = g_hash_table_lookup (name_room_map, name);
      room_item = create_room_sub_menu (individual, contact, chatroom);
      gtk_menu_shell_append ((GtkMenuShell *) submenu, room_item);
      gtk_widget_show (room_item);
    }

  if (submenu)
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
  else
    gtk_widget_set_sensitive (item, FALSE);

  gtk_widget_show (image);

  g_hash_table_unref (name_room_map);
  g_object_unref (mgr);
  g_list_free (names);
  g_list_free (rooms);

  return item;
}

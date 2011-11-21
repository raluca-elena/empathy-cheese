/*
 * Copyright (C) 2009 Collabora Ltd.
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
 * Authors: Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
 */

/* empathy-account-assistant.c */

#include <config.h>
#include <glib/gi18n-lib.h>
#include <telepathy-glib/util.h>
#include <gdk/gdkkeysyms.h>

#include "empathy-account-assistant.h"
#include "empathy-import-widget.h"
#include "empathy-import-utils.h"
#include "empathy-auto-salut-account-helper.h"

#include <libempathy/empathy-account-settings.h>
#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-account-widget.h>
#include <libempathy-gtk/empathy-protocol-chooser.h>
#include <libempathy-gtk/empathy-ui-utils.h>

#define DEBUG_FLAG EMPATHY_DEBUG_ACCOUNT
#include <libempathy/empathy-debug.h>

G_DEFINE_TYPE (EmpathyAccountAssistant, empathy_account_assistant,
    GTK_TYPE_ASSISTANT)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyAccountAssistant)

typedef enum {
  RESPONSE_IMPORT = 1,
  RESPONSE_ENTER_ACCOUNT = 2,
  RESPONSE_CREATE_ACCOUNT = 3,
  RESPONSE_SALUT_ONLY = 4
} FirstPageResponse;

typedef enum {
  RESPONSE_CREATE_AGAIN = 1,
  RESPONSE_CREATE_STOP = 2
} CreateEnterPageResponse;

typedef enum {
  PAGE_INTRO = 0,
  PAGE_IMPORT = 1,
  PAGE_ENTER_CREATE = 2,
  PAGE_SALUT = 3,
} PageID;

enum {
  PROP_PARENT = 1,
  PROP_CONNECTION_MGRS,
};

typedef struct {
  FirstPageResponse first_resp;
  CreateEnterPageResponse create_enter_resp;
  gboolean enter_create_forward;
  TpAccountManager *account_mgr;
  EmpathyConnectionManagers *connection_mgrs;
  PageID current_page_id;

  /* enter or create page */
  GtkWidget *enter_or_create_page;
  GtkWidget *current_account_widget;
  EmpathyAccountWidget *current_widget_object;
  GtkWidget *first_label;
  GtkWidget *second_label;
  GtkWidget *chooser;
  GtkWidget *create_again_radio;
  EmpathyAccountSettings *settings;
  gboolean is_creating;

  /* import page */
  EmpathyImportWidget *iw;
  GtkWidget *import_page;

  /* salut page */
  GtkWidget *salut_page;
  EmpathyAccountSettings *salut_settings;
  GtkWidget *salut_account_widget;
  gboolean create_salut_account;
  gboolean display_salut_page;

  GtkWindow *parent_window;

  gboolean dispose_run;
} EmpathyAccountAssistantPriv;

static GtkWidget * account_assistant_build_enter_or_create_page (
    EmpathyAccountAssistant *self);
static void account_assistant_finish_enter_or_create_page (
    EmpathyAccountAssistant *self,
    gboolean is_enter);

static void do_constructed (GObject *object);

static GtkWidget *
build_error_vbox (const gchar *primary_message,
    const gchar *secondary_message)
{
  GtkWidget *main_vbox, *w, *hbox;
  PangoAttrList *list;

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  w = gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR,
      GTK_ICON_SIZE_DIALOG);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  w = gtk_label_new (primary_message);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  list = pango_attr_list_new ();
  pango_attr_list_insert (list, pango_attr_scale_new (PANGO_SCALE_LARGE));
  pango_attr_list_insert (list, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
  gtk_label_set_attributes (GTK_LABEL (w), list);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_widget_show (w);

  pango_attr_list_unref (list);

  w = gtk_label_new (secondary_message);
  gtk_label_set_use_markup (GTK_LABEL (w), TRUE);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_widget_show (w);

  return main_vbox;
}

static GtkWidget *
account_assistant_build_error_page (EmpathyAccountAssistant *self,
    GError *error, gint page_num)
{
  GtkWidget *main_vbox, *w;
  const char *primary_message;
  gchar *secondary_message, *markup;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  if (page_num == PAGE_IMPORT)
    primary_message = _("There was an error while importing the accounts.");
  else if (page_num >= PAGE_ENTER_CREATE &&
      priv->first_resp == RESPONSE_ENTER_ACCOUNT)
    primary_message = _("There was an error while parsing the account details.");
  else if (page_num >= PAGE_ENTER_CREATE &&
      priv->first_resp == RESPONSE_CREATE_ACCOUNT)
    primary_message = _("There was an error while creating the account.");
  else
    primary_message = _("There was an error.");

  markup = g_markup_printf_escaped ("<span style=\"italic\">%s</span>",
      error->message);
  secondary_message = g_strdup_printf (_("The error message was: %s"), markup);

  main_vbox = build_error_vbox (primary_message, secondary_message);

  w = gtk_label_new (_("You can either go back and try to enter your "
          "accounts' details again or quit this assistant and add accounts "
          "later from the Edit menu."));
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 6);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_widget_show (w);

  g_free (markup);
  g_free (secondary_message);
  return main_vbox;
}

static void
account_assistant_back_button_clicked_cb (GtkButton *button,
    EmpathyAccountAssistant *self)
{
  gint page_num;

  page_num = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
          "page-num"));
  gtk_assistant_remove_action_widget (GTK_ASSISTANT (self),
      GTK_WIDGET (button));
  gtk_assistant_set_current_page (GTK_ASSISTANT (self), page_num);
}

static void
account_assistant_present_error_page (EmpathyAccountAssistant *self,
    GError *error, gint page_num)
{
  GtkWidget *error_page, *back_button;
  gint num;

  error_page = account_assistant_build_error_page (self, error,
      page_num);
  num = gtk_assistant_append_page (GTK_ASSISTANT (self), error_page);
  gtk_assistant_set_page_title (GTK_ASSISTANT (self), error_page,
      _("An error occurred"));
  gtk_assistant_set_page_type (GTK_ASSISTANT (self), error_page,
      GTK_ASSISTANT_PAGE_SUMMARY);

  back_button = gtk_button_new_from_stock (GTK_STOCK_GO_BACK);
  gtk_assistant_add_action_widget (GTK_ASSISTANT (self), back_button);
  g_object_set_data (G_OBJECT (back_button),
      "page-num", GINT_TO_POINTER (page_num));
  g_signal_connect (back_button, "clicked",
      G_CALLBACK (account_assistant_back_button_clicked_cb), self);
  gtk_widget_show (back_button);

  gtk_assistant_set_current_page (GTK_ASSISTANT (self), num);
}

static void
update_create_page_buttons (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkAssistantPageType type;

  if (priv->display_salut_page ||
      priv->create_enter_resp == RESPONSE_CREATE_AGAIN)
    type = GTK_ASSISTANT_PAGE_CONTENT;
  else
    type = GTK_ASSISTANT_PAGE_CONFIRM;

  gtk_assistant_set_page_type (GTK_ASSISTANT (self), priv->enter_or_create_page,
      type);
}

static void
account_assistant_reset_enter_create_page (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *page;
  gint idx;

  page = account_assistant_build_enter_or_create_page (self);
  idx = gtk_assistant_append_page (GTK_ASSISTANT (self), page);
  priv->enter_or_create_page = page;
  update_create_page_buttons (self);

  gtk_assistant_set_current_page (GTK_ASSISTANT (self), idx);

  account_assistant_finish_enter_or_create_page (self,
      priv->first_resp == RESPONSE_ENTER_ACCOUNT ?
      TRUE : FALSE);
}

static void
account_assistant_account_enabled_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  const gchar *protocol;
  TpAccount *account = TP_ACCOUNT (source);
  gint current_idx;
  gboolean salut_created = FALSE;

  tp_account_set_enabled_finish (account, result, &error);

  if (error)
    {
      g_warning ("Error enabling an account: %s", error->message);
      g_error_free (error);
    }

  protocol = tp_account_get_protocol (account);
  if (!tp_strdiff (protocol, "local-xmpp"))
    {
      salut_created = TRUE;
    }

  empathy_connect_new_account (account, priv->account_mgr);

  current_idx = gtk_assistant_get_current_page (GTK_ASSISTANT (self));
  if (current_idx == PAGE_SALUT && !salut_created)
    /* We are on the Salut page and aren't creating the salut account so don't
     * terminate the assistant. */
    return;

  if (priv->create_enter_resp == RESPONSE_CREATE_STOP)
    g_signal_emit_by_name (self, "close");
  else
    account_assistant_reset_enter_create_page (self);
}

static void
account_assistant_apply_account_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  GError *error = NULL;
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  EmpathyAccountSettings *settings = EMPATHY_ACCOUNT_SETTINGS (source);
  TpAccount *account;

  empathy_account_settings_apply_finish (settings, result, NULL, &error);

  priv->is_creating = FALSE;

  if (error != NULL)
    {
      account_assistant_present_error_page (self, error,
          gtk_assistant_get_current_page (GTK_ASSISTANT (self)));
      g_error_free (error);
      return;
    }

  /* enable the newly created account */
  account = empathy_account_settings_get_account (settings);
  tp_account_set_enabled_async (account, TRUE,
      account_assistant_account_enabled_cb, self);
}

static void
account_assistant_apply_account_and_finish (EmpathyAccountAssistant *self,
    EmpathyAccountSettings *settings,
    gboolean set_display_name)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  if (settings == NULL)
    return;

  priv->is_creating = TRUE;

  if (set_display_name)
    {
      gchar *display_name;

      display_name = empathy_account_widget_get_default_display_name (
        priv->current_widget_object);

      empathy_account_settings_set_display_name_async (settings,
        display_name, NULL, NULL);

      g_free (display_name);
    }

  empathy_account_settings_apply_async (settings,
      account_assistant_apply_account_cb, self);
}

static void
account_assistant_handle_apply_cb (EmpathyAccountWidget *widget_object,
    gboolean is_valid,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
      priv->enter_or_create_page, is_valid);
}

static void
account_assistant_protocol_changed_cb (GtkComboBox *chooser,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountSettings *settings;
  EmpathyAccountAssistantPriv *priv;
  GtkWidget *account_widget;
  EmpathyAccountWidget *widget_object = NULL;

  priv = GET_PRIV (self);

  settings = empathy_protocol_chooser_create_account_settings (
      EMPATHY_PROTOCOL_CHOOSER (chooser));

  if (settings == NULL)
    return;

  if (priv->first_resp == RESPONSE_CREATE_ACCOUNT)
    empathy_account_settings_set_boolean (settings, "register", TRUE);

  widget_object = empathy_account_widget_new_for_protocol (settings, TRUE);
  account_widget = empathy_account_widget_get_widget (widget_object);

  if (priv->current_account_widget != NULL)
    {
      g_signal_handlers_disconnect_by_func (priv->current_widget_object,
          account_assistant_handle_apply_cb, self);
      gtk_widget_destroy (priv->current_account_widget);
    }

  priv->current_account_widget = account_widget;
  priv->current_widget_object = widget_object;

  if (priv->settings != NULL)
    g_object_unref (priv->settings);

  priv->settings = settings;

  g_signal_connect (priv->current_widget_object, "handle-apply",
      G_CALLBACK (account_assistant_handle_apply_cb), self);

  if (empathy_account_settings_is_valid (settings))
    {
      gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
          priv->enter_or_create_page, TRUE);
    }

  gtk_box_pack_start (GTK_BOX (priv->enter_or_create_page), account_widget,
      FALSE, FALSE, 0);
  gtk_widget_show (account_widget);
}

static gboolean
account_assistant_chooser_enter_details_filter_func (
    TpConnectionManager *cm,
    TpConnectionManagerProtocol *protocol,
    const gchar *service,
    gpointer user_data)
{
  if (!tp_strdiff (protocol->name, "local-xmpp"))
    return FALSE;

  return TRUE;
}

static gboolean
account_assistant_chooser_create_account_filter_func (
    TpConnectionManager *cm,
    TpConnectionManagerProtocol *protocol,
    const gchar *service,
    gpointer user_data)
{
  if (service != NULL)
    return FALSE;

  return tp_connection_manager_protocol_can_register (protocol);
}

static void
account_assistant_finish_enter_or_create_page (EmpathyAccountAssistant *self,
    gboolean is_enter)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  if (is_enter)
    {
      gtk_label_set_label (GTK_LABEL (priv->first_label),
          _("What kind of chat account do you have?"));
      /*      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("If you have other accounts to set up, you can do "
              "that at any time from the Edit menu."));
      */
      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("Do you have any other chat accounts you want to set up?"));
      empathy_protocol_chooser_set_visible (
          EMPATHY_PROTOCOL_CHOOSER (priv->chooser),
          account_assistant_chooser_enter_details_filter_func, self);

      gtk_assistant_set_page_title (GTK_ASSISTANT (self),
          priv->enter_or_create_page, _("Enter your account details"));
    }
  else
    {
      gtk_label_set_label (GTK_LABEL (priv->first_label),
          _("What kind of chat account do you want to create?"));
      /*      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("You can register other accounts, or setup "
              "an existing one at any time from the Edit menu."));
      */
      gtk_label_set_label (GTK_LABEL (priv->second_label),
          _("Do you want to create other chat accounts?"));
      empathy_protocol_chooser_set_visible (
          EMPATHY_PROTOCOL_CHOOSER (priv->chooser),
          account_assistant_chooser_create_account_filter_func, self);

      gtk_assistant_set_page_title (GTK_ASSISTANT (self),
          priv->enter_or_create_page,
          _("Enter the details for the new account"));
    }

  g_signal_connect (priv->chooser, "changed",
      G_CALLBACK (account_assistant_protocol_changed_cb), self);

  /* trigger show the first account widget */
  account_assistant_protocol_changed_cb (GTK_COMBO_BOX (priv->chooser), self);
}

static gint
account_assistant_page_forward_func (gint current_page,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint retval;

  retval = current_page;

  if (current_page == PAGE_INTRO)
    {
      if (priv->first_resp == RESPONSE_ENTER_ACCOUNT ||
          priv->first_resp == RESPONSE_CREATE_ACCOUNT)
        retval = PAGE_ENTER_CREATE;
      else if (priv->first_resp == RESPONSE_IMPORT)
        retval = PAGE_IMPORT;
      else if (priv->first_resp == RESPONSE_SALUT_ONLY)
        retval = PAGE_SALUT;
    }
  else if (current_page == PAGE_IMPORT)
    {
      if (priv->display_salut_page)
        retval = PAGE_SALUT;
      else
        /* Don't go forward */
        retval = -1;
    }
  else if (current_page == PAGE_SALUT)
    {
      /* Don't go forward */
      retval = -1;
    }
  else if (current_page >= PAGE_ENTER_CREATE)
    {
      if (priv->create_enter_resp == RESPONSE_CREATE_AGAIN)
        {
          priv->enter_create_forward = TRUE;
          retval = current_page;
        }
      else if (priv->display_salut_page)
        {
          retval = PAGE_SALUT;
        }
      else
        {
          /* Don't go forward */
          retval = -1;
        }
    }

  return retval;
}

static void
update_intro_page_buttons (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *intro_page;

  intro_page = gtk_assistant_get_nth_page (GTK_ASSISTANT (self),
      PAGE_INTRO);

  if (priv->first_resp == RESPONSE_SALUT_ONLY &&
      !priv->display_salut_page)
    gtk_assistant_set_page_type (GTK_ASSISTANT (self), intro_page,
        GTK_ASSISTANT_PAGE_SUMMARY);
  else
    gtk_assistant_set_page_type (GTK_ASSISTANT (self), intro_page,
        GTK_ASSISTANT_PAGE_INTRO);
}

static void
account_assistant_radio_choice_toggled_cb (GtkToggleButton *button,
    EmpathyAccountAssistant *self)
{
  FirstPageResponse response;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  response = GPOINTER_TO_INT (g_object_get_data
      (G_OBJECT (button), "response"));

  priv->first_resp = response;

  update_intro_page_buttons (self);
}

static GtkWidget *
account_assistant_build_introduction_page (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *main_vbox, *hbox_1, *w, *vbox_1;
  GtkWidget *radio = NULL;
  GdkPixbuf *pix;
  const gchar *str;

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_show (main_vbox);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);

  hbox_1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox_1, TRUE, TRUE, 0);
  gtk_widget_show (hbox_1);

  w = gtk_label_new (
      _("With Empathy you can chat with people "
        "online nearby and with friends and colleagues "
        "who use Google Talk, AIM, Windows Live "
        "and many other chat programs. With a microphone "
        "or a webcam you can also have audio or video calls."));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  pix = empathy_pixbuf_from_icon_name_sized ("empathy", 80);
  w = gtk_image_new_from_pixbuf (pix);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, FALSE, FALSE, 6);
  gtk_widget_show (w);

  g_object_unref (pix);

  w = gtk_label_new (_("Do you have an account you've been using "
          "with another chat program?"));
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  vbox_1 = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_set_homogeneous (GTK_BOX (vbox_1), TRUE);
  gtk_container_add (GTK_CONTAINER (w), vbox_1);
  gtk_widget_show (vbox_1);

  if (empathy_import_accounts_to_import ())
    {
      hbox_1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
      gtk_box_pack_start (GTK_BOX (vbox_1), hbox_1, TRUE, TRUE, 0);
      gtk_widget_show (hbox_1);

      radio = gtk_radio_button_new_with_label (NULL,
          _("Yes, import my account details from "));
      gtk_box_pack_start (GTK_BOX (hbox_1), radio, TRUE, TRUE, 0);
      g_object_set_data (G_OBJECT (radio), "response",
          GINT_TO_POINTER (RESPONSE_IMPORT));
      gtk_widget_show (radio);

      w = gtk_combo_box_text_new ();
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (w), "Pidgin");
      gtk_box_pack_start (GTK_BOX (hbox_1), w, TRUE, TRUE, 0);
      gtk_combo_box_set_active (GTK_COMBO_BOX (w), 0);
      gtk_widget_show (w);

      g_signal_connect (radio, "clicked",
          G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);
      priv->first_resp = RESPONSE_IMPORT;
    }
  else
    {
      priv->first_resp = RESPONSE_ENTER_ACCOUNT;
    }

  str = _("Yes, I'll enter my account details now");

  if (radio == NULL)
    {
      radio = gtk_radio_button_new_with_label (NULL, str);
      w = radio;
    }
  else
    {
      w = gtk_radio_button_new_with_label_from_widget (
          GTK_RADIO_BUTTON (radio), str);
    }

  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_ENTER_ACCOUNT));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("No, I want a new account"));
  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_CREATE_ACCOUNT));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("No, I just want to see people online nearby for now"));
  gtk_box_pack_start (GTK_BOX (vbox_1), w, TRUE, TRUE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_SALUT_ONLY));
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_choice_toggled_cb), self);

  return main_vbox;
}

static GtkWidget *
account_assistant_build_import_page (EmpathyAccountAssistant *self)
{
  GtkWidget *main_vbox, *w, *import;
  EmpathyImportWidget *iw;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  w = gtk_label_new (_("Select the accounts you want to import:"));
  gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
  gtk_widget_show (w);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 6);

  w = gtk_alignment_new (0, 0, 1, 1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  /* NOTE: this is hardcoded as we support pidgin only */
  iw = empathy_import_widget_new (EMPATHY_IMPORT_APPLICATION_PIDGIN);
  import = empathy_import_widget_get_widget (iw);
  gtk_container_add (GTK_CONTAINER (w), import);
  gtk_widget_show (import);

  priv->iw = iw;

  gtk_widget_show (main_vbox);

  return main_vbox;
}

static void
account_assistant_radio_create_again_clicked_cb (GtkButton *button,
    EmpathyAccountAssistant *self)
{
  CreateEnterPageResponse response;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  response = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (button),
          "response"));

  priv->create_enter_resp = response;

  update_create_page_buttons (self);
}

static GtkWidget *
account_assistant_build_enter_or_create_page (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *main_vbox, *w, *chooser, *vbox, *hbox, *radio;

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);
  gtk_widget_show (main_vbox);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  w = gtk_label_new (NULL);
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);
  priv->first_label = w;

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  chooser = empathy_protocol_chooser_new ();
  gtk_box_pack_start (GTK_BOX (hbox), chooser, FALSE, FALSE, 0);
  gtk_widget_show (chooser);
  priv->chooser = chooser;

  vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
  gtk_box_pack_end (GTK_BOX (main_vbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  w = gtk_label_new (NULL);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_misc_set_alignment (GTK_MISC (w), 0.0, 0.5);
  gtk_widget_show (w);
  priv->second_label = w;

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (vbox), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_container_add (GTK_CONTAINER (w), hbox);
  gtk_widget_show (hbox);

  radio = gtk_radio_button_new_with_label (NULL, _("Yes"));
  gtk_box_pack_start (GTK_BOX (hbox), radio, FALSE, FALSE, 0);
  g_object_set_data (G_OBJECT (radio), "response",
      GINT_TO_POINTER (RESPONSE_CREATE_AGAIN));
  gtk_widget_show (radio);

  w = gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON (radio),
      _("No, that's all for now"));
  gtk_box_pack_start (GTK_BOX (hbox), w, FALSE, FALSE, 0);
  g_object_set_data (G_OBJECT (w), "response",
      GINT_TO_POINTER (RESPONSE_CREATE_STOP));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (w), TRUE);
  priv->create_enter_resp = RESPONSE_CREATE_STOP;
  priv->create_again_radio = w;
  gtk_widget_show (w);

  g_signal_connect (w, "clicked",
      G_CALLBACK (account_assistant_radio_create_again_clicked_cb), self);
  g_signal_connect (radio, "clicked",
      G_CALLBACK (account_assistant_radio_create_again_clicked_cb), self);

  return main_vbox;
}

static void
account_assistant_close_cb (GtkAssistant *assistant,
    gpointer user_data)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (assistant);

  if (priv->is_creating)
    return;

  gtk_widget_destroy (GTK_WIDGET (assistant));
}

static void
impl_signal_apply (GtkAssistant *assistant)
{
  EmpathyAccountAssistant *self = EMPATHY_ACCOUNT_ASSISTANT (assistant);
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint current_page;

  current_page = gtk_assistant_get_current_page (assistant);

  if (current_page == PAGE_SALUT)
    {
      if (priv->create_salut_account)
        /* create_salut_account_settings() already set the display name of the
         * account so there is no need to set it again. */
        account_assistant_apply_account_and_finish (self, priv->salut_settings,
            FALSE);
      return;
    }
  else if (current_page >= PAGE_ENTER_CREATE &&
      priv->settings != NULL &&
      empathy_account_settings_is_valid (priv->settings))
    {
      account_assistant_apply_account_and_finish (self, priv->settings, TRUE);
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }
  else if (current_page == PAGE_IMPORT)
    {
      empathy_import_widget_add_selected_accounts (priv->iw);
    }
}

static void
impl_signal_cancel (GtkAssistant *assistant)
{
  gtk_widget_destroy (GTK_WIDGET (assistant));
}

static void
impl_signal_prepare (GtkAssistant *assistant,
    GtkWidget *current_page)
{
  EmpathyAccountAssistant *self = EMPATHY_ACCOUNT_ASSISTANT (assistant);
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gint current_idx;

  /* check from which page we are coming from */
  if (priv->current_page_id == PAGE_IMPORT)
    empathy_import_widget_add_selected_accounts (priv->iw);

  current_idx = gtk_assistant_get_current_page (assistant);
  priv->current_page_id = current_idx;

  if (current_idx >= PAGE_ENTER_CREATE)
    {
      if (!priv->enter_create_forward && current_idx != PAGE_SALUT)
        {
          account_assistant_finish_enter_or_create_page (self,
              priv->first_resp == RESPONSE_ENTER_ACCOUNT ?
              TRUE : FALSE);
        }
      else
        {
          priv->enter_create_forward = FALSE;
        }

      if (priv->settings != NULL &&
          empathy_account_settings_is_valid (priv->settings))
        {
          account_assistant_apply_account_and_finish (self, priv->settings,
              TRUE);
          g_object_unref (priv->settings);
          priv->settings = NULL;
        }
    }
}

static void
do_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      g_value_set_object (value, priv->parent_window);
      break;
    case PROP_CONNECTION_MGRS:
      g_value_set_object (value, priv->connection_mgrs);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_PARENT:
      priv->parent_window = g_value_get_object (value);
      break;
    case PROP_CONNECTION_MGRS:
      priv->connection_mgrs = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
do_dispose (GObject *obj)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (obj);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  if (priv->settings != NULL)
    {
      g_object_unref (priv->settings);
      priv->settings = NULL;
    }

  g_object_unref (priv->account_mgr);
  priv->account_mgr = NULL;

  g_object_unref (priv->connection_mgrs);
  priv->connection_mgrs = NULL;

  if (G_OBJECT_CLASS (empathy_account_assistant_parent_class)->dispose != NULL)
    G_OBJECT_CLASS (empathy_account_assistant_parent_class)->dispose (obj);
}

static void
empathy_account_assistant_class_init (EmpathyAccountAssistantClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);
  GtkAssistantClass *gtkclass = GTK_ASSISTANT_CLASS (klass);
  GParamSpec *param_spec;

  oclass->get_property = do_get_property;
  oclass->set_property = do_set_property;
  oclass->constructed = do_constructed;
  oclass->dispose = do_dispose;

  gtkclass->apply = impl_signal_apply;
  gtkclass->prepare = impl_signal_prepare;
  gtkclass->cancel = impl_signal_cancel;

  param_spec = g_param_spec_object ("parent-window",
      "parent-window", "The parent window",
      GTK_TYPE_WINDOW,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_PARENT, param_spec);

  param_spec = g_param_spec_object ("connection-managers",
      "connection-managers", "A EmpathyConnectionManagers",
      EMPATHY_TYPE_CONNECTION_MANAGERS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT_ONLY);
  g_object_class_install_property (oclass, PROP_CONNECTION_MGRS, param_spec);

  g_type_class_add_private (klass, sizeof (EmpathyAccountAssistantPriv));
}

static void
create_salut_check_box_toggled_cb (GtkWidget *widget,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  gboolean sensitive;
  gboolean page_valid;

  sensitive = !gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget));

  gtk_widget_set_sensitive (priv->salut_account_widget, sensitive);

  if (!sensitive)
    {
      page_valid = TRUE;
      priv->create_salut_account = FALSE;
    }
  else
    {
      /* page is complete if the account is valid */
      page_valid = empathy_account_settings_is_valid (priv->salut_settings);
      priv->create_salut_account = TRUE;
    }

  gtk_assistant_set_page_complete (GTK_ASSISTANT (self), priv->salut_page,
      page_valid);
}

static void
account_assistant_salut_handle_apply_cb (EmpathyAccountWidget *widget_object,
    gboolean is_valid,
    EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);

  gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
      priv->salut_page, is_valid);
}

static GtkWidget *
account_assistant_build_salut_page (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  GtkWidget *main_vbox, *hbox_1, *w;
  GdkPixbuf *pix;
  EmpathyAccountSettings *settings;
  GtkWidget *account_widget;
  EmpathyAccountWidget *widget_object;
  gchar *markup;

  main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_show (main_vbox);
  gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 12);

  hbox_1 = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
  gtk_box_pack_start (GTK_BOX (main_vbox), hbox_1, TRUE, TRUE, 0);
  gtk_widget_show (hbox_1);

  w = gtk_label_new (NULL);
  markup = g_strdup_printf ("%s (<span style=\"italic\">%s</span>).",
      _("Empathy can automatically discover and chat with the people "
        "connected on the same network as you. "
        "If you want to use this feature, please check that the "
        "details below are correct. "
        "You can easily change these details later or disable this feature "
        "by using the 'Accounts' dialog"),
      _("Edit->Accounts"));
  gtk_label_set_markup (GTK_LABEL (w), markup);
  g_free (markup);
  gtk_misc_set_alignment (GTK_MISC (w), 0, 0.5);
  gtk_label_set_line_wrap (GTK_LABEL (w), TRUE);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, FALSE, FALSE, 0);
  gtk_widget_show (w);

  pix = empathy_pixbuf_from_icon_name_sized ("im-local-xmpp", 80);
  w = gtk_image_new_from_pixbuf (pix);
  gtk_box_pack_start (GTK_BOX (hbox_1), w, FALSE, FALSE, 6);
  gtk_widget_show (w);

  g_object_unref (pix);

  w = gtk_check_button_new_with_mnemonic (
      _("I do _not want to enable this feature for now"));
  gtk_box_pack_start (GTK_BOX (main_vbox), w, FALSE, FALSE, 0);
  g_signal_connect (w, "toggled",
      G_CALLBACK (create_salut_check_box_toggled_cb), self);
  gtk_widget_show (w);

  w = gtk_alignment_new (0, 0, 0, 0);
  gtk_alignment_set_padding (GTK_ALIGNMENT (w), 0, 0, 12, 0);
  gtk_box_pack_start (GTK_BOX (main_vbox), w, TRUE, TRUE, 0);
  gtk_widget_show (w);

  settings = create_salut_account_settings ();

  widget_object = empathy_account_widget_new_for_protocol (settings, TRUE);
  account_widget = empathy_account_widget_get_widget (widget_object);

  priv->salut_settings = settings;
  priv->salut_account_widget = account_widget;

  g_signal_connect (widget_object, "handle-apply",
      G_CALLBACK (account_assistant_salut_handle_apply_cb), self);

  gtk_box_pack_start (GTK_BOX (main_vbox), account_widget,
      FALSE, FALSE, 0);
  gtk_widget_show (account_widget);

  return main_vbox;
}

static GtkWidget *
account_assistant_build_salut_error_page (EmpathyAccountAssistant *self)
{
  GtkWidget *vbox;
  gchar *markup;

  markup = g_strdup_printf ("%s (<span style=\"italic\">%s</span>).",
      _("You won't be able to chat with people connected to your local "
        "network, as telepathy-salut is not installed. If you want to enable "
        "this feature, please install the telepathy-salut package and create "
        "a People Nearby account from the Accounts dialog"),
        _("Edit->Accounts"));

  vbox = build_error_vbox (_("telepathy-salut not installed"), markup);
  g_free (markup);
  return vbox;
}

static void
account_mgr_prepare_cb (GObject *source_object,
    GAsyncResult *result,
    gpointer user_data)
{
  EmpathyAccountAssistant *self = user_data;
  EmpathyAccountAssistantPriv *priv = GET_PRIV (self);
  TpAccountManager *manager = TP_ACCOUNT_MANAGER (source_object);
  GError *error = NULL;

  if (!tp_proxy_prepare_finish (manager, result, &error))
    {
      DEBUG ("Failed to prepare account manager: %s", error->message);
      g_error_free (error);
      return;
    }

  if (!should_create_salut_account (manager))
    {
      DEBUG ("No need to create a Salut account");
      priv->display_salut_page = FALSE;

      update_intro_page_buttons (self);

      gtk_assistant_set_page_type (GTK_ASSISTANT (self), priv->import_page,
          GTK_ASSISTANT_PAGE_CONFIRM);

      update_create_page_buttons (self);
    }
}

static void
empathy_account_assistant_init (EmpathyAccountAssistant *self)
{
  EmpathyAccountAssistantPriv *priv;

  priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_ACCOUNT_ASSISTANT,
      EmpathyAccountAssistantPriv);
  self->priv = priv;

  gtk_window_set_title (GTK_WINDOW (self),
      _("Messaging and VoIP Accounts Assistant"));

  priv->account_mgr = tp_account_manager_dup ();
}

static void
do_constructed (GObject *object)
{
  GtkAssistant *assistant = GTK_ASSISTANT (object);
  EmpathyAccountAssistant *self = EMPATHY_ACCOUNT_ASSISTANT (object);
  EmpathyAccountAssistantPriv *priv = GET_PRIV (object);
  GtkWidget *page;

  /* set us as transient for the parent window if any */
  if (priv->parent_window)
    gtk_window_set_transient_for (GTK_WINDOW (object),
        priv->parent_window);

  /* set the dialog hint, so this will be centered over the parent window */
  gtk_window_set_type_hint (GTK_WINDOW (object), GDK_WINDOW_TYPE_HINT_DIALOG);

  g_assert (priv->connection_mgrs != NULL);
  g_assert (empathy_connection_managers_is_ready (priv->connection_mgrs));

  g_signal_connect (self, "close",
      G_CALLBACK (account_assistant_close_cb), NULL);

  gtk_assistant_set_forward_page_func (assistant,
      account_assistant_page_forward_func, self, NULL);

  /* first page (introduction) */
  page = account_assistant_build_introduction_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Welcome to Empathy"));
  gtk_assistant_set_page_type (assistant, page,
      GTK_ASSISTANT_PAGE_INTRO);
  gtk_assistant_set_page_complete (assistant, page, TRUE);

  /* second page (import accounts) */
  page = account_assistant_build_import_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_title (assistant, page,
      _("Import your existing accounts"));
  gtk_assistant_set_page_complete (assistant, page, TRUE);
  gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_CONTENT);
  priv->import_page = page;

  /* third page (enter account details) */
  page = account_assistant_build_enter_or_create_page (self);
  gtk_assistant_append_page (assistant, page);
  gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_CONTENT);
  priv->enter_or_create_page = page;

  /* fourth page (salut details) */
  if (empathy_connection_managers_get_cm (priv->connection_mgrs, "salut")
      != NULL)
    {
      page = account_assistant_build_salut_page (self);
      gtk_assistant_append_page (assistant, page);
      gtk_assistant_set_page_title (assistant, page,
          _("Please enter personal details"));
      gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_CONFIRM);

      priv->create_salut_account = TRUE;

      if (empathy_account_settings_is_valid (priv->salut_settings))
        {
          gtk_assistant_set_page_complete (GTK_ASSISTANT (self),
              page, TRUE);
        }
    }
  else
    {
      page = account_assistant_build_salut_error_page (self);
      gtk_assistant_append_page (assistant, page);
      gtk_assistant_set_page_title (assistant, page, _("An error occurred"));
      gtk_assistant_set_page_type (assistant, page, GTK_ASSISTANT_PAGE_SUMMARY);

      priv->create_salut_account = FALSE;
    }

  priv->salut_page = page;
  priv->display_salut_page = TRUE;

  tp_proxy_prepare_async (priv->account_mgr, NULL,
      account_mgr_prepare_cb, self);

  gtk_window_set_resizable (GTK_WINDOW (self), FALSE);
}

GtkWidget *
empathy_account_assistant_show (GtkWindow *window,
    EmpathyConnectionManagers *connection_mgrs)
{
  static GtkWidget *dialog = NULL;

  if (dialog == NULL)
    {
      dialog = g_object_new (EMPATHY_TYPE_ACCOUNT_ASSISTANT,
          "parent-window", window,
          "connection-managers", connection_mgrs,
          NULL);
      g_object_add_weak_pointer (G_OBJECT (dialog), (gpointer *) &dialog);
    }

  gtk_window_present (GTK_WINDOW (dialog));

  return dialog;
}



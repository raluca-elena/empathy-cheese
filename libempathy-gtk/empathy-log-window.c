/*
 * Copyright (C) 2006-2007 Imendio AB
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
 *          Emilio Pozuelo Monfort <emilio.pozuelo@collabora.co.uk>
 */

#include "config.h"

#include <string.h>
#include <stdlib.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/proxy-subclass.h>

#include <telepathy-logger/telepathy-logger.h>
#include <telepathy-logger/call-event.h>

#include <extensions/extensions.h>

#include <libempathy/action-chain-internal.h>
#include <libempathy/empathy-chatroom-manager.h>
#include <libempathy/empathy-chatroom.h>
#include <libempathy/empathy-message.h>
#include <libempathy/empathy-request-util.h>
#include <libempathy/empathy-utils.h>
#include <libempathy/empathy-time.h>

#include "empathy-log-window.h"
#include "empathy-account-chooser.h"
#include "empathy-call-utils.h"
#include "empathy-chat-view.h"
#include "empathy-contact-dialogs.h"
#include "empathy-images.h"
#include "empathy-theme-manager.h"
#include "empathy-ui-utils.h"

#define DEBUG_FLAG EMPATHY_DEBUG_OTHER
#include <libempathy/empathy-debug.h>

typedef struct
{
  GtkWidget *window;

  GtkWidget *button_profile;
  GtkWidget *button_chat;
  GtkWidget *button_call;
  GtkWidget *button_video;

  GtkWidget *search_entry;

  GtkWidget *notebook;
  GtkWidget *spinner;

  GtkWidget *treeview_who;
  GtkWidget *treeview_what;
  GtkWidget *treeview_when;
  GtkWidget *treeview_events;

  GtkTreeStore *store_events;

  GtkWidget *account_chooser;

  gchar *last_find;

  TplActionChain *chain;
  TplLogManager *log_manager;

  /* Used to cancel logger calls when no longer needed */
  guint count;

  /* List of owned TplLogSearchHits, free with tpl_log_search_hit_free */
  GList *hits;
  guint source;

  /* Only used while waiting for the account chooser to be ready */
  TpAccount *selected_account;
  gchar *selected_chat_id;
  gboolean selected_is_chatroom;
} EmpathyLogWindow;

static void log_window_destroy_cb                (GtkWidget        *widget,
                                                  EmpathyLogWindow *window);
static void log_window_search_entry_changed_cb   (GtkWidget        *entry,
                                                  EmpathyLogWindow *window);
static void log_window_search_entry_activate_cb  (GtkWidget        *widget,
                                                  EmpathyLogWindow *window);
static void log_window_search_entry_icon_pressed_cb (GtkEntry      *entry,
                                                  GtkEntryIconPosition icon_pos,
                                                  GdkEvent *event,
                                                  gpointer user_data);
static void log_window_who_populate              (EmpathyLogWindow *window);
static void log_window_who_setup                 (EmpathyLogWindow *window);
static void log_window_when_setup                (EmpathyLogWindow *window);
static void log_window_what_setup                (EmpathyLogWindow *window);
static void log_window_events_setup              (EmpathyLogWindow *window);
static void log_window_chats_accounts_changed_cb (GtkWidget        *combobox,
                                                  EmpathyLogWindow *window);
static void log_window_chats_set_selected        (EmpathyLogWindow *window);
static void log_window_chats_get_messages        (EmpathyLogWindow *window,
                                                  gboolean force_get_dates);
static void log_window_when_changed_cb           (GtkTreeSelection *selection,
                                                  EmpathyLogWindow *window);
static void log_window_delete_menu_clicked_cb    (GtkMenuItem      *menuitem,
                                                  EmpathyLogWindow *window);

static void
empathy_account_chooser_filter_has_logs (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data);

enum
{
  PAGE_EVENTS,
  PAGE_SPINNER,
  PAGE_EMPTY
};

enum
{
  COL_TYPE_ANY,
  COL_TYPE_SEPARATOR,
  COL_TYPE_NORMAL
};

enum
{
  COL_WHO_TYPE,
  COL_WHO_ICON,
  COL_WHO_NAME,
  COL_WHO_ACCOUNT,
  COL_WHO_TARGET,
  COL_WHO_COUNT
};

enum
{
  COL_WHAT_TYPE,
  COL_WHAT_SUBTYPE,
  COL_WHAT_TEXT,
  COL_WHAT_ICON,
  COL_WHAT_EXPANDER,
  COL_WHAT_COUNT
};

enum
{
  COL_WHEN_DATE,
  COL_WHEN_TEXT,
  COL_WHEN_ICON,
  COL_WHEN_COUNT
};

enum
{
  COL_EVENTS_TYPE,
  COL_EVENTS_TS,
  COL_EVENTS_PRETTY_DATE,
  COL_EVENTS_ICON,
  COL_EVENTS_TEXT,
  COL_EVENTS_ACCOUNT,
  COL_EVENTS_TARGET,
  COL_EVENTS_EVENT,
  COL_EVENTS_COUNT
};

#define CALENDAR_ICON "stock_calendar"

/* Seconds between two messages to be considered one conversation */
#define MAX_GAP 30*60

#define WHAT_TYPE_SEPARATOR -1

typedef enum
{
  EVENT_CALL_INCOMING = 1 << 0,
  EVENT_CALL_OUTGOING = 1 << 1,
  EVENT_CALL_MISSED   = 1 << 2,
  EVENT_CALL_ALL      = 1 << 3,
} EventSubtype;

static EmpathyLogWindow *log_window = NULL;

static gboolean has_element;

#ifndef _date_copy
#define _date_copy(d) g_date_new_julian (g_date_get_julian (d))
#endif

typedef struct
{
  EmpathyLogWindow *window;
  TpAccount *account;
  TplEntity *entity;
  GDate *date;
  TplEventTypeMask event_mask;
  EventSubtype subtype;
  guint count;
} Ctx;

static Ctx *
ctx_new (EmpathyLogWindow *window,
    TpAccount *account,
    TplEntity *entity,
    GDate *date,
    TplEventTypeMask event_mask,
    EventSubtype subtype,
    guint count)
{
  Ctx *ctx = g_slice_new0 (Ctx);

  ctx->window = window;
  if (account != NULL)
    ctx->account = g_object_ref (account);
  if (entity != NULL)
    ctx->entity = g_object_ref (entity);
  if (date != NULL)
    ctx->date = _date_copy (date);
  ctx->event_mask = event_mask;
  ctx->subtype = subtype;
  ctx->count = count;

  return ctx;
}

static void
ctx_free (Ctx *ctx)
{
  tp_clear_object (&ctx->account);
  tp_clear_object (&ctx->entity);
  tp_clear_pointer (&ctx->date, g_date_free);

  g_slice_free (Ctx, ctx);
}

static void
account_chooser_ready_cb (EmpathyAccountChooser *chooser,
    EmpathyLogWindow *window)
{
  /* We'll display the account once the model has been populate with the chats
   * of this account. */
  empathy_account_chooser_set_account (EMPATHY_ACCOUNT_CHOOSER (
      window->account_chooser), window->selected_account);
}

static void
select_account_once_ready (EmpathyLogWindow *self,
    TpAccount *account,
    const gchar *chat_id,
    gboolean is_chatroom)
{
  EmpathyAccountChooser *account_chooser;

  account_chooser = EMPATHY_ACCOUNT_CHOOSER (self->account_chooser);

  tp_clear_object (&self->selected_account);
  self->selected_account = g_object_ref (account);

  g_free (self->selected_chat_id);
  self->selected_chat_id = g_strdup (chat_id);

  self->selected_is_chatroom = is_chatroom;

  if (empathy_account_chooser_is_ready (account_chooser))
    account_chooser_ready_cb (account_chooser, self);
  else
    /* Chat will be selected once the account chooser is ready */
    g_signal_connect (account_chooser, "ready",
        G_CALLBACK (account_chooser_ready_cb), self);
}

static void
toolbutton_profile_clicked (GtkToolButton *toolbutton,
    EmpathyLogWindow *window)
{
  GtkTreeView *view;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  GList *paths;
  TpAccount *account;
  TplEntity *target;
  EmpathyContact *contact;
  gint type;

  g_return_if_fail (window != NULL);

  view = GTK_TREE_VIEW (log_window->treeview_who);
  selection = gtk_tree_view_get_selection (view);

  paths = gtk_tree_selection_get_selected_rows (selection, &model);
  g_return_if_fail (paths != NULL);

  path = paths->data;
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
      COL_WHO_ACCOUNT, &account,
      COL_WHO_TARGET, &target,
      COL_WHO_TYPE, &type,
      -1);

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  g_return_if_fail (type == COL_TYPE_NORMAL);

  contact = empathy_contact_from_tpl_contact (account, target);
  empathy_contact_information_dialog_show (contact,
      GTK_WINDOW (window->window));
  g_object_unref (contact);

  g_object_unref (account);
  g_object_unref (target);
}

static void
toolbutton_chat_clicked (GtkToolButton *toolbutton,
    EmpathyLogWindow *window)
{
  GtkTreeView *view;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  GList *paths;
  TpAccount *account;
  TplEntity *target;
  EmpathyContact *contact;
  gint type;

  g_return_if_fail (window != NULL);

  view = GTK_TREE_VIEW (log_window->treeview_who);
  selection = gtk_tree_view_get_selection (view);

  paths = gtk_tree_selection_get_selected_rows (selection, &model);
  g_return_if_fail (paths != NULL);

  path = paths->data;
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
      COL_WHO_ACCOUNT, &account,
      COL_WHO_TARGET, &target,
      COL_WHO_TYPE, &type,
      -1);

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  g_return_if_fail (type == COL_TYPE_NORMAL);

  contact = empathy_contact_from_tpl_contact (account, target);
  empathy_chat_with_contact (contact,
      gtk_get_current_event_time ());

  g_object_unref (contact);
  g_object_unref (account);
  g_object_unref (target);
}

static void
toolbutton_av_clicked (GtkToolButton *toolbutton,
    EmpathyLogWindow *window)
{
  GtkTreeView *view;
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkTreePath *path;
  GList *paths;
  TpAccount *account;
  gchar *contact;
  gint type;
  gboolean video;

  g_return_if_fail (window != NULL);

  view = GTK_TREE_VIEW (log_window->treeview_who);
  selection = gtk_tree_view_get_selection (view);

  paths = gtk_tree_selection_get_selected_rows (selection, &model);
  g_return_if_fail (paths != NULL);

  path = paths->data;
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
      COL_WHO_ACCOUNT, &account,
      COL_WHO_NAME, &contact,
      COL_WHO_TYPE, &type,
      -1);

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  g_return_if_fail (type == COL_TYPE_NORMAL);

  video = (GTK_WIDGET (toolbutton) == window->button_video);

  empathy_call_new_with_streams (contact, account,
      TRUE, video, gtk_get_current_event_time ());

  g_free (contact);
  g_object_unref (account);
}

GtkWidget *
empathy_log_window_show (TpAccount  *account,
    const gchar *chat_id,
    gboolean     is_chatroom,
    GtkWindow   *parent)
{
  EmpathyAccountChooser   *account_chooser;
  GtkBuilder             *gui;
  gchar                  *filename;
  EmpathyLogWindow       *window;
  GtkWidget *vbox, *accounts, *search, *label, *quit;

  if (log_window != NULL)
    {
      gtk_window_present (GTK_WINDOW (log_window->window));

      if (account != NULL && chat_id != NULL)
        select_account_once_ready (log_window, account, chat_id, is_chatroom);

      return log_window->window;
    }

  log_window = g_new0 (EmpathyLogWindow, 1);
  log_window->chain = _tpl_action_chain_new_async (NULL, NULL, NULL);

  log_window->log_manager = tpl_log_manager_dup_singleton ();

  window = log_window;

  filename = empathy_file_lookup ("empathy-log-window.ui", "libempathy-gtk");
  gui = empathy_builder_get_file (filename,
      "log_window", &window->window,
      "toolbutton_profile", &window->button_profile,
      "toolbutton_chat", &window->button_chat,
      "toolbutton_call", &window->button_call,
      "toolbutton_video", &window->button_video,
      "toolbutton_accounts", &accounts,
      "toolbutton_search", &search,
      "imagemenuitem_quit", &quit,
      "treeview_who", &window->treeview_who,
      "treeview_what", &window->treeview_what,
      "treeview_when", &window->treeview_when,
      "treeview_events", &window->treeview_events,
      "notebook", &window->notebook,
      "spinner", &window->spinner,
      NULL);
  g_free (filename);

  empathy_builder_connect (gui, window,
      "log_window", "destroy", log_window_destroy_cb,
      "toolbutton_profile", "clicked", toolbutton_profile_clicked,
      "toolbutton_chat", "clicked", toolbutton_chat_clicked,
      "toolbutton_call", "clicked", toolbutton_av_clicked,
      "toolbutton_video", "clicked", toolbutton_av_clicked,
      "imagemenuitem_delete", "activate", log_window_delete_menu_clicked_cb,
      NULL);

  g_object_unref (gui);

  g_object_add_weak_pointer (G_OBJECT (window->window),
      (gpointer) &log_window);

  g_signal_connect_swapped (quit, "activate",
      G_CALLBACK (gtk_widget_destroy), window->window);

  /* Account chooser for chats */
  vbox = gtk_vbox_new (FALSE, 3);

  window->account_chooser = empathy_account_chooser_new ();
  account_chooser = EMPATHY_ACCOUNT_CHOOSER (window->account_chooser);
  empathy_account_chooser_set_has_all_option (account_chooser, TRUE);
  empathy_account_chooser_set_filter (account_chooser,
      empathy_account_chooser_filter_has_logs, NULL);
  empathy_account_chooser_set_all (account_chooser);

  g_signal_connect (window->account_chooser, "changed",
      G_CALLBACK (log_window_chats_accounts_changed_cb),
      window);

  label = gtk_label_new (_("Show"));

  gtk_box_pack_start (GTK_BOX (vbox),
      window->account_chooser,
      FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
      label,
      FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);
  gtk_container_add (GTK_CONTAINER (accounts), vbox);

  /* Search entry */
  vbox = gtk_vbox_new (FALSE, 3);

  window->search_entry = gtk_entry_new ();
  gtk_entry_set_icon_from_stock (GTK_ENTRY (window->search_entry),
      GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FIND);
  gtk_entry_set_icon_from_stock (GTK_ENTRY (window->search_entry),
      GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);

  label = gtk_label_new (_("Search"));

  gtk_box_pack_start (GTK_BOX (vbox),
      window->search_entry,
      FALSE, FALSE, 0);

  gtk_box_pack_start (GTK_BOX (vbox),
      label,
      FALSE, FALSE, 0);

  gtk_widget_show_all (vbox);
  gtk_container_add (GTK_CONTAINER (search), vbox);

  g_signal_connect (window->search_entry, "changed",
      G_CALLBACK (log_window_search_entry_changed_cb),
      window);

  g_signal_connect (window->search_entry, "activate",
      G_CALLBACK (log_window_search_entry_activate_cb),
      window);

  g_signal_connect (window->search_entry, "icon-press",
      G_CALLBACK (log_window_search_entry_icon_pressed_cb),
      window);

  /* Contacts */
  log_window_events_setup (window);
  log_window_who_setup (window);
  log_window_what_setup (window);
  log_window_when_setup (window);

  log_window_who_populate (window);

  if (account != NULL && chat_id != NULL)
    select_account_once_ready (window, account, chat_id, is_chatroom);

  if (parent != NULL)
    gtk_window_set_transient_for (GTK_WINDOW (window->window),
        GTK_WINDOW (parent));

  gtk_widget_show (window->window);

  return window->window;
}

static void
log_window_destroy_cb (GtkWidget *widget,
    EmpathyLogWindow *window)
{
  if (window->source != 0)
    g_source_remove (window->source);

  g_free (window->last_find);
  _tpl_action_chain_free (window->chain);
  g_object_unref (window->log_manager);
  tp_clear_object (&window->selected_account);
  g_free (window->selected_chat_id);

  g_free (window);
}

static gboolean
account_equal (TpAccount *a,
    TpAccount *b)
{
  return g_str_equal (tp_proxy_get_object_path (a),
      tp_proxy_get_object_path (b));
}

static gboolean
entity_equal (TplEntity *a,
    TplEntity *b)
{
  return g_str_equal (tpl_entity_get_identifier (a),
      tpl_entity_get_identifier (b));
}

static gboolean
is_same_confroom (TplEvent *e1,
    TplEvent *e2)
{
  TplEntity *sender1 = tpl_event_get_sender (e1);
  TplEntity *receiver1 = tpl_event_get_receiver (e1);
  TplEntity *sender2 = tpl_event_get_sender (e2);
  TplEntity *receiver2 = tpl_event_get_receiver (e2);
  TplEntity *room1, *room2;

  if (tpl_entity_get_entity_type (sender1) == TPL_ENTITY_ROOM)
    room1 = sender1;
  else if (tpl_entity_get_entity_type (receiver1) == TPL_ENTITY_ROOM)
    room1 = receiver1;
  else
    return FALSE;

  if (tpl_entity_get_entity_type (sender2) == TPL_ENTITY_ROOM)
    room2 = sender2;
  else if (tpl_entity_get_entity_type (receiver2) == TPL_ENTITY_ROOM)
    room2 = receiver2;
  else
    return FALSE;

  return g_str_equal (tpl_entity_get_identifier (room1),
      tpl_entity_get_identifier (room1));
}

static TplEntity *
event_get_target (TplEvent *event)
{
  TplEntity *sender = tpl_event_get_sender (event);
  TplEntity *receiver = tpl_event_get_receiver (event);

  if (tpl_entity_get_entity_type (sender) == TPL_ENTITY_SELF)
    return receiver;

  return sender;
}

static gboolean
model_is_parent (GtkTreeModel *model,
    GtkTreeIter *iter,
    TplEvent *event)
{
  TplEvent *stored_event;
  TplEntity *target;
  TpAccount *account;
  gboolean found = FALSE;
  GtkTreeIter parent;

  if (gtk_tree_model_iter_parent (model, &parent, iter))
    return FALSE;

  gtk_tree_model_get (model, iter,
      COL_EVENTS_ACCOUNT, &account,
      COL_EVENTS_TARGET, &target,
      COL_EVENTS_EVENT, &stored_event,
      -1);

  if (G_OBJECT_TYPE (event) == G_OBJECT_TYPE (stored_event) &&
      account_equal (account, tpl_event_get_account (event)) &&
      (entity_equal (target, event_get_target (event)) ||
      is_same_confroom (event, stored_event)))
    {
      GtkTreeIter child;
      gint64 timestamp;

      gtk_tree_model_iter_nth_child (model, &child, iter,
          gtk_tree_model_iter_n_children (model, iter) - 1);

      gtk_tree_model_get (model, &child,
          COL_EVENTS_TS, &timestamp,
          -1);

      if (ABS (tpl_event_get_timestamp (event) - timestamp) < MAX_GAP)
        {
          /* The gap is smaller than 30 min */
          found = TRUE;
        }
    }

  g_object_unref (stored_event);
  g_object_unref (account);
  g_object_unref (target);

  return found;
}

static const gchar *
get_contact_alias_for_message (EmpathyMessage *message)
{
  EmpathyContact *sender, *receiver;

  sender = empathy_message_get_sender (message);
  receiver = empathy_message_get_receiver (message);

  if (empathy_contact_is_user (sender))
    return empathy_contact_get_alias (receiver);

  return empathy_contact_get_alias (sender);
}

static void
get_parent_iter_for_message (TplEvent *event,
    EmpathyMessage *message,
    GtkTreeIter *parent)
{
  GtkTreeStore *store;
  GtkTreeModel *model;
  GtkTreeIter iter;
  gboolean parent_found = FALSE;
  gboolean next;

  store = log_window->store_events;
  model = GTK_TREE_MODEL (store);

  for (next = gtk_tree_model_get_iter_first (model, &iter);
       next;
       next = gtk_tree_model_iter_next (model, &iter))
    {
      if ((parent_found = model_is_parent (model, &iter, event)))
        break;
    }

  if (parent_found)
    {
      *parent = iter;
    }
  else
    {
      GDateTime *date;
      gchar *body, *pretty_date;

      date = g_date_time_new_from_unix_utc (
          tpl_event_get_timestamp (event));

      pretty_date = g_date_time_format (date,
          C_("A date with the time", "%A, %e %B %Y %X"));

      body = g_strdup_printf (_("Chat with %s"),
          get_contact_alias_for_message (message));

      gtk_tree_store_append (store, &iter, NULL);
      gtk_tree_store_set (store, &iter,
          COL_EVENTS_TS, tpl_event_get_timestamp (event),
          COL_EVENTS_PRETTY_DATE, pretty_date,
          COL_EVENTS_TEXT, body,
          COL_EVENTS_ICON, "stock_text_justify",
          COL_EVENTS_ACCOUNT, tpl_event_get_account (event),
          COL_EVENTS_TARGET, event_get_target (event),
          COL_EVENTS_EVENT, event,
          -1);

      *parent = iter;

      g_free (body);
      g_free (pretty_date);
      g_date_time_unref (date);
    }
}

static const gchar *
get_icon_for_event (TplEvent *event)
{
  const gchar *icon = NULL;

  if (TPL_IS_CALL_EVENT (event))
    {
      TplCallEvent *call = TPL_CALL_EVENT (event);
      TplCallEndReason reason = tpl_call_event_get_end_reason (call);
      TplEntity *sender = tpl_event_get_sender (event);
      TplEntity *receiver = tpl_event_get_receiver (event);

      if (reason == TPL_CALL_END_REASON_NO_ANSWER)
        icon = EMPATHY_IMAGE_CALL_MISSED;
      else if (tpl_entity_get_entity_type (sender) == TPL_ENTITY_SELF)
        icon = EMPATHY_IMAGE_CALL_OUTGOING;
      else if (tpl_entity_get_entity_type (receiver) == TPL_ENTITY_SELF)
        icon = EMPATHY_IMAGE_CALL_INCOMING;
    }

  return icon;
}

static void
log_window_append_chat_message (TplEvent *event,
    EmpathyMessage *message)
{
  GtkTreeStore *store = log_window->store_events;
  GtkTreeIter iter, parent;
  gchar *pretty_date, *body;
  GDateTime *date;

  date = g_date_time_new_from_unix_utc (
      tpl_event_get_timestamp (event));

  pretty_date = g_date_time_format (date, "%X");

  get_parent_iter_for_message (event, message, &parent);

  if (tpl_text_event_get_message_type (TPL_TEXT_EVENT (event))
      == TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION)
    {
      body = g_strdup_printf ("* %s %s",
          tpl_entity_get_alias (tpl_event_get_sender (event)),
          empathy_message_get_body (message));
    }
  else
    {
      body = g_strdup_printf (
          C_("First is a contact, second is what was said", "%s: %s"),
          tpl_entity_get_alias (tpl_event_get_sender (event)),
          empathy_message_get_body (message));
    }

  gtk_tree_store_append (store, &iter, &parent);
  gtk_tree_store_set (store, &iter,
      COL_EVENTS_TS, tpl_event_get_timestamp (event),
      COL_EVENTS_PRETTY_DATE, pretty_date,
      COL_EVENTS_TEXT, body,
      COL_EVENTS_ICON, get_icon_for_event (event),
      COL_EVENTS_ACCOUNT, tpl_event_get_account (event),
      COL_EVENTS_TARGET, event_get_target (event),
      COL_EVENTS_EVENT, event,
      -1);

  g_free (body);
  g_free (pretty_date);
  g_date_time_unref (date);
}

static void
log_window_append_call (TplEvent *event,
    EmpathyMessage *message)
{
  TplCallEvent *call = TPL_CALL_EVENT (event);
  GtkTreeStore *store = log_window->store_events;
  GtkTreeIter iter, child;
  gchar *pretty_date, *duration, *finished;
  GDateTime *started_date, *finished_date;
  GTimeSpan span;

  started_date = g_date_time_new_from_unix_utc (
      tpl_event_get_timestamp (event));

  pretty_date = g_date_time_format (started_date,
      C_("A date with the time", "%A, %e %B %Y %X"));

  gtk_tree_store_append (store, &iter, NULL);
  gtk_tree_store_set (store, &iter,
      COL_EVENTS_TS, tpl_event_get_timestamp (event),
      COL_EVENTS_PRETTY_DATE, pretty_date,
      COL_EVENTS_TEXT, empathy_message_get_body (message),
      COL_EVENTS_ICON, get_icon_for_event (event),
      COL_EVENTS_ACCOUNT, tpl_event_get_account (event),
      COL_EVENTS_TARGET, event_get_target (event),
      COL_EVENTS_EVENT, event,
      -1);

  if (tpl_call_event_get_end_reason (call) != TPL_CALL_END_REASON_NO_ANSWER)
    {
      gchar *body;

      span = tpl_call_event_get_duration (TPL_CALL_EVENT (event));
      if (span < 60)
        duration = g_strdup_printf (_("%" G_GINT64_FORMAT " seconds"), span);
      else
        duration = g_strdup_printf (_("%" G_GINT64_FORMAT " minutes"),
            span / 60);

      finished_date = g_date_time_add (started_date, -span);
      finished = g_date_time_format (finished_date, "%X");
      g_date_time_unref (finished_date);

      body = g_strdup_printf (_("Call took %s, ended at %s"),
          duration, finished);

      g_free (duration);
      g_free (finished);

      gtk_tree_store_append (store, &child, &iter);
      gtk_tree_store_set (store, &child,
          COL_EVENTS_TS, tpl_event_get_timestamp (event),
          COL_EVENTS_TEXT, body,
          COL_EVENTS_ACCOUNT, tpl_event_get_account (event),
          COL_EVENTS_TARGET, event_get_target (event),
          COL_EVENTS_EVENT, event,
          -1);

      g_free (body);
    }

  g_free (pretty_date);
  g_date_time_unref (started_date);
}

static void
log_window_append_message (TplEvent *event,
    EmpathyMessage *message)
{
  if (TPL_IS_TEXT_EVENT (event))
    log_window_append_chat_message (event, message);
  else if (TPL_IS_CALL_EVENT (event))
    log_window_append_call (event, message);
  else
    DEBUG ("Message type not handled");
}

static void
add_all_accounts_and_entities (GList **accounts,
    GList **entities)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeIter       iter;

  view = GTK_TREE_VIEW (log_window->treeview_who);
  model = gtk_tree_view_get_model (view);

  if (!gtk_tree_model_get_iter_first (model, &iter))
    return;

  do
    {
      TpAccount *account;
      TplEntity *entity;
      gint type;

      gtk_tree_model_get (model, &iter,
          COL_WHO_ACCOUNT, &account,
          COL_WHO_TARGET, &entity,
          COL_WHO_TYPE, &type,
          -1);

      if (type != COL_TYPE_NORMAL)
        continue;

      if (accounts != NULL)
        *accounts = g_list_append (*accounts, account);

      if (entities != NULL)
        *entities = g_list_append (*entities, entity);
    }
  while (gtk_tree_model_iter_next (model, &iter));
}

static gboolean
log_window_get_selected (EmpathyLogWindow *window,
    GList **accounts,
    GList **entities,
    GList **dates,
    TplEventTypeMask *event_mask,
    EventSubtype *subtype)
{
  GtkTreeView      *view;
  GtkTreeModel     *model;
  GtkTreeSelection *selection;
  GtkTreeIter       iter;
  TplEventTypeMask  ev = 0;
  EventSubtype      st = 0;
  GList            *paths, *l;
  gint              type;

  view = GTK_TREE_VIEW (window->treeview_who);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  paths = gtk_tree_selection_get_selected_rows (selection, NULL);
  if (paths == NULL)
    return FALSE;

  if (accounts != NULL)
    *accounts = NULL;
  if (entities != NULL)
    *entities = NULL;

  for (l = paths; l != NULL; l = l->next)
    {
      GtkTreePath *path = l->data;
      TpAccount *account;
      TplEntity *entity;

      gtk_tree_model_get_iter (model, &iter, path);
      gtk_tree_model_get (model, &iter,
          COL_WHO_ACCOUNT, &account,
          COL_WHO_TARGET, &entity,
          COL_WHO_TYPE, &type,
          -1);

      if (type == COL_TYPE_ANY)
        {
          if (accounts != NULL || entities != NULL)
            add_all_accounts_and_entities (accounts, entities);
          break;
        }

      if (accounts != NULL)
        *accounts = g_list_append (*accounts, g_object_ref (account));

      if (entities != NULL)
        *entities = g_list_append (*entities, g_object_ref (entity));

      g_object_unref (account);
      g_object_unref (entity);
    }
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  view = GTK_TREE_VIEW (window->treeview_what);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  paths = gtk_tree_selection_get_selected_rows (selection, NULL);
  for (l = paths; l != NULL; l = l->next)
    {
      GtkTreePath *path = l->data;
      TplEventTypeMask mask;
      EventSubtype submask;

      gtk_tree_model_get_iter (model, &iter, path);
      gtk_tree_model_get (model, &iter,
          COL_WHAT_TYPE, &mask,
          COL_WHAT_SUBTYPE, &submask,
          -1);

      ev |= mask;
      st |= submask;
    }
  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  view = GTK_TREE_VIEW (window->treeview_when);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  if (dates != NULL)
    {
      *dates = NULL;

      paths = gtk_tree_selection_get_selected_rows (selection, NULL);
      for (l = paths; l != NULL; l = l->next)
        {
          GtkTreePath *path = l->data;
          GDate *date;

          gtk_tree_model_get_iter (model, &iter, path);
          gtk_tree_model_get (model, &iter,
              COL_WHEN_DATE, &date,
              -1);

          *dates = g_list_append (*dates, date);
        }
      g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);
    }

  if (event_mask != NULL)
    *event_mask = ev;

  if (subtype != NULL)
    *subtype = st;

  return TRUE;
}

static gboolean
model_has_entity (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer data)
{
  TplLogSearchHit *hit = data;
  TplEntity *e;
  TpAccount *a;
  gboolean ret = FALSE;

  gtk_tree_model_get (model, iter,
      COL_WHO_TARGET, &e,
      COL_WHO_ACCOUNT, &a,
      -1);

  if (e != NULL && entity_equal (hit->target, e) &&
      a != NULL && account_equal (hit->account, a))
    {
      ret = has_element = TRUE;
    }

  tp_clear_object (&e);
  tp_clear_object (&a);

  return ret;
}

static gboolean
model_has_date (GtkTreeModel *model,
    GtkTreePath *path,
    GtkTreeIter *iter,
    gpointer data)
{
  GDate *date = data;
  GDate *d;

  gtk_tree_model_get (model, iter,
      COL_WHEN_DATE, &d,
      -1);

  if (!g_date_compare (date, d))
    {
      has_element = TRUE;
      return TRUE;
    }

  return FALSE;
}

static void
get_events_for_date (TplActionChain *chain, gpointer user_data);

static void
populate_events_from_search_hits (GList *accounts,
    GList *targets,
    GList *dates)
{
  TplEventTypeMask event_mask;
  EventSubtype subtype;
  GDate *anytime;
  GList *l;
  gboolean is_anytime = FALSE;

  if (!log_window_get_selected (log_window,
      NULL, NULL, NULL, &event_mask, &subtype))
    return;

  anytime = g_date_new_dmy (2, 1, -1);
  if (g_list_find_custom (dates, anytime, (GCompareFunc) g_date_compare))
    is_anytime = TRUE;

  for (l = log_window->hits; l != NULL; l = l->next)
    {
      TplLogSearchHit *hit = l->data;
      GList *acc, *targ;
      gboolean found = FALSE;

      /* Protect against invalid data (corrupt or old log files). */
      if (hit->account == NULL || hit->target == NULL)
        continue;

      for (acc = accounts, targ = targets;
           acc != NULL && targ != NULL && !found;
           acc = acc->next, targ = targ->next)
        {
          TpAccount *account = acc->data;
          TplEntity *target = targ->data;

          if (account_equal (hit->account, account) &&
              entity_equal (hit->target, target))
            found = TRUE;
        }

        if (!found)
          continue;

      if (is_anytime ||
          g_list_find_custom (dates, hit->date, (GCompareFunc) g_date_compare)
              != NULL)
        {
          Ctx *ctx;

          ctx = ctx_new (log_window, hit->account, hit->target, hit->date,
              event_mask, subtype, log_window->count);
          _tpl_action_chain_append (log_window->chain,
              get_events_for_date, ctx);
        }
    }

  _tpl_action_chain_start (log_window->chain);

  g_date_free (anytime);
}

static gchar *
format_date_for_display (GDate *date)
{
  gchar *text;
  GDate *now = NULL;
  gint days_elapsed;

  /* g_date_strftime sucks */

  now = g_date_new ();
  g_date_set_time_t (now, time (NULL));

  days_elapsed = g_date_days_between (date, now);

  if (days_elapsed < 0)
    {
      text = NULL;
    }
  else if (days_elapsed == 0)
    {
      text = g_strdup (_("Today"));
    }
  else if (days_elapsed == 1)
    {
      text = g_strdup (_("Yesterday"));
    }
  else
    {
      GDateTime *dt;

      dt = g_date_time_new_utc (g_date_get_year (date),
          g_date_get_month (date), g_date_get_day (date),
          0, 0, 0);

      if (days_elapsed <= 7)
        text = g_date_time_format (dt, "%A");
      else
        text = g_date_time_format (dt,
            C_("A date such as '23 May 2010', "
               "%e is the day, %B the month and %Y the year",
               "%e %B %Y"));

      g_date_time_unref (dt);
    }

  g_date_free (now);

  return text;
}

static void
populate_dates_from_search_hits (GList *accounts,
    GList *targets)
{
  GList *l;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkListStore *store;
  GtkTreeSelection *selection;
  GtkTreeIter iter;

  if (log_window == NULL)
    return;

  view = GTK_TREE_VIEW (log_window->treeview_when);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);
  selection = gtk_tree_view_get_selection (view);

  for (l = log_window->hits; l != NULL; l = l->next)
    {
      TplLogSearchHit *hit = l->data;
      GList *acc, *targ;
      gboolean found = FALSE;

      /* Protect against invalid data (corrupt or old log files). */
      if (hit->account == NULL || hit->target == NULL)
        continue;

      for (acc = accounts, targ = targets;
           acc != NULL && targ != NULL && !found;
           acc = acc->next, targ = targ->next)
        {
          TpAccount *account = acc->data;
          TplEntity *target = targ->data;

          if (account_equal (hit->account, account) &&
              entity_equal (hit->target, target))
            found = TRUE;
        }

        if (!found)
          continue;

      /* Add the date if it's not already there */
      has_element = FALSE;
      gtk_tree_model_foreach (model, model_has_date, hit->date);
      if (!has_element)
        {
          gchar *text = format_date_for_display (hit->date);

          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHEN_DATE, hit->date,
              COL_WHEN_TEXT, text,
              COL_WHEN_ICON, CALENDAR_ICON,
              -1);
        }
    }

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_WHEN_DATE, g_date_new_dmy (1, 1, -1),
          COL_WHEN_TEXT, "separator",
          -1);

      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_WHEN_DATE, g_date_new_dmy (2, 1, -1),
          COL_WHEN_TEXT, _("Anytime"),
          -1);

      if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 2))
        gtk_tree_selection_select_iter (selection, &iter);
    }
}

static void
populate_entities_from_search_hits (void)
{
  EmpathyAccountChooser *account_chooser;
  TpAccount *account;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GtkListStore *store;
  GList *l;

  view = GTK_TREE_VIEW (log_window->treeview_who);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);

  gtk_list_store_clear (store);

  account_chooser = EMPATHY_ACCOUNT_CHOOSER (log_window->account_chooser);
  account = empathy_account_chooser_get_account (account_chooser);

  for (l = log_window->hits; l; l = l->next)
    {
      TplLogSearchHit *hit = l->data;

      /* Protect against invalid data (corrupt or old log files). */
      if (hit->account == NULL || hit->target == NULL)
        continue;

      /* Filter based on the selected account */
      if (account != NULL && !account_equal (account, hit->account))
        continue;

      /* Add the entity if it's not already there */
      has_element = FALSE;
      gtk_tree_model_foreach (model, model_has_entity, hit);
      if (!has_element)
        {
          TplEntityType type = tpl_entity_get_entity_type (hit->target);
          gboolean room = type == TPL_ENTITY_ROOM;

          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHO_TYPE, COL_TYPE_NORMAL,
              COL_WHO_ICON, room ? EMPATHY_IMAGE_GROUP_MESSAGE
                                 : EMPATHY_IMAGE_AVATAR_DEFAULT,
              COL_WHO_NAME, tpl_entity_get_alias (hit->target),
              COL_WHO_ACCOUNT, hit->account,
              COL_WHO_TARGET, hit->target,
              -1);
        }
    }

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_WHO_TYPE, COL_TYPE_SEPARATOR,
          COL_WHO_NAME, "separator",
          -1);

      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_WHO_TYPE, COL_TYPE_ANY,
          COL_WHO_NAME, _("Anyone"),
          -1);
    }

  /* FIXME: select old entity if still available */
}

static void
log_manager_searched_new_cb (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  GList *hits;
  GtkTreeView *view;
  GtkTreeSelection *selection;
  GError *error = NULL;

  if (log_window == NULL)
    return;

  if (!tpl_log_manager_search_finish (TPL_LOG_MANAGER (manager),
      result, &hits, &error))
    {
      DEBUG ("%s. Aborting", error->message);
      g_error_free (error);
      return;
    }

  tp_clear_pointer (&log_window->hits, tpl_log_manager_search_free);
  log_window->hits = hits;

  populate_entities_from_search_hits ();

  view = GTK_TREE_VIEW (log_window->treeview_when);
  selection = gtk_tree_view_get_selection (view);

  g_signal_handlers_unblock_by_func (selection,
      log_window_when_changed_cb,
      log_window);
}

static void
log_window_find_populate (EmpathyLogWindow *window,
    const gchar *search_criteria)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkListStore *store;

  gtk_tree_store_clear (window->store_events);

  view = GTK_TREE_VIEW (window->treeview_who);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);

  gtk_list_store_clear (store);

  view = GTK_TREE_VIEW (window->treeview_when);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);
  selection = gtk_tree_view_get_selection (view);

  gtk_list_store_clear (store);

  if (EMP_STR_EMPTY (search_criteria))
    {
      tp_clear_pointer (&window->hits, tpl_log_manager_search_free);
      log_window_who_populate (window);
      return;
    }

  g_signal_handlers_block_by_func (selection,
      log_window_when_changed_cb,
      window);

  tpl_log_manager_search_async (window->log_manager,
      search_criteria, TPL_EVENT_MASK_ANY,
      log_manager_searched_new_cb, NULL);
}

static gboolean
start_find_search (EmpathyLogWindow *window)
{
  const gchar *str;

  str = gtk_entry_get_text (GTK_ENTRY (window->search_entry));

  /* Don't find the same crap again */
  if (window->last_find && !tp_strdiff (window->last_find, str))
    return FALSE;

  g_free (window->last_find);
  window->last_find = g_strdup (str);

  log_window_find_populate (window, str);

  return FALSE;
}

static void
log_window_search_entry_changed_cb (GtkWidget *entry,
    EmpathyLogWindow *window)
{
  if (window->source != 0)
    g_source_remove (window->source);
  window->source = g_timeout_add (500, (GSourceFunc) start_find_search,
      window);
}

static void
log_window_search_entry_activate_cb (GtkWidget *entry,
    EmpathyLogWindow *self)
{
  start_find_search (self);
}

static void
log_window_search_entry_icon_pressed_cb (GtkEntry *entry,
    GtkEntryIconPosition icon_pos,
    GdkEvent *event,
    gpointer user_data)
{
  if (icon_pos != GTK_ENTRY_ICON_SECONDARY)
    return;

  gtk_entry_buffer_set_text (gtk_entry_get_buffer (entry),
    "", -1);
}

static void
log_window_update_buttons_sensitivity (EmpathyLogWindow *window,
    GtkTreeModel *model,
    GtkTreeSelection *selection)
{
  EmpathyContact *contact;
  EmpathyCapabilities capabilities;
  TpAccount *account;
  TplEntity *target;
  GtkTreeIter iter;
  GList *paths;
  GtkTreePath *path;
  gboolean profile, chat, call, video;

  profile = chat = call = video = FALSE;

  if (!gtk_tree_model_get_iter_first (model, &iter))
    goto out;

  if (gtk_tree_selection_count_selected_rows (selection) != 1)
    goto out;

  if (gtk_tree_selection_iter_is_selected (selection, &iter))
    goto out;

  paths = gtk_tree_selection_get_selected_rows (selection, &model);
  g_return_if_fail (paths != NULL);

  path = paths->data;
  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter,
      COL_WHO_ACCOUNT, &account,
      COL_WHO_TARGET, &target,
      -1);

  g_list_free_full (paths, (GDestroyNotify) gtk_tree_path_free);

  contact = empathy_contact_from_tpl_contact (account, target);

  g_object_unref (account);
  g_object_unref (target);

  capabilities = empathy_contact_get_capabilities (contact);

  profile = chat = TRUE;
  call = capabilities & EMPATHY_CAPABILITIES_AUDIO;
  video = capabilities & EMPATHY_CAPABILITIES_VIDEO;

 out:
  gtk_widget_set_sensitive (window->button_profile, profile);
  gtk_widget_set_sensitive (window->button_chat, chat);
  gtk_widget_set_sensitive (window->button_call, call);
  gtk_widget_set_sensitive (window->button_video, video);
}

static void
log_window_who_changed_cb (GtkTreeSelection *selection,
    EmpathyLogWindow  *window)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeIter iter;

  DEBUG ("log_window_who_changed_cb");

  view = gtk_tree_selection_get_tree_view (selection);
  model = gtk_tree_view_get_model (view);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      /* If 'Anyone' is selected, everything else should be deselected */
      if (gtk_tree_selection_iter_is_selected (selection, &iter))
        {
          g_signal_handlers_block_by_func (selection,
              log_window_who_changed_cb,
              window);

          gtk_tree_selection_unselect_all (selection);
          gtk_tree_selection_select_iter (selection, &iter);

          g_signal_handlers_unblock_by_func (selection,
              log_window_who_changed_cb,
              window);
        }
    }

  log_window_update_buttons_sensitivity (window, model, selection);

  /* The contact changed, so the dates need to be updated */
  log_window_chats_get_messages (window, TRUE);
}

static void
log_manager_got_entities_cb (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  Ctx                   *ctx = user_data;
  GList                 *entities;
  GList                 *l;
  GtkTreeView           *view;
  GtkTreeModel          *model;
  GtkTreeSelection      *selection;
  GtkListStore          *store;
  GtkTreeIter            iter;
  GError                *error = NULL;
  gboolean               select_account = FALSE;

  if (log_window == NULL)
    goto out;

  if (log_window->count != ctx->count)
    goto out;

  if (!tpl_log_manager_get_entities_finish (TPL_LOG_MANAGER (manager),
      result, &entities, &error))
    {
      DEBUG ("%s. Aborting", error->message);
      g_error_free (error);
      goto out;
    }

  view = GTK_TREE_VIEW (ctx->window->treeview_who);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);
  store = GTK_LIST_STORE (model);

  /* Block signals to stop the logs being retrieved prematurely  */
  g_signal_handlers_block_by_func (selection,
      log_window_who_changed_cb, ctx->window);

  for (l = entities; l; l = l->next)
    {
      TplEntity *entity = TPL_ENTITY (l->data);
      TplEntityType type = tpl_entity_get_entity_type (entity);
      gboolean room = type == TPL_ENTITY_ROOM;

      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter,
          COL_WHO_TYPE, COL_TYPE_NORMAL,
          COL_WHO_ICON, room ? EMPATHY_IMAGE_GROUP_MESSAGE
                             : EMPATHY_IMAGE_AVATAR_DEFAULT,
          COL_WHO_NAME, tpl_entity_get_alias (entity),
          COL_WHO_ACCOUNT, ctx->account,
          COL_WHO_TARGET, entity,
          -1);

      if (ctx->window->selected_account != NULL &&
          !tp_strdiff (tp_proxy_get_object_path (ctx->account),
          tp_proxy_get_object_path (ctx->window->selected_account)))
        select_account = TRUE;
    }
  g_list_free_full (entities, g_object_unref);

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gint type;

      gtk_tree_model_get (model, &iter,
          COL_WHO_TYPE, &type,
          -1);

      if (type != COL_TYPE_ANY)
        {
          gtk_list_store_prepend (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHO_TYPE, COL_TYPE_SEPARATOR,
              COL_WHO_NAME, "separator",
              -1);

          gtk_list_store_prepend (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHO_TYPE, COL_TYPE_ANY,
              COL_WHO_NAME, _("Anyone"),
              -1);
        }
    }

  /* Unblock signals */
  g_signal_handlers_unblock_by_func (selection,
      log_window_who_changed_cb,
      ctx->window);

  /* We display the selected account if we populate the model with chats from
   * this account. */
  if (select_account)
    log_window_chats_set_selected (ctx->window);

out:
  _tpl_action_chain_continue (log_window->chain);
  ctx_free (ctx);
}

static void
get_entities_for_account (TplActionChain *chain, gpointer user_data)
{
  Ctx *ctx = user_data;

  tpl_log_manager_get_entities_async (ctx->window->log_manager, ctx->account,
      log_manager_got_entities_cb, ctx);
}

static void
select_first_entity (TplActionChain *chain, gpointer user_data)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkTreeIter iter;

  view = GTK_TREE_VIEW (log_window->treeview_who);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  if (gtk_tree_model_get_iter_first (model, &iter))
    gtk_tree_selection_select_iter (selection, &iter);

  _tpl_action_chain_continue (log_window->chain);
}

static void
log_window_who_populate (EmpathyLogWindow *window)
{
  EmpathyAccountChooser *account_chooser;
  TpAccount *account;
  gboolean all_accounts;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkListStore *store;
  Ctx *ctx;

  if (window->hits != NULL)
    {
      populate_entities_from_search_hits ();
      return;
    }

  account_chooser = EMPATHY_ACCOUNT_CHOOSER (window->account_chooser);
  account = empathy_account_chooser_dup_account (account_chooser);
  all_accounts = empathy_account_chooser_has_all_selected (account_chooser);

  view = GTK_TREE_VIEW (window->treeview_who);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);
  store = GTK_LIST_STORE (model);

  /* Block signals to stop the logs being retrieved prematurely  */
  g_signal_handlers_block_by_func (selection,
      log_window_who_changed_cb,
      window);

  gtk_list_store_clear (store);

  /* Unblock signals */
  g_signal_handlers_unblock_by_func (selection,
      log_window_who_changed_cb,
      window);

  _tpl_action_chain_clear (window->chain);
  window->count++;

  if (!all_accounts && account == NULL)
    {
      return;
    }
  else if (!all_accounts)
    {
      ctx = ctx_new (window, account, NULL, NULL, 0, 0, window->count);
      _tpl_action_chain_append (window->chain, get_entities_for_account, ctx);
    }
  else
    {
      TpAccountManager *manager;
      GList *accounts, *l;

      manager = empathy_account_chooser_get_account_manager (account_chooser);
      accounts = tp_account_manager_get_valid_accounts (manager);

      for (l = accounts; l != NULL; l = l->next)
        {
          account = l->data;

          ctx = ctx_new (window, account, NULL, NULL, 0, 0, window->count);
          _tpl_action_chain_append (window->chain,
              get_entities_for_account, ctx);
        }

      g_list_free (accounts);
    }
  _tpl_action_chain_append (window->chain, select_first_entity, NULL);
  _tpl_action_chain_start (window->chain);
}

static gint
sort_by_name (GtkTreeModel *model,
    GtkTreeIter *a,
    GtkTreeIter *b,
    gpointer user_data)
{
  gchar *name1, *name2;
  gint type1, type2;
  gint ret;

  gtk_tree_model_get (model, a,
      COL_WHO_TYPE, &type1,
      COL_WHO_NAME, &name1,
      -1);

  gtk_tree_model_get (model, b,
      COL_WHO_TYPE, &type2,
      COL_WHO_NAME, &name2,
      -1);

  if (type1 == COL_TYPE_ANY)
    ret = -1;
  else if (type2 == COL_TYPE_ANY)
    ret = 1;
  else if (type1 == COL_TYPE_SEPARATOR)
    ret = -1;
  else if (type2 == COL_TYPE_SEPARATOR)
    ret = 1;
  else
    ret = g_strcmp0 (name1, name2);

  g_free (name1);
  g_free (name2);

  return ret;
}

static gboolean
who_row_is_separator (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer data)
{
  gint type;

  gtk_tree_model_get (model, iter,
      COL_WHO_TYPE, &type,
      -1);

  return (type == COL_TYPE_SEPARATOR);
}

static void
log_window_events_setup (EmpathyLogWindow *window)
{
  GtkTreeView       *view;
  GtkTreeModel      *model;
  GtkTreeSelection  *selection;
  GtkTreeSortable   *sortable;
  GtkTreeViewColumn *column;
  GtkTreeStore      *store;
  GtkCellRenderer   *cell;

  view = GTK_TREE_VIEW (window->treeview_events);
  selection = gtk_tree_view_get_selection (view);

  /* new store */
  window->store_events = store = gtk_tree_store_new (COL_EVENTS_COUNT,
      G_TYPE_INT,           /* type */
      G_TYPE_INT64,         /* timestamp */
      G_TYPE_STRING,        /* stringified date */
      G_TYPE_STRING,        /* icon */
      G_TYPE_STRING,        /* name */
      TP_TYPE_ACCOUNT,      /* account */
      TPL_TYPE_ENTITY,      /* target */
      TPL_TYPE_EVENT);      /* event */

  model = GTK_TREE_MODEL (store);
  sortable = GTK_TREE_SORTABLE (store);

  gtk_tree_view_set_model (view, model);

  /* new column */
  column = gtk_tree_view_column_new ();

  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell,
      "icon-name", COL_EVENTS_ICON);

  cell = gtk_cell_renderer_text_new ();
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell,
      "text", COL_EVENTS_TEXT);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "xalign", 1.0, NULL);
  gtk_tree_view_column_pack_end (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell,
      "text", COL_EVENTS_PRETTY_DATE);

  gtk_tree_view_append_column (view, column);

  /* set up treeview properties */
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_NONE);
  gtk_tree_view_set_headers_visible (view, FALSE);

  gtk_tree_sortable_set_sort_column_id (sortable,
      COL_EVENTS_TS,
      GTK_SORT_ASCENDING);

  g_object_unref (store);
}

static void
log_window_who_setup (EmpathyLogWindow *window)
{
  GtkTreeView       *view;
  GtkTreeModel      *model;
  GtkTreeSelection  *selection;
  GtkTreeSortable   *sortable;
  GtkTreeViewColumn *column;
  GtkListStore      *store;
  GtkCellRenderer   *cell;

  view = GTK_TREE_VIEW (window->treeview_who);
  selection = gtk_tree_view_get_selection (view);

  /* new store */
  store = gtk_list_store_new (COL_WHO_COUNT,
      G_TYPE_INT,           /* type */
      G_TYPE_STRING,        /* icon */
      G_TYPE_STRING,        /* name */
      TP_TYPE_ACCOUNT,      /* account */
      TPL_TYPE_ENTITY);     /* target */

  model = GTK_TREE_MODEL (store);
  sortable = GTK_TREE_SORTABLE (store);

  gtk_tree_view_set_model (view, model);

  /* new column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("Who"));

  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell,
      "icon-name",
      COL_WHO_ICON);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell,
      "text",
      COL_WHO_NAME);

  gtk_tree_view_append_column (view, column);

  /* set up treeview properties */
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_tree_view_set_row_separator_func (view, who_row_is_separator,
      NULL, NULL);

  gtk_tree_sortable_set_sort_column_id (sortable,
      COL_WHO_NAME,
      GTK_SORT_ASCENDING);
  gtk_tree_sortable_set_sort_func (sortable,
      COL_WHO_NAME, sort_by_name,
      NULL, NULL);

  /* set up signals */
  g_signal_connect (selection, "changed",
      G_CALLBACK (log_window_who_changed_cb), window);

  g_object_unref (store);
}

static void
log_window_chats_accounts_changed_cb (GtkWidget       *combobox,
    EmpathyLogWindow *window)
{
  /* Clear all current messages shown in the textview */
  gtk_tree_store_clear (window->store_events);

  log_window_who_populate (window);
}

static void
log_window_chats_set_selected (EmpathyLogWindow *window)
{
  GtkTreeView          *view;
  GtkTreeModel         *model;
  GtkTreeSelection     *selection;
  GtkTreeIter           iter;
  GtkTreePath          *path;
  gboolean              next;

  view = GTK_TREE_VIEW (window->treeview_who);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  for (next = gtk_tree_model_get_iter_first (model, &iter);
       next;
       next = gtk_tree_model_iter_next (model, &iter))
    {
      TpAccount   *this_account;
      TplEntity   *this_target;
      const gchar *this_chat_id;
      gboolean     this_is_chatroom;
      gint         this_type;

      gtk_tree_model_get (model, &iter,
          COL_WHO_TYPE, &this_type,
          COL_WHO_ACCOUNT, &this_account,
          COL_WHO_TARGET, &this_target,
          -1);

      if (this_type != COL_TYPE_NORMAL)
        continue;

      this_chat_id = tpl_entity_get_identifier (this_target);
      this_is_chatroom = tpl_entity_get_entity_type (this_target)
          == TPL_ENTITY_ROOM;

      if (this_account == window->selected_account &&
          !tp_strdiff (this_chat_id, window->selected_chat_id) &&
          this_is_chatroom == window->selected_is_chatroom)
        {
          gtk_tree_selection_select_iter (selection, &iter);
          path = gtk_tree_model_get_path (model, &iter);
          gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 0.5, 0.0);
          gtk_tree_path_free (path);
          g_object_unref (this_account);
          g_object_unref (this_target);
          break;
        }

      g_object_unref (this_account);
      g_object_unref (this_target);
    }

  tp_clear_object (&window->selected_account);
  tp_clear_pointer (&window->selected_chat_id, g_free);
}

static gint
sort_by_date (GtkTreeModel *model,
    GtkTreeIter *a,
    GtkTreeIter *b,
    gpointer user_data)
{
  GDate *date1, *date2;

  gtk_tree_model_get (model, a,
      COL_WHEN_DATE, &date1,
      -1);

  gtk_tree_model_get (model, b,
      COL_WHEN_DATE, &date2,
      -1);

  return g_date_compare (date1, date2);
}

static gboolean
when_row_is_separator (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer data)
{
  gchar *when;
  gboolean ret;

  gtk_tree_model_get (model, iter,
      COL_WHEN_TEXT, &when,
      -1);

  ret = g_str_equal (when, "separator");
  g_free (when);
  return ret;
}

static void
log_window_when_changed_cb (GtkTreeSelection *selection,
    EmpathyLogWindow *window)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeIter iter;

  DEBUG ("log_window_when_changed_cb");

  view = gtk_tree_selection_get_tree_view (selection);
  model = gtk_tree_view_get_model (view);

  /* If 'Anytime' is selected, everything else should be deselected */
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      if (gtk_tree_selection_iter_is_selected (selection, &iter))
        {
          g_signal_handlers_block_by_func (selection,
              log_window_when_changed_cb,
              window);

          gtk_tree_selection_unselect_all (selection);
          gtk_tree_selection_select_iter (selection, &iter);

          g_signal_handlers_unblock_by_func (selection,
              log_window_when_changed_cb,
              window);
        }
    }

  log_window_chats_get_messages (window, FALSE);
}

static void
log_window_when_setup (EmpathyLogWindow *window)
{
  GtkTreeView       *view;
  GtkTreeModel      *model;
  GtkTreeSelection  *selection;
  GtkTreeSortable   *sortable;
  GtkTreeViewColumn *column;
  GtkListStore      *store;
  GtkCellRenderer   *cell;

  view = GTK_TREE_VIEW (window->treeview_when);
  selection = gtk_tree_view_get_selection (view);

  /* new store */
  store = gtk_list_store_new (COL_WHEN_COUNT,
      G_TYPE_DATE,        /* date */
      G_TYPE_STRING,      /* stringified date */
      G_TYPE_STRING);     /* icon */

  model = GTK_TREE_MODEL (store);
  sortable = GTK_TREE_SORTABLE (store);

  gtk_tree_view_set_model (view, model);

  /* new column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("When"));

  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell,
      "icon-name", COL_WHEN_ICON);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell,
      "text",
      COL_WHEN_TEXT);

  gtk_tree_view_append_column (view, column);

  /* set up treeview properties */
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_tree_view_set_row_separator_func (view, when_row_is_separator,
      NULL, NULL);
  gtk_tree_sortable_set_sort_column_id (sortable,
      COL_WHEN_DATE,
      GTK_SORT_DESCENDING);
  gtk_tree_sortable_set_sort_func (sortable,
      COL_WHEN_DATE, sort_by_date,
      NULL, NULL);

  /* set up signals */
  g_signal_connect (selection, "changed",
      G_CALLBACK (log_window_when_changed_cb),
      window);

  g_object_unref (store);
}

static gboolean
what_row_is_separator (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer data)
{
  gint type;

  gtk_tree_model_get (model, iter,
      COL_WHAT_TYPE, &type,
      -1);

  return (type == WHAT_TYPE_SEPARATOR);
}

static void
log_window_what_changed_cb (GtkTreeSelection *selection,
    EmpathyLogWindow *window)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeIter iter;

  DEBUG ("log_window_what_changed_cb");

  view = gtk_tree_selection_get_tree_view (selection);
  model = gtk_tree_view_get_model (view);

  /* If 'Anything' is selected, everything else should be deselected */
  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      if (gtk_tree_selection_iter_is_selected (selection, &iter))
        {
          g_signal_handlers_block_by_func (selection,
              log_window_what_changed_cb,
              window);

          gtk_tree_selection_unselect_all (selection);
          gtk_tree_selection_select_iter (selection, &iter);

          g_signal_handlers_unblock_by_func (selection,
              log_window_what_changed_cb,
              window);
        }
    }

  /* The dates need to be updated if we're not searching */
  log_window_chats_get_messages (window, window->hits == NULL);
}

static gboolean
log_window_what_collapse_row_cb (GtkTreeView *tree_view,
    GtkTreeIter *iter,
    GtkTreePath *path,
    gpointer user_data)
{
  /* Reject collapsing */
  return TRUE;
}

struct event
{
  gint type;
  EventSubtype subtype;
  const gchar *icon;
  const gchar *text;
};

static void
log_window_what_setup (EmpathyLogWindow *window)
{
  GtkTreeView       *view;
  GtkTreeModel      *model;
  GtkTreeSelection  *selection;
  GtkTreeSortable   *sortable;
  GtkTreeViewColumn *column;
  GtkTreeIter        iter, parent;
  GtkTreeStore      *store;
  GtkCellRenderer   *cell;
  guint i;
  struct event events [] = {
    { TPL_EVENT_MASK_ANY, 0, NULL, _("Anything") },
    { WHAT_TYPE_SEPARATOR, 0, NULL, "separator" },
    { TPL_EVENT_MASK_TEXT, 0, "stock_text_justify", _("Text chats") },
    { TPL_EVENT_MASK_CALL, EVENT_CALL_ALL, "call-start", _("Calls") }
  };
  struct event call_events [] = {
    { TPL_EVENT_MASK_CALL, EVENT_CALL_INCOMING, "call-start", _("Incoming calls") },
    { TPL_EVENT_MASK_CALL, EVENT_CALL_OUTGOING, "call-start", _("Outgoing calls") },
    { TPL_EVENT_MASK_CALL, EVENT_CALL_MISSED, "call-stop", _("Missed calls") }
  };

  view = GTK_TREE_VIEW (window->treeview_what);
  selection = gtk_tree_view_get_selection (view);

  /* new store */
  store = gtk_tree_store_new (COL_WHAT_COUNT,
      G_TYPE_INT,         /* history type */
      G_TYPE_INT,         /* history subtype */
      G_TYPE_STRING,      /* stringified history type */
      G_TYPE_STRING,      /* icon */
      G_TYPE_BOOLEAN);    /* expander (hidden) */

  model = GTK_TREE_MODEL (store);
  sortable = GTK_TREE_SORTABLE (store);

  gtk_tree_view_set_model (view, model);

  /* new column */
  column = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_title (column, _("What"));

  cell = gtk_cell_renderer_pixbuf_new ();
  gtk_tree_view_column_pack_start (column, cell, FALSE);
  gtk_tree_view_column_add_attribute (column, cell,
      "icon-name", COL_WHAT_ICON);

  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  gtk_tree_view_column_pack_start (column, cell, TRUE);
  gtk_tree_view_column_add_attribute (column, cell,
      "text", COL_WHAT_TEXT);

  gtk_tree_view_append_column (view, column);

  /* set up treeview properties */
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_tree_view_set_show_expanders (view, FALSE);
  gtk_tree_view_set_level_indentation (view, 12);
  gtk_tree_view_expand_all (view);
  gtk_tree_view_set_row_separator_func (view, what_row_is_separator,
      NULL, NULL);

  /* populate */
  for (i = 0; i < G_N_ELEMENTS (events); i++)
    {
      gtk_tree_store_append (store, &iter, NULL);
      gtk_tree_store_set (store, &iter,
          COL_WHAT_TYPE, events[i].type,
          COL_WHAT_SUBTYPE, events[i].subtype,
          COL_WHAT_TEXT, events[i].text,
          COL_WHAT_ICON, events[i].icon,
          -1);
    }

  gtk_tree_model_iter_nth_child (model, &parent, NULL, 3);
  for (i = 0; i < G_N_ELEMENTS (call_events); i++)
    {
      gtk_tree_store_append (store, &iter, &parent);
      gtk_tree_store_set (store, &iter,
          COL_WHAT_TYPE, call_events[i].type,
          COL_WHAT_SUBTYPE, call_events[i].subtype,
          COL_WHAT_TEXT, call_events[i].text,
          COL_WHAT_ICON, call_events[i].icon,
          -1);
    }

  gtk_tree_view_expand_all (view);

  /* select 'Anything' */
  if (gtk_tree_model_get_iter_first (model, &iter))
    gtk_tree_selection_select_iter (selection, &iter);

  /* set up signals */
  g_signal_connect (view, "test-collapse-row",
      G_CALLBACK (log_window_what_collapse_row_cb),
      NULL);
  g_signal_connect (selection, "changed",
      G_CALLBACK (log_window_what_changed_cb),
      window);

  g_object_unref (store);
}

static void
start_spinner (void)
{
  gtk_spinner_start (GTK_SPINNER (log_window->spinner));
  gtk_notebook_set_current_page (GTK_NOTEBOOK (log_window->notebook),
      PAGE_EMPTY);
}

static gboolean
show_spinner (gpointer data)
{
  gboolean active;

  if (log_window == NULL)
    return FALSE;

  g_object_get (log_window->spinner, "active", &active, NULL);

  if (active)
    gtk_notebook_set_current_page (GTK_NOTEBOOK (log_window->notebook),
        PAGE_SPINNER);

  return FALSE;
}

static void
show_events (TplActionChain *chain,
    gpointer user_data)
{
  gtk_spinner_stop (GTK_SPINNER (log_window->spinner));
  gtk_notebook_set_current_page (GTK_NOTEBOOK (log_window->notebook),
      PAGE_EVENTS);

  _tpl_action_chain_continue (chain);
}

static void
log_window_got_messages_for_date_cb (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  Ctx *ctx = user_data;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeIter iter;
  GList *events;
  GList *l;
  GError *error = NULL;
  gint n;

  if (log_window == NULL)
    {
      ctx_free (ctx);
      return;
    }

  if (log_window->count != ctx->count)
    goto out;

  if (!tpl_log_manager_get_events_for_date_finish (TPL_LOG_MANAGER (manager),
      result, &events, &error))
    {
      DEBUG ("Unable to retrieve messages for the selected date: %s. Aborting",
          error->message);
      g_error_free (error);
      goto out;
    }

  for (l = events; l; l = l->next)
    {
      TplEvent *event = l->data;
      gboolean append = TRUE;

      if (TPL_IS_CALL_EVENT (l->data)
          && ctx->event_mask & TPL_EVENT_MASK_CALL
          && ctx->event_mask != TPL_EVENT_MASK_ANY)
        {
          TplCallEvent *call = l->data;

          append = FALSE;

          if (ctx->subtype & EVENT_CALL_ALL)
            {
              append = TRUE;
            }
          else
            {
              TplCallEndReason reason = tpl_call_event_get_end_reason (call);
              TplEntity *sender = tpl_event_get_sender (event);
              TplEntity *receiver = tpl_event_get_receiver (event);

              if (reason == TPL_CALL_END_REASON_NO_ANSWER)
                {
                  if (ctx->subtype & EVENT_CALL_MISSED)
                    append = TRUE;
                }
              else if (ctx->subtype & EVENT_CALL_OUTGOING
                  && tpl_entity_get_entity_type (sender) == TPL_ENTITY_SELF)
                {
                  append = TRUE;
                }
              else if (ctx->subtype & EVENT_CALL_INCOMING
                  && tpl_entity_get_entity_type (receiver) == TPL_ENTITY_SELF)
                {
                  append = TRUE;
                }
            }
        }

      if (append)
        {
          EmpathyMessage *msg = empathy_message_from_tpl_log_event (event);
          log_window_append_message (event, msg);
          g_object_unref (msg);
        }

      g_object_unref (event);
    }
  g_list_free (events);

  view = GTK_TREE_VIEW (log_window->treeview_events);
  model = gtk_tree_view_get_model (view);
  n = gtk_tree_model_iter_n_children (model, NULL) - 1;

  if (n >= 0 && gtk_tree_model_iter_nth_child (model, &iter, NULL, n))
    {
      GtkTreePath *path;

      path = gtk_tree_model_get_path (model, &iter);
      gtk_tree_view_scroll_to_cell (view, path, NULL, FALSE, 0, 0);
      gtk_tree_path_free (path);
    }

 out:
  ctx_free (ctx);

  _tpl_action_chain_continue (log_window->chain);
}

static void
get_events_for_date (TplActionChain *chain, gpointer user_data)
{
  Ctx *ctx = user_data;

  tpl_log_manager_get_events_for_date_async (ctx->window->log_manager,
      ctx->account, ctx->entity, ctx->event_mask,
      ctx->date,
      log_window_got_messages_for_date_cb,
      ctx);
}

static void
log_window_get_messages_for_dates (EmpathyLogWindow *window,
    GList *dates)
{
  GList *accounts, *targets, *acc, *targ, *l;
  TplEventTypeMask event_mask;
  EventSubtype subtype;
  GDate *date, *anytime, *separator;

  if (!log_window_get_selected (window,
      &accounts, &targets, NULL, &event_mask, &subtype))
    return;

  anytime = g_date_new_dmy (2, 1, -1);
  separator = g_date_new_dmy (1, 1, -1);

  _tpl_action_chain_clear (window->chain);
  window->count++;

  for (acc = accounts, targ = targets;
       acc != NULL && targ != NULL;
       acc = acc->next, targ = targ->next)
    {
      TpAccount *account = acc->data;
      TplEntity *target = targ->data;

      for (l = dates; l != NULL; l = l->next)
        {
          date = l->data;

          /* Get events */
          if (g_date_compare (date, anytime) != 0)
            {
              Ctx *ctx;

              ctx = ctx_new (window, account, target, date, event_mask, subtype,
                  window->count);
              _tpl_action_chain_append (window->chain, get_events_for_date, ctx);
            }
          else
            {
              GtkTreeView *view = GTK_TREE_VIEW (window->treeview_when);
              GtkTreeModel *model = gtk_tree_view_get_model (view);
              GtkTreeIter iter;
              gboolean next;
              GDate *d;

              for (next = gtk_tree_model_get_iter_first (model, &iter);
                   next;
                   next = gtk_tree_model_iter_next (model, &iter))
                {
                  Ctx *ctx;

                  gtk_tree_model_get (model, &iter,
                      COL_WHEN_DATE, &d,
                      -1);

                  if (g_date_compare (d, anytime) != 0 &&
                      g_date_compare (d, separator) != 0)
                    {
                      ctx = ctx_new (window, account, target, d,
                          event_mask, subtype, window->count);
                      _tpl_action_chain_append (window->chain, get_events_for_date, ctx);
                    }
                }
            }
        }
    }

  start_spinner ();
  g_timeout_add (1000, show_spinner, NULL);
  _tpl_action_chain_append (window->chain, show_events, NULL);
  _tpl_action_chain_start (window->chain);

  g_list_free_full (accounts, g_object_unref);
  g_list_free_full (targets, g_object_unref);
  g_date_free (separator);
  g_date_free (anytime);
}

static void
log_manager_got_dates_cb (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  Ctx *ctx = user_data;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkListStore *store;
  GtkTreeIter iter;
  GList *dates;
  GList *l;
  GError *error = NULL;

  if (log_window == NULL)
    goto out;

  if (log_window->count != ctx->count)
    goto out;

  if (!tpl_log_manager_get_dates_finish (TPL_LOG_MANAGER (manager),
       result, &dates, &error))
    {
      DEBUG ("Unable to retrieve messages' dates: %s. Aborting",
          error->message);
      goto out;
    }

  view = GTK_TREE_VIEW (log_window->treeview_when);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);
  selection = gtk_tree_view_get_selection (view);

  for (l = dates; l != NULL; l = l->next)
    {
      GDate *date = l->data;

      /* Add the date if it's not already there */
      has_element = FALSE;
      gtk_tree_model_foreach (model, model_has_date, date);
      if (!has_element)
        {
          gchar *text = format_date_for_display (date);

          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHEN_DATE, date,
              COL_WHEN_TEXT, text,
              COL_WHEN_ICON, CALENDAR_ICON,
              -1);

          g_free (text);
        }
    }

  if (gtk_tree_model_get_iter_first (model, &iter))
    {
      gchar *separator = NULL;

      if (gtk_tree_model_iter_next (model, &iter))
        {
          gtk_tree_model_get (model, &iter,
              COL_WHEN_TEXT, &separator,
              -1);
        }

      if (g_strcmp0 (separator, "separator") != 0)
        {
          gtk_list_store_prepend (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHEN_DATE, g_date_new_dmy (1, 1, -1),
              COL_WHEN_TEXT, "separator",
              -1);

          gtk_list_store_prepend (store, &iter);
          gtk_list_store_set (store, &iter,
              COL_WHEN_DATE, g_date_new_dmy (2, 1, -1),
              COL_WHEN_TEXT, _("Anytime"),
              -1);
        }
    }

  g_list_free_full (dates, g_free);
 out:
  ctx_free (ctx);
  _tpl_action_chain_continue (log_window->chain);
}

static void
select_first_date (TplActionChain *chain, gpointer user_data)
{
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkTreeSelection *selection;
  GtkTreeIter iter;

  view = GTK_TREE_VIEW (log_window->treeview_when);
  model = gtk_tree_view_get_model (view);
  selection = gtk_tree_view_get_selection (view);

  /* Show messages of the most recent date */
  if (gtk_tree_model_iter_nth_child (model, &iter, NULL, 2))
    gtk_tree_selection_select_iter (selection, &iter);

  _tpl_action_chain_continue (log_window->chain);
}

static void
get_dates_for_entity (TplActionChain *chain, gpointer user_data)
{
  Ctx *ctx = user_data;

  tpl_log_manager_get_dates_async (ctx->window->log_manager,
      ctx->account, ctx->entity, ctx->event_mask,
      log_manager_got_dates_cb, ctx);
}

static void
log_window_chats_get_messages (EmpathyLogWindow *window,
    gboolean force_get_dates)
{
  GList *accounts, *targets, *dates;
  TplEventTypeMask event_mask;
  GtkTreeView *view;
  GtkTreeModel *model;
  GtkListStore *store;
  GtkTreeSelection *selection;

  if (!log_window_get_selected (window, &accounts, &targets,
      &dates, &event_mask, NULL))
    return;

  view = GTK_TREE_VIEW (window->treeview_when);
  selection = gtk_tree_view_get_selection (view);
  model = gtk_tree_view_get_model (view);
  store = GTK_LIST_STORE (model);

  /* Clear all current messages shown in the textview */
  gtk_tree_store_clear (window->store_events);

  _tpl_action_chain_clear (window->chain);
  window->count++;

  /* If there's a search use the returned hits */
  if (window->hits != NULL)
    {
      if (force_get_dates)
        {
          g_signal_handlers_block_by_func (selection,
              log_window_when_changed_cb,
              window);

          gtk_list_store_clear (store);

          g_signal_handlers_unblock_by_func (selection,
              log_window_when_changed_cb,
              window);

          populate_dates_from_search_hits (accounts, targets);
        }
      else
        {
          populate_events_from_search_hits (accounts, targets, dates);
        }
    }
  /* Either use the supplied date or get the last */
  else if (force_get_dates || dates == NULL)
    {
      GList *acc, *targ;

      g_signal_handlers_block_by_func (selection,
          log_window_when_changed_cb,
          window);

      gtk_list_store_clear (store);

      g_signal_handlers_unblock_by_func (selection,
          log_window_when_changed_cb,
          window);

      /* Get a list of dates and show them on the treeview */
      for (targ = targets, acc = accounts;
           targ != NULL && acc != NULL;
           targ = targ->next, acc = acc->next)
        {
          TpAccount *account = acc->data;
          TplEntity *target = targ->data;
          Ctx *ctx = ctx_new (window, account, target, NULL, event_mask, 0,
              window->count);

          _tpl_action_chain_append (window->chain, get_dates_for_entity, ctx);
        }
      _tpl_action_chain_append (window->chain, select_first_date, NULL);
      _tpl_action_chain_start (window->chain);
    }
  else
    {
      /* Show messages of the selected date */
      log_window_get_messages_for_dates (window, dates);
    }

  g_list_free_full (accounts, g_object_unref);
  g_list_free_full (targets, g_object_unref);
  g_list_free_full (dates, (GFreeFunc) g_date_free);
}

typedef struct {
  EmpathyAccountChooserFilterResultCallback callback;
  gpointer user_data;
} FilterCallbackData;

static void
got_entities (GObject *manager,
    GAsyncResult *result,
    gpointer user_data)
{
  FilterCallbackData *data = user_data;
  GList *entities;
  GError *error = NULL;

  if (!tpl_log_manager_get_entities_finish (TPL_LOG_MANAGER (manager),
      result, &entities, &error))
    {
      DEBUG ("Could not get entities: %s", error->message);
      g_error_free (error);
      data->callback (FALSE, data->user_data);
    }
  else
    {
      data->callback (entities != NULL, data->user_data);

      g_list_free_full (entities, g_object_unref);
    }

  g_slice_free (FilterCallbackData, data);
}

static void
empathy_account_chooser_filter_has_logs (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data)
{
  TplLogManager *manager = tpl_log_manager_dup_singleton ();
  FilterCallbackData *cb_data = g_slice_new0 (FilterCallbackData);

  cb_data->callback = callback;
  cb_data->user_data = callback_data;

  tpl_log_manager_get_entities_async (manager, account, got_entities, cb_data);

  g_object_unref (manager);
}

static void
log_window_logger_clear_account_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyLogWindow *window = user_data;

  if (error != NULL)
    g_warning ("Error when clearing logs: %s", error->message);

  /* Refresh the log viewer so the logs are cleared if the account
   * has been deleted */
  gtk_tree_store_clear (window->store_events);
  log_window_who_populate (window);

  /* Re-filter the account chooser so the accounts without logs get greyed out */
  empathy_account_chooser_set_filter (
      EMPATHY_ACCOUNT_CHOOSER (window->account_chooser),
      empathy_account_chooser_filter_has_logs, NULL);
}

static void
log_window_clear_logs_chooser_select_account (EmpathyAccountChooser *chooser,
    EmpathyLogWindow *window)
{
  EmpathyAccountChooser *account_chooser;

  account_chooser = EMPATHY_ACCOUNT_CHOOSER (window->account_chooser);

  empathy_account_chooser_set_account (chooser,
      empathy_account_chooser_get_account (account_chooser));
}

static void
log_window_delete_menu_clicked_cb (GtkMenuItem *menuitem,
    EmpathyLogWindow *window)
{
  GtkWidget *dialog, *content_area, *hbox, *label;
  EmpathyAccountChooser *account_chooser;
  gint response_id;
  TpDBusDaemon *bus;
  TpProxy *logger;
  GError *error = NULL;

  account_chooser = (EmpathyAccountChooser *) empathy_account_chooser_new ();
  empathy_account_chooser_set_has_all_option (account_chooser, TRUE);
  empathy_account_chooser_set_filter (account_chooser,
      empathy_account_chooser_filter_has_logs, NULL);

  /* Select the same account as in the history window */
  if (empathy_account_chooser_is_ready (account_chooser))
    log_window_clear_logs_chooser_select_account (account_chooser, window);
  else
    g_signal_connect (account_chooser, "ready",
        G_CALLBACK (log_window_clear_logs_chooser_select_account), window);

  dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW (window->window),
      GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING,
      GTK_BUTTONS_NONE,
      _("Are you sure you want to delete all logs of previous conversations?"));

  gtk_dialog_add_buttons (GTK_DIALOG (dialog),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      _("Clear All"), GTK_RESPONSE_APPLY,
      NULL);

  content_area = gtk_message_dialog_get_message_area (
      GTK_MESSAGE_DIALOG (dialog));

  hbox = gtk_hbox_new (FALSE, 6);
  label = gtk_label_new (_("Delete from:"));
  gtk_box_pack_start (GTK_BOX (hbox), label,
      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (hbox), GTK_WIDGET (account_chooser),
      FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (content_area), hbox,
      FALSE, FALSE, 0);

  gtk_widget_show_all (hbox);

  response_id = gtk_dialog_run (GTK_DIALOG (dialog));

  if (response_id != GTK_RESPONSE_APPLY)
    goto out;

  bus = tp_dbus_daemon_dup (&error);
  if (error != NULL)
    {
      g_warning ("Could not delete logs: %s", error->message);
      g_error_free (error);
      goto out;
    }

  logger = g_object_new (TP_TYPE_PROXY,
      "bus-name", "org.freedesktop.Telepathy.Logger",
      "object-path", "/org/freedesktop/Telepathy/Logger",
      "dbus-daemon", bus,
      NULL);
  g_object_unref (bus);

  tp_proxy_add_interface_by_id (logger, EMP_IFACE_QUARK_LOGGER);

  if (empathy_account_chooser_has_all_selected (account_chooser))
    {
      DEBUG ("Deleting logs for all the accounts");

      emp_cli_logger_call_clear (logger, -1,
          log_window_logger_clear_account_cb,
          window, NULL, G_OBJECT (window->window));
    }
  else
    {
      TpAccount *account;

      account = empathy_account_chooser_get_account (account_chooser);

      DEBUG ("Deleting logs for %s", tp_proxy_get_object_path (account));

      emp_cli_logger_call_clear_account (logger, -1,
          tp_proxy_get_object_path (account),
          log_window_logger_clear_account_cb,
          window, NULL, G_OBJECT (window->window));
    }

  g_object_unref (logger);
 out:
  gtk_widget_destroy (dialog);
}

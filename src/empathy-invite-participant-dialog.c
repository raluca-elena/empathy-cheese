/*
 * empathy-invite-participant-dialog.c
 *
 * EmpathyInviteParticipantDialog
 *
 * (c) 2009, Collabora Ltd.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <glib/gi18n.h>
#include <folks/folks-telepathy.h>

#include "empathy-invite-participant-dialog.h"

#include <libempathy-gtk/empathy-individual-view.h>
#include <libempathy-gtk/empathy-ui-utils.h>

G_DEFINE_TYPE (EmpathyInviteParticipantDialog,
    empathy_invite_participant_dialog, GTK_TYPE_DIALOG);

enum
{
  PROP_TP_CHAT = 1
};

typedef struct _AddTemporaryIndividualCtx AddTemporaryIndividualCtx;

struct _EmpathyInviteParticipantDialogPrivate
{
  EmpathyTpChat *tp_chat;
  TpAccountManager *account_mgr;

  EmpathyIndividualStore *store;
  EmpathyIndividualView *view;

  GtkWidget *invite_button;

  GPtrArray *search_words;
  gchar *search_str;

  /* Context representing the FolksIndividual which are added because of the
   * current search from the user. */
  AddTemporaryIndividualCtx *add_temp_ctx;
};

static void
invite_participant_dialog_get_property (GObject *object,
    guint param_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyInviteParticipantDialog *self = (EmpathyInviteParticipantDialog *)
    object;

  switch (param_id)
    {
    case PROP_TP_CHAT:
      g_value_set_object (value, self->priv->tp_chat);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

static void
invite_participant_dialog_set_property (GObject *object,
    guint param_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyInviteParticipantDialog *self = (EmpathyInviteParticipantDialog *)
    object;

  switch (param_id)
    {
    case PROP_TP_CHAT:
      g_assert (self->priv->tp_chat == NULL); /* construct-only */
      self->priv->tp_chat = g_value_dup_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    };
}

struct _AddTemporaryIndividualCtx
{
  EmpathyInviteParticipantDialog *self;
  /* List of owned FolksIndividual */
  GList *individuals;
};

static AddTemporaryIndividualCtx *
add_temporary_individual_ctx_new (EmpathyInviteParticipantDialog *self)
{
  AddTemporaryIndividualCtx *ctx = g_slice_new0 (AddTemporaryIndividualCtx);

  ctx->self = self;
  return ctx;
}

static void
add_temporary_individual_ctx_free (AddTemporaryIndividualCtx *ctx)
{
  GList *l;

  /* Remove all the individuals from the model */
  for (l = ctx->individuals; l != NULL; l = g_list_next (l))
    {
      FolksIndividual *individual = l->data;

      individual_store_remove_individual_and_disconnect (ctx->self->priv->store,
          individual);

      g_object_unref (individual);
    }

  g_list_free (ctx->individuals);
  g_slice_free (AddTemporaryIndividualCtx, ctx);
}

static void
invite_participant_dialog_dispose (GObject *object)
{
  EmpathyInviteParticipantDialog *self = (EmpathyInviteParticipantDialog *)
    object;

  tp_clear_pointer (&self->priv->add_temp_ctx,
      add_temporary_individual_ctx_free);

  tp_clear_object (&self->priv->tp_chat);
  tp_clear_object (&self->priv->store);
  tp_clear_pointer (&self->priv->search_words, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->search_str, g_free);

  tp_clear_object (&self->priv->account_mgr);

  G_OBJECT_CLASS (empathy_invite_participant_dialog_parent_class)->dispose (
      object);
}

static void
empathy_invite_participant_dialog_class_init (
    EmpathyInviteParticipantDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = invite_participant_dialog_get_property;
  object_class->set_property = invite_participant_dialog_set_property;
  object_class->dispose = invite_participant_dialog_dispose;

  g_type_class_add_private (object_class,
      sizeof (EmpathyInviteParticipantDialogPrivate));

  g_object_class_install_property (object_class,
      PROP_TP_CHAT,
      g_param_spec_object ("tp-chat", "EmpathyTpChat", "EmpathyTpChat",
          EMPATHY_TYPE_TP_CHAT,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
view_selection_changed_cb (GtkWidget *treeview,
    EmpathyInviteParticipantDialog *self)
{
  FolksIndividual *individual;

  individual = empathy_individual_view_dup_selected (self->priv->view);

  gtk_widget_set_sensitive (self->priv->invite_button, individual != NULL);

  tp_clear_object (&individual);
}

/* Return the TpContact of @individual which is on the same connection as the
 * EmpathyTpChat */
static TpContact *
get_tp_contact_for_chat (EmpathyInviteParticipantDialog *self,
    FolksIndividual *individual)
{
  TpContact *contact = NULL;
  TpConnection *chat_conn;
  GeeSet *personas;
  GeeIterator *iter;

  chat_conn = tp_channel_borrow_connection ((TpChannel *) self->priv->tp_chat);

  personas = folks_individual_get_personas (individual);
  iter = gee_iterable_iterator (GEE_ITERABLE (personas));
  while (contact == FALSE && gee_iterator_next (iter))
    {
      TpfPersona *persona = gee_iterator_get (iter);
      TpConnection *contact_conn;
      TpContact *contact_cur = NULL;

      if (TPF_IS_PERSONA (persona))
        {
          contact_cur = tpf_persona_get_contact (persona);
          if (contact_cur != NULL)
            {
              contact_conn = tp_contact_get_connection (contact_cur);

              if (!tp_strdiff (tp_proxy_get_object_path (contact_conn),
                    tp_proxy_get_object_path (chat_conn)))
                contact = contact_cur;
            }
        }

      g_clear_object (&persona);
    }
  g_clear_object (&iter);

  return contact;
}

static gboolean
filter_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer user_data)
{
  EmpathyInviteParticipantDialog *self = user_data;
  FolksIndividual *individual;
  TpContact *contact;
  gboolean is_online;
  GList *members, *l;
  gboolean display = FALSE;

  gtk_tree_model_get (model, iter,
      EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
      EMPATHY_INDIVIDUAL_STORE_COL_IS_ONLINE, &is_online,
      -1);

  if (individual == NULL)
    goto out;

  if (self->priv->search_words == NULL)
    {
      /* Not searching, display online contacts */
      if (!is_online)
        goto out;
    }
  else
    {
      if (!empathy_individual_match_string (individual,
            self->priv->search_str, self->priv->search_words))
        goto out;
    }

  /* Filter out individuals not having a persona on the same connection as the
   * EmpathyTpChat. */
  contact = get_tp_contact_for_chat (self, individual);

  if (contact == NULL)
    goto out;

  /* Filter out contacts which are already in the chat */
  members = empathy_contact_list_get_members (EMPATHY_CONTACT_LIST (
        self->priv->tp_chat));

  display = TRUE;

  for (l = members; l != NULL; l = g_list_next (l))
    {
      EmpathyContact *member = l->data;
      TpHandle handle;

      /* Try to get the non-channel specific handle. */
      handle = tp_channel_group_get_handle_owner (
          TP_CHANNEL (self->priv->tp_chat),
          empathy_contact_get_handle (member));
      if (handle == 0)
        handle = empathy_contact_get_handle (member);

      if (handle == tp_contact_get_handle (contact))
        {
          display = FALSE;
          break;
        }
    }

  g_list_free_full (members, g_object_unref);

out:
  tp_clear_object (&individual);
  return display;
}

static void
get_contacts_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    const gchar * const *requested_ids,
    GHashTable *failed_id_errors,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  EmpathyInviteParticipantDialog *self =
    (EmpathyInviteParticipantDialog *) weak_object;
  AddTemporaryIndividualCtx *ctx = user_data;
  TpAccount *account;
  TpfPersonaStore *store;
  FolksIndividual *individual;
  TpfPersona *persona_new;
  GeeSet *personas;

  if (self->priv->add_temp_ctx != ctx)
    /* another request has been started */
    return;

  if (n_contacts != 1)
    return;

  account = g_object_get_data (G_OBJECT (connection), "account");

  store = tpf_persona_store_new (account);
  personas = GEE_SET (
      gee_hash_set_new (FOLKS_TYPE_PERSONA, g_object_ref, g_object_unref,
      g_direct_hash, g_direct_equal));
  persona_new = tpf_persona_new (contacts[0], store);
  gee_collection_add (GEE_COLLECTION (personas),
      tpf_persona_new (contacts[0], store));

  individual = folks_individual_new (personas);

  /* Pass ownership to the list */
  ctx->individuals = g_list_prepend (ctx->individuals, individual);

  individual_store_add_individual_and_connect (self->priv->store, individual);

  g_clear_object (&persona_new);
  g_clear_object (&personas);
  g_object_unref (store);
}

static void
add_temporary_individuals (EmpathyInviteParticipantDialog *self,
    const gchar *id)
{
  GList *accounts, *l;

  tp_clear_pointer (&self->priv->add_temp_ctx,
      add_temporary_individual_ctx_free);

  if (tp_str_empty (id))
    return;

  self->priv->add_temp_ctx = add_temporary_individual_ctx_new (self);

  /* Try to add an individual for each connected account */
  accounts = tp_account_manager_get_valid_accounts (self->priv->account_mgr);
  for (l = accounts; l != NULL; l = g_list_next (l))
    {
      TpAccount *account = l->data;
      TpConnection *conn;
      TpContactFeature features[] = { TP_CONTACT_FEATURE_ALIAS,
          TP_CONTACT_FEATURE_AVATAR_DATA,
          TP_CONTACT_FEATURE_PRESENCE,
          TP_CONTACT_FEATURE_CAPABILITIES };

      conn = tp_account_get_connection (account);
      if (conn == NULL)
        continue;

      /* One day we'll have tp_connection_get_account()... */
      g_object_set_data_full (G_OBJECT (conn), "account",
          g_object_ref (account), g_object_unref);

      tp_connection_get_contacts_by_id (conn, 1, &id, G_N_ELEMENTS (features),
          features, get_contacts_cb, self->priv->add_temp_ctx, NULL,
          G_OBJECT (self));
    }

  g_list_free (accounts);
}

static void
search_text_changed (GtkEntry *entry,
    EmpathyInviteParticipantDialog *self)
{
  const gchar *id;

  tp_clear_pointer (&self->priv->search_words, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->search_str, g_free);

  id = gtk_entry_get_text (entry);

  self->priv->search_words = empathy_live_search_strip_utf8_string (id);
  self->priv->search_str = g_strdup (id);

  add_temporary_individuals (self, id);

  empathy_individual_view_refilter (self->priv->view);
}

static void
empathy_invite_participant_dialog_init (EmpathyInviteParticipantDialog *self)
{
  GtkDialog *dialog = GTK_DIALOG (self);
  GtkWidget *label;
  char *str;
  GtkWidget *content;
  EmpathyIndividualManager *mgr;
  GtkTreeSelection *selection;
  GtkWidget *scroll;
  GtkWidget *search_entry;
  GQuark features[] = { TP_ACCOUNT_MANAGER_FEATURE_CORE, 0 };

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG,
      EmpathyInviteParticipantDialogPrivate);

  self->priv->account_mgr = tp_account_manager_dup ();

  /* We don't wait for the CORE feature to be prepared, which is fine as we
   * won't use the account manager until user starts searching. Furthermore,
   * the AM has probably already been prepared by another Empathy
   * component. */
  tp_proxy_prepare_async (self->priv->account_mgr, features, NULL, NULL);

  content = gtk_dialog_get_content_area (dialog);

  label = gtk_label_new (NULL);
  str = g_strdup_printf (
      "<span size=\"x-large\" weight=\"bold\">%s</span>\n\n%s",
      _("Invite Participant"),
      _("Choose a contact to invite into the conversation:"));
  gtk_label_set_markup (GTK_LABEL (label), str);
  g_free (str);

  gtk_box_pack_start (GTK_BOX (content), label, FALSE, TRUE, 6);
  gtk_widget_show (label);

  gtk_dialog_add_button (dialog, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);

  /* Search entry */
  search_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (content), search_entry, FALSE, TRUE, 6);
  gtk_widget_show (search_entry);

  g_signal_connect (search_entry, "changed",
      G_CALLBACK (search_text_changed), self);

  /* Add the treeview */
  mgr = empathy_individual_manager_dup_singleton ();
  self->priv->store = empathy_individual_store_new (mgr);
  g_object_unref (mgr);

  empathy_individual_store_set_show_groups (self->priv->store, FALSE);

  self->priv->view = empathy_individual_view_new (self->priv->store,
      EMPATHY_INDIVIDUAL_VIEW_FEATURE_NONE, EMPATHY_INDIVIDUAL_FEATURE_NONE);

  empathy_individual_view_set_custom_filter (self->priv->view,
      filter_func, self);

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->priv->view));

  g_signal_connect (selection, "changed",
      G_CALLBACK (view_selection_changed_cb), self);

  scroll = gtk_scrolled_window_new (NULL, NULL);

  gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (self->priv->view));

  gtk_box_pack_start (GTK_BOX (content), scroll, TRUE, TRUE, 6);
  gtk_widget_show (GTK_WIDGET (self->priv->view));
  gtk_widget_show (scroll);

  self->priv->invite_button = gtk_dialog_add_button (dialog, _("Invite"),
      GTK_RESPONSE_ACCEPT);
  gtk_widget_set_sensitive (self->priv->invite_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (self), _("Invite Participant"));
  gtk_window_set_role (GTK_WINDOW (self), "invite_participant");

  /* Set a default height so a few contacts are displayed */
  gtk_window_set_default_size (GTK_WINDOW (self), -1, 400);
}

GtkWidget *
empathy_invite_participant_dialog_new (GtkWindow *parent,
    EmpathyTpChat *tp_chat)
{
  GtkWidget *self = g_object_new (EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG,
      "tp-chat", tp_chat,
      NULL);

  if (parent != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (self), parent);
    }

  return self;
}

TpContact *
empathy_invite_participant_dialog_get_selected (
    EmpathyInviteParticipantDialog *self)
{
  FolksIndividual *individual;
  TpContact *contact;

  individual = empathy_individual_view_dup_selected (self->priv->view);
  if (individual == NULL)
    return NULL;

  contact = get_tp_contact_for_chat (self, individual);

  g_object_unref (individual);
  return contact;
}

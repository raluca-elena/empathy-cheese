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

G_DEFINE_TYPE (EmpathyInviteParticipantDialog,
    empathy_invite_participant_dialog, GTK_TYPE_DIALOG);

enum
{
  PROP_TP_CHAT = 1
};

struct _EmpathyInviteParticipantDialogPrivate
{
  EmpathyTpChat *tp_chat;

  EmpathyIndividualStore *store;
  EmpathyIndividualView *view;

  GtkWidget *invite_button;
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

static void
invite_participant_dialog_dispose (GObject *object)
{
  EmpathyInviteParticipantDialog *self = (EmpathyInviteParticipantDialog *)
    object;

  tp_clear_object (&self->priv->tp_chat);
  tp_clear_object (&self->priv->store);

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
  GList *personas, *l;
  TpConnection *chat_conn;

  chat_conn = tp_channel_borrow_connection ((TpChannel *) self->priv->tp_chat);

  personas = folks_individual_get_personas (individual);

  for (l = personas; l != NULL; l = g_list_next (l))
    {
      TpfPersona *persona = l->data;
      TpContact *contact;
      TpConnection *contact_conn;

      if (!TPF_IS_PERSONA (persona))
        continue;

      contact = tpf_persona_get_contact (persona);
      if (contact == NULL)
        continue;

      contact_conn = tp_contact_get_connection (contact);

      if (!tp_strdiff (tp_proxy_get_object_path (contact_conn),
            tp_proxy_get_object_path (chat_conn)))
        return contact;
    }

  return NULL;
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

  if (individual == NULL || !is_online)
    goto out;

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
empathy_invite_participant_dialog_init (EmpathyInviteParticipantDialog *self)
{
  GtkDialog *dialog = GTK_DIALOG (self);
  GtkWidget *label;
  char *str;
  GtkWidget *content;
  EmpathyIndividualManager *mgr;
  GtkTreeSelection *selection;
  GtkWidget *scroll;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG,
      EmpathyInviteParticipantDialogPrivate);

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

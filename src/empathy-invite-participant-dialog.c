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

#include <libempathy/empathy-utils.h>

#include <libempathy-gtk/empathy-contact-chooser.h>
#include <libempathy-gtk/empathy-individual-view.h>
#include <libempathy-gtk/empathy-ui-utils.h>

G_DEFINE_TYPE (EmpathyInviteParticipantDialog,
    empathy_invite_participant_dialog, GTK_TYPE_DIALOG);

enum
{
  PROP_TP_CHAT = 1
};

struct _EmpathyInviteParticipantDialogPrivate
{
  EmpathyTpChat *tp_chat;

  GtkWidget *chooser;
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

  G_OBJECT_CLASS (empathy_invite_participant_dialog_parent_class)->dispose (
      object);
}

static void
selection_changed_cb (GtkWidget *treeview,
    FolksIndividual *selected,
    EmpathyInviteParticipantDialog *self)
{
  gtk_widget_set_sensitive (self->priv->invite_button, selected != NULL);
}

static void
activate_cb (GtkWidget *chooser,
    EmpathyInviteParticipantDialog *self)
{
  gtk_dialog_response (GTK_DIALOG (self), GTK_RESPONSE_ACCEPT);
}

/* Return the TpContact of @individual which is on the same connection as the
 * EmpathyTpChat */
static TpContact *
get_tp_contact_for_chat (EmpathyInviteParticipantDialog *self,
    FolksIndividual *individual)
{
  TpConnection *chat_conn;

  chat_conn = tp_channel_borrow_connection (TP_CHANNEL (self->priv->tp_chat));

  return empathy_get_tp_contact_for_individual (individual, chat_conn);
}

static gboolean
filter_individual (EmpathyContactChooser *chooser,
    FolksIndividual *individual,
    gboolean is_online,
    gboolean searching,
    gpointer user_data)
{
  EmpathyInviteParticipantDialog *self = user_data;
  GList *members, *l;
  TpContact *contact;
  gboolean display = TRUE;

  /* Filter out offline contacts if we are not searching */
  if (!searching && !is_online)
    return FALSE;

  /* Filter out individuals not having a persona on the same connection as the
   * EmpathyTpChat. */
  contact = get_tp_contact_for_chat (self, individual);

  if (contact == NULL)
    return FALSE;

  /* Filter out contacts which are already in the chat */
  members = empathy_contact_list_get_members (EMPATHY_CONTACT_LIST (
        self->priv->tp_chat));

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

  return display;
}

static gboolean
has_contact_list (EmpathyInviteParticipantDialog *self)
{
  TpConnection *conn;

  conn = tp_channel_borrow_connection (TP_CHANNEL (self->priv->tp_chat));

  return tp_proxy_has_interface_by_id (conn,
      TP_IFACE_QUARK_CONNECTION_INTERFACE_CONTACT_LIST);
}

static void
invite_participant_dialog_constructed (GObject *object)
{
  EmpathyInviteParticipantDialog *self =
    (EmpathyInviteParticipantDialog *) object;
  GtkDialog *dialog = GTK_DIALOG (self);
  GtkWidget *label;
  char *str;
  GtkWidget *content;

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

  /* contact chooser */
  self->priv->chooser = empathy_contact_chooser_new ();

  empathy_contact_chooser_set_filter_func (
      EMPATHY_CONTACT_CHOOSER (self->priv->chooser), filter_individual, self);

  gtk_box_pack_start (GTK_BOX (content), self->priv->chooser, TRUE, TRUE, 6);
  gtk_widget_show (self->priv->chooser);

  g_signal_connect (self->priv->chooser, "selection-changed",
      G_CALLBACK (selection_changed_cb), self);
  g_signal_connect (self->priv->chooser, "activate",
      G_CALLBACK (activate_cb), self);

  self->priv->invite_button = gtk_dialog_add_button (dialog, _("Invite"),
      GTK_RESPONSE_ACCEPT);
  gtk_widget_set_sensitive (self->priv->invite_button, FALSE);

  gtk_window_set_title (GTK_WINDOW (self), _("Invite Participant"));
  gtk_window_set_role (GTK_WINDOW (self), "invite_participant");

  if (has_contact_list (self))
    {
      /* Set a default height so a few contacts are displayed */
      gtk_window_set_default_size (GTK_WINDOW (self), -1, 400);
    }
  else
    {
      /* No need to display an empty treeview (ie IRC) */
      empathy_contact_chooser_show_tree_view (
          EMPATHY_CONTACT_CHOOSER (self->priv->chooser), FALSE);
    }
}

static void
empathy_invite_participant_dialog_class_init (
    EmpathyInviteParticipantDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = invite_participant_dialog_get_property;
  object_class->set_property = invite_participant_dialog_set_property;
  object_class->constructed = invite_participant_dialog_constructed;
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
empathy_invite_participant_dialog_init (EmpathyInviteParticipantDialog *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (
      self, EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG,
      EmpathyInviteParticipantDialogPrivate);
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

  individual = empathy_contact_chooser_dup_selected (
      EMPATHY_CONTACT_CHOOSER (self->priv->chooser));
  if (individual == NULL)
    return NULL;

  contact = get_tp_contact_for_chat (self, individual);

  g_object_unref (individual);
  return contact;
}

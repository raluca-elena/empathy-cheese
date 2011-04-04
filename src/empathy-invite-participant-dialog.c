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

#include "empathy-invite-participant-dialog.h"

G_DEFINE_TYPE (EmpathyInviteParticipantDialog,
    empathy_invite_participant_dialog, EMPATHY_TYPE_CONTACT_SELECTOR_DIALOG);

static void
empathy_invite_participant_dialog_class_init (EmpathyInviteParticipantDialogClass *klass)
{
}

static void
empathy_invite_participant_dialog_init (EmpathyInviteParticipantDialog *self)
{
  EmpathyContactSelectorDialog *parent = EMPATHY_CONTACT_SELECTOR_DIALOG (self);
  GtkWidget *label;
  char *str;

  label = gtk_label_new (NULL);
  str = g_strdup_printf (
      "<span size=\"x-large\" weight=\"bold\">%s</span>\n\n%s",
      _("Invite Participant"),
      _("Choose a contact to invite into the conversation:"));
  gtk_label_set_markup (GTK_LABEL (label), str);
  g_free (str);

  gtk_box_pack_start (GTK_BOX (parent->vbox), label, FALSE, TRUE, 0);
  /* move to the top -- wish there was a better way to do this */
  gtk_box_reorder_child (GTK_BOX (parent->vbox), label, 0);
  gtk_widget_show (label);

  parent->button_action = gtk_dialog_add_button (GTK_DIALOG (self),
      _("Invite"), GTK_RESPONSE_ACCEPT);
  gtk_widget_set_sensitive (parent->button_action, FALSE);

  gtk_window_set_title (GTK_WINDOW (self), _("Invite Participant"));
  gtk_window_set_role (GTK_WINDOW (self), "invite_participant");
  empathy_contact_selector_dialog_set_show_account_chooser (
      EMPATHY_CONTACT_SELECTOR_DIALOG (self), FALSE);
}

GtkWidget *
empathy_invite_participant_dialog_new (GtkWindow *parent,
    TpAccount *account)
{
  GtkWidget *self = g_object_new (EMPATHY_TYPE_INVITE_PARTICIPANT_DIALOG,
      "filter-account", account,
      NULL);

  if (parent != NULL)
    {
      gtk_window_set_transient_for (GTK_WINDOW (self), parent);
    }

  return self;
}

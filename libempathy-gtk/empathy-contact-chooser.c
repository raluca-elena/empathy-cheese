/*
 * empathy-contact-chooser.c
 *
 * EmpathyContactChooser
 *
 * (c) 2009, Collabora Ltd.
 *
 * Authors:
 *    Danielle Madeley <danielle.madeley@collabora.co.uk>
 */

#include <glib/gi18n.h>
#include <folks/folks-telepathy.h>

#include "empathy-contact-chooser.h"

#include <libempathy-gtk/empathy-individual-view.h>
#include <libempathy-gtk/empathy-ui-utils.h>

G_DEFINE_TYPE (EmpathyContactChooser,
    empathy_contact_chooser, GTK_TYPE_BOX);

enum {
  SIG_SELECTION_CHANGED,
  SIG_ACTIVATE,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

typedef struct _AddTemporaryIndividualCtx AddTemporaryIndividualCtx;

struct _EmpathyContactChooserPrivate
{
  TpAccountManager *account_mgr;

  EmpathyIndividualStore *store;
  EmpathyIndividualView *view;
  GtkWidget *search_entry;

  GPtrArray *search_words;
  gchar *search_str;

  /* Context representing the FolksIndividual which are added because of the
   * current search from the user. */
  AddTemporaryIndividualCtx *add_temp_ctx;

  EmpathyContactChooserFilterFunc filter_func;
  gpointer filter_data;
};

struct _AddTemporaryIndividualCtx
{
  EmpathyContactChooser *self;
  /* List of owned FolksIndividual */
  GList *individuals;
};

static AddTemporaryIndividualCtx *
add_temporary_individual_ctx_new (EmpathyContactChooser *self)
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
contact_chooser_dispose (GObject *object)
{
  EmpathyContactChooser *self = (EmpathyContactChooser *)
    object;

  tp_clear_pointer (&self->priv->add_temp_ctx,
      add_temporary_individual_ctx_free);

  tp_clear_object (&self->priv->store);
  tp_clear_pointer (&self->priv->search_words, g_ptr_array_unref);
  tp_clear_pointer (&self->priv->search_str, g_free);

  tp_clear_object (&self->priv->account_mgr);

  G_OBJECT_CLASS (empathy_contact_chooser_parent_class)->dispose (
      object);
}

static void
empathy_contact_chooser_class_init (
    EmpathyContactChooserClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = contact_chooser_dispose;

  g_type_class_add_private (object_class,
      sizeof (EmpathyContactChooserPrivate));

  signals[SIG_SELECTION_CHANGED] = g_signal_new ("selection-changed",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL, NULL,
            g_cclosure_marshal_generic,
            G_TYPE_NONE,
            1, FOLKS_TYPE_INDIVIDUAL);

  signals[SIG_ACTIVATE] = g_signal_new ("activate",
            G_TYPE_FROM_CLASS (klass),
            G_SIGNAL_RUN_LAST,
            0,
            NULL, NULL,
            g_cclosure_marshal_generic,
            G_TYPE_NONE,
            0);
}

static void
view_selection_changed_cb (GtkWidget *treeview,
    EmpathyContactChooser *self)
{
  FolksIndividual *individual;

  individual = empathy_individual_view_dup_selected (self->priv->view);

  g_signal_emit (self, signals[SIG_SELECTION_CHANGED], 0, individual);

  tp_clear_object (&individual);
}

static gboolean
filter_func (GtkTreeModel *model,
    GtkTreeIter *iter,
    gpointer user_data)
{
  EmpathyContactChooser *self = user_data;
  FolksIndividual *individual;
  gboolean is_online;
  gboolean display = FALSE;
  gboolean searching = FALSE;

  gtk_tree_model_get (model, iter,
      EMPATHY_INDIVIDUAL_STORE_COL_INDIVIDUAL, &individual,
      EMPATHY_INDIVIDUAL_STORE_COL_IS_ONLINE, &is_online,
      -1);

  if (individual == NULL)
    goto out;

  if (self->priv->search_words != NULL)
    {
      searching = TRUE;

      /* Filter out the contact if we are searching and it doesn't match */
      if (!empathy_individual_match_string (individual,
            self->priv->search_str, self->priv->search_words))
        goto out;
    }

  if (self->priv->filter_func == NULL)
    display = TRUE;
  else
    display = self->priv->filter_func (self, individual, is_online, searching,
      self->priv->filter_data);
out:
  tp_clear_object (&individual);
  return display;
}

static void
contact_capabilities_changed (TpContact *contact,
    GParamSpec *pspec,
    EmpathyContactChooser *self)
{
  empathy_individual_view_refilter (self->priv->view);
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
  EmpathyContactChooser *self =
    (EmpathyContactChooser *) weak_object;
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

  account = tp_connection_get_account (connection);

  store = tpf_persona_store_new (account);
  personas = GEE_SET (
      gee_hash_set_new (FOLKS_TYPE_PERSONA, g_object_ref, g_object_unref,
      g_direct_hash, g_direct_equal));
  persona_new = tpf_persona_new (contacts[0], store);
  gee_collection_add (GEE_COLLECTION (personas),
      tpf_persona_new (contacts[0], store));

  individual = folks_individual_new (personas);

  /* listen for updates to the capabilities */
  tp_g_signal_connect_object (contacts[0], "notify::capabilities",
      G_CALLBACK (contact_capabilities_changed), self, 0);

  /* Pass ownership to the list */
  ctx->individuals = g_list_prepend (ctx->individuals, individual);

  individual_store_add_individual_and_connect (self->priv->store, individual);

  /* if nothing is selected, select the first matching node */
  if (!gtk_tree_selection_get_selected (
        gtk_tree_view_get_selection (GTK_TREE_VIEW (self->priv->view)),
        NULL, NULL))
    empathy_individual_view_select_first (self->priv->view);

  g_clear_object (&persona_new);
  g_clear_object (&personas);
  g_object_unref (store);
}

static void
add_temporary_individuals (EmpathyContactChooser *self,
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

      tp_connection_get_contacts_by_id (conn, 1, &id, G_N_ELEMENTS (features),
          features, get_contacts_cb, self->priv->add_temp_ctx, NULL,
          G_OBJECT (self));
    }

  g_list_free (accounts);
}

static void
search_text_changed (GtkEntry *entry,
    EmpathyContactChooser *self)
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
search_activate_cb (GtkEntry *entry,
    EmpathyContactChooser *self)
{
  g_signal_emit (self, signals[SIG_ACTIVATE], 0);
}

static void
view_activate_cb (GtkTreeView *view,
    GtkTreePath *path,
    GtkTreeViewColumn *column,
    EmpathyContactChooser *self)
{
  g_signal_emit (self, signals[SIG_ACTIVATE], 0);
}

static gboolean
search_key_press_cb (GtkEntry *entry,
    GdkEventKey *event,
    EmpathyContactChooser *self)
{
  GtkTreeSelection *selection;
  GtkTreeModel *model;
  GtkTreeIter iter;

  if (event->state != 0)
    return FALSE;

  switch (event->keyval)
    {
      case GDK_KEY_Down:
      case GDK_KEY_KP_Down:
      case GDK_KEY_Up:
      case GDK_KEY_KP_Up:
        break;

      default:
        return FALSE;
    }

  selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (self->priv->view));

  if (!gtk_tree_selection_get_selected (selection, &model, &iter))
    return TRUE;

  switch (event->keyval)
    {
      case GDK_KEY_Down:
      case GDK_KEY_KP_Down:
        if (!gtk_tree_model_iter_next (model, &iter))
          return TRUE;

        break;

      case GDK_KEY_Up:
      case GDK_KEY_KP_Up:
        if (!gtk_tree_model_iter_previous (model, &iter))
          return TRUE;

        break;

      default:
        g_assert_not_reached ();
    }

  gtk_tree_selection_select_iter (selection, &iter);

  return TRUE;
}

static void
empathy_contact_chooser_init (EmpathyContactChooser *self)
{
  EmpathyIndividualManager *mgr;
  GtkTreeSelection *selection;
  GtkWidget *scroll;
  GQuark features[] = { TP_ACCOUNT_MANAGER_FEATURE_CORE, 0 };

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, EMPATHY_TYPE_CONTACT_CHOOSER,
      EmpathyContactChooserPrivate);

  self->priv->account_mgr = tp_account_manager_dup ();

  /* We don't wait for the CORE feature to be prepared, which is fine as we
   * won't use the account manager until user starts searching. Furthermore,
   * the AM has probably already been prepared by another Empathy
   * component. */
  tp_proxy_prepare_async (self->priv->account_mgr, features, NULL, NULL);

  /* Search entry */
  self->priv->search_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (self), self->priv->search_entry, FALSE, TRUE, 6);
  gtk_widget_show (self->priv->search_entry);

  g_signal_connect (self->priv->search_entry, "changed",
      G_CALLBACK (search_text_changed), self);
  g_signal_connect (self->priv->search_entry, "activate",
      G_CALLBACK (search_activate_cb), self);
  g_signal_connect (self->priv->search_entry, "key-press-event",
      G_CALLBACK (search_key_press_cb), self);

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
  g_signal_connect (self->priv->view, "row-activated",
      G_CALLBACK (view_activate_cb), self);

  scroll = gtk_scrolled_window_new (NULL, NULL);

  gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (self->priv->view));

  gtk_box_pack_start (GTK_BOX (self), scroll, TRUE, TRUE, 6);
  gtk_widget_show (GTK_WIDGET (self->priv->view));
  gtk_widget_show (scroll);
}

GtkWidget *
empathy_contact_chooser_new (void)
{
  return g_object_new (EMPATHY_TYPE_CONTACT_CHOOSER,
      "orientation", GTK_ORIENTATION_VERTICAL,
      NULL);
}

FolksIndividual *
empathy_contact_chooser_dup_selected (EmpathyContactChooser *self)
{
  return empathy_individual_view_dup_selected (self->priv->view);
}

void
empathy_contact_chooser_set_filter_func (EmpathyContactChooser *self,
    EmpathyContactChooserFilterFunc func,
    gpointer user_data)
{
  g_assert (self->priv->filter_func == NULL);

  self->priv->filter_func = func;
  self->priv->filter_data = user_data;
}

void
empathy_contact_chooser_show_search_entry (EmpathyContactChooser *self,
    gboolean show)
{
  gtk_widget_set_visible (self->priv->search_entry, show);
}

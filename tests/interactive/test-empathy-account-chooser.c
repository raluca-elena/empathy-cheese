#include <config.h>

#include <gtk/gtk.h>

#include <libempathy-gtk/empathy-ui-utils.h>
#include <libempathy-gtk/empathy-account-chooser.h>

static void
filter_func (TpAccount *account,
    EmpathyAccountChooserFilterResultCallback callback,
    gpointer callback_data,
    gpointer user_data)
{
  g_assert (TP_IS_ACCOUNT (account));
  callback (TRUE, callback_data);
}

int
main (int argc,
    char **argv)
{
  GtkWidget *window, *c;

  gtk_init (&argc, &argv);
  empathy_gtk_init ();

  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  c = empathy_account_chooser_new ();

  empathy_account_chooser_set_has_all_option (EMPATHY_ACCOUNT_CHOOSER (c),
      TRUE);

  empathy_account_chooser_set_filter (EMPATHY_ACCOUNT_CHOOSER (c),
      filter_func, NULL);

  gtk_container_add (GTK_CONTAINER (window), c);

  /*  gtk_window_set_default_size (GTK_WINDOW (window), 150, -1);*/
  gtk_widget_show_all (window);

  g_signal_connect_swapped (window, "destroy",
      G_CALLBACK (gtk_main_quit), NULL);

  gtk_main ();

  return 0;
}

AM_CPPFLAGS =						\
	$(ERROR_CFLAGS)					\
	-I$(top_srcdir)                          	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	$(EMPATHY_CFLAGS)				\
	$(EDS_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED)

LDADD =								\
	$(top_builddir)/libempathy-gtk/libempathy-gtk.la	\
	$(top_builddir)/libempathy/libempathy.la		\
	$(EMPATHY_LIBS)						\
	$(EDS_LIBS)

noinst_PROGRAMS =			\
	empathy-logs			\
	test-empathy-account-assistant \
	test-empathy-contact-blocking-dialog \
	test-empathy-presence-chooser	\
	test-empathy-status-preset-dialog \
	test-empathy-protocol-chooser \
	test-empathy-account-chooser

empathy_logs_SOURCES = empathy-logs.c
test_empathy_contact_blocking_dialog_SOURCES = test-empathy-contact-blocking-dialog.c
test_empathy_presence_chooser_SOURCES = test-empathy-presence-chooser.c
test_empathy_status_preset_dialog_SOURCES = test-empathy-status-preset-dialog.c
test_empathy_protocol_chooser_SOURCES = test-empathy-protocol-chooser.c
test_empathy_account_assistant_SOURCES = test-empathy-account-assistant.c
test_empathy_account_chooser_SOURCES = test-empathy-account-chooser.c

test_empathy_account_assistant_CFLAGS = -I$(top_srcdir)/src
test_empathy_account_assistant_LDADD = 			\
	$(top_builddir)/src/empathy-account-assistant.lo	\
	$(top_builddir)/src/empathy-auto-salut-account-helper.lo	\
	$(top_builddir)/src/empathy-import-pidgin.lo	\
	$(top_builddir)/src/empathy-import-utils.lo	\
	$(top_builddir)/src/empathy-import-widget.lo	\
	$(LDADD)

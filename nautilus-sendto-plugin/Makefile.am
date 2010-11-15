plugindir = $(libdir)/nautilus-sendto/plugins

INCLUDES =					\
	-I$(top_builddir)			\
	-I$(top_srcdir)				\
	-DLOCALEDIR=\""$(datadir)/locale"\"	\
	$(NST_CFLAGS)				\
	$(WARN_CFLAGS)				\
	$(DISABLE_DEPRECATED)			\
	$(EMPATHY_CFLAGS)

plugin_LTLIBRARIES = libnstempathy.la

libnstempathy_la_SOURCES = empathy-nautilus-sendto.c
libnstempathy_la_LDFLAGS = -module -avoid-version
libnstempathy_la_LIBADD =				\
	$(NST_LIBS)					\
	$(top_builddir)/libempathy-gtk/libempathy-gtk.la


include $(top_srcdir)/tools/flymake.mk

AM_CPPFLAGS =                                           \
	$(ERROR_CFLAGS)					\
	-I.                                             \
	-I$(top_srcdir)                                 \
	-DDATADIR=\""$(datadir)"\"              	\
	-DPKGDATADIR=\""$(pkgdatadir)"\"		\
	-DGCR_API_SUBJECT_TO_CHANGE			\
	$(EMPATHY_CFLAGS)				\
	$(YELL_CFLAGS)					\
	$(ENCHANT_CFLAGS)				\
	$(LIBCHAMPLAIN_CFLAGS)				\
	$(GEOCLUE_CFLAGS)				\
	$(GEOCODE_CFLAGS)				\
	$(MEEGO_CFLAGS)					\
	$(CHEESE_CFLAGS)				\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED)

BUILT_SOURCES = 					\
	empathy-gtk-enum-types.h			\
	empathy-gtk-enum-types.c

noinst_LTLIBRARIES = libempathy-gtk.la

libempathy_gtk_handwritten_source =            	\
	empathy-account-chooser.c		\
	empathy-account-selector-dialog.c		\
	empathy-account-widget-irc.c		\
	empathy-account-widget-private.h	\
	empathy-account-widget-sip.c		\
	empathy-account-widget.c		\
	empathy-avatar-chooser.c		\
	empathy-avatar-image.c			\
	empathy-call-utils.c			\
	empathy-cell-renderer-activatable.c	\
	empathy-cell-renderer-expander.c	\
	empathy-cell-renderer-text.c		\
	empathy-chat-text-view.c		\
	empathy-chat-view.c			\
	empathy-chat.c				\
	empathy-contact-blocking-dialog.c	\
	empathy-contact-dialogs.c		\
	empathy-contact-chooser.c		\
	empathy-contact-list-store.c		\
	empathy-contact-list-view.c		\
	empathy-contact-menu.c			\
	empathy-linking-dialog.c		\
	empathy-live-search.c			\
	empathy-contact-search-dialog.c		\
	empathy-contact-widget.c		\
	empathy-dialpad-widget.c		\
	empathy-geometry.c			\
	empathy-groups-widget.c			\
	empathy-individual-dialogs.c		\
	empathy-individual-edit-dialog.c	\
	empathy-individual-information-dialog.c	\
	empathy-individual-linker.c		\
	empathy-individual-menu.c		\
	empathy-individual-store.c		\
	empathy-individual-view.c		\
	empathy-individual-widget.c		\
	empathy-input-text-view.c		\
	empathy-irc-network-chooser.c \
	empathy-irc-network-chooser-dialog.c \
	empathy-irc-network-dialog.c		\
	empathy-log-window.c			\
	empathy-new-message-dialog.c		\
	empathy-new-call-dialog.c		\
	empathy-notify-manager.c		\
	empathy-password-dialog.c 		\
	empathy-persona-store.c			\
	empathy-persona-view.c			\
	empathy-presence-chooser.c		\
	empathy-protocol-chooser.c		\
	empathy-search-bar.c			\
	empathy-share-my-desktop.c		\
	empathy-smiley-manager.c		\
	empathy-sound-manager.c			\
	empathy-spell.c				\
	empathy-status-preset-dialog.c		\
	empathy-string-parser.c			\
	empathy-theme-boxes.c			\
	empathy-theme-irc.c			\
	empathy-theme-manager.c			\
	empathy-tls-dialog.c			\
	empathy-ui-utils.c			\
	empathy-plist.c				\
	empathy-theme-adium.c			\
	empathy-webkit-utils.c			\
	$(NULL)

libempathy_gtk_headers =			\
	empathy-account-chooser.h		\
	empathy-account-selector-dialog.h		\
	empathy-account-widget-irc.h		\
	empathy-account-widget-sip.h		\
	empathy-account-widget.h		\
	empathy-avatar-chooser.h		\
	empathy-avatar-image.h			\
	empathy-call-utils.h			\
	empathy-cell-renderer-activatable.h	\
	empathy-cell-renderer-expander.h	\
	empathy-cell-renderer-text.h		\
	empathy-chat-text-view.h		\
	empathy-chat-view.h			\
	empathy-chat.h				\
	empathy-contact-blocking-dialog.h	\
	empathy-contact-dialogs.h		\
	empathy-contact-chooser.h		\
	empathy-contact-list-store.h		\
	empathy-contact-list-view.h		\
	empathy-contact-menu.h			\
	empathy-linking-dialog.h		\
	empathy-live-search.h			\
	empathy-contact-search-dialog.h		\
	empathy-contact-widget.h		\
	empathy-dialpad-widget.h		\
	empathy-geometry.h			\
	empathy-groups-widget.h			\
	empathy-images.h			\
	empathy-individual-dialogs.h		\
	empathy-individual-edit-dialog.h	\
	empathy-individual-information-dialog.h	\
	empathy-individual-linker.h		\
	empathy-individual-menu.h		\
	empathy-individual-store.h		\
	empathy-individual-view.h		\
	empathy-individual-widget.h		\
	empathy-input-text-view.h		\
	empathy-irc-network-chooser.h \
	empathy-irc-network-chooser-dialog.h \
	empathy-irc-network-dialog.h		\
	empathy-log-window.h			\
	empathy-new-message-dialog.h		\
	empathy-new-call-dialog.h		\
	empathy-notify-manager.h		\
	empathy-password-dialog.h		\
	empathy-persona-store.h			\
	empathy-persona-view.h			\
	empathy-presence-chooser.h		\
	empathy-protocol-chooser.h		\
	empathy-search-bar.h			\
	empathy-share-my-desktop.h		\
	empathy-smiley-manager.h		\
	empathy-sound-manager.h			\
	empathy-spell.h				\
	empathy-status-preset-dialog.h		\
	empathy-string-parser.h			\
	empathy-theme-boxes.h			\
	empathy-theme-irc.h			\
	empathy-theme-manager.h			\
	empathy-tls-dialog.h			\
	empathy-ui-utils.h			\
	empathy-plist.h				\
	empathy-theme-adium.h			\
	empathy-webkit-utils.h			\
	$(NULL)


libempathy_gtk_la_SOURCES =			\
	$(libempathy_gtk_handwritten_source)	\
	$(libempathy_gtk_headers)		\
	totem-subtitle-encoding.c totem-subtitle-encoding.h

# do not distribute generated files
nodist_libempathy_gtk_la_SOURCES =\
	$(BUILT_SOURCES)

libempathy_gtk_la_LIBADD =			\
	$(EMPATHY_LIBS)			\
	$(EMPATHY_LIBS)			\
	$(GTK_LIBS)			\
	$(LIBNOTIFY_LIBS)			\
	$(CANBERRA_LIBS) \
	$(ENCHANT_LIBS)				\
	$(LIBCHAMPLAIN_LIBS)			\
	$(GEOCLUE_LIBS)				\
	$(GEOCODE_LIBS)				\
	$(GCR_LIBS)				\
	$(MEEGO_LIBS)				\
	$(CHEESE_LIBS)				\
	$(top_builddir)/libempathy/libempathy.la

check_c_sources = \
    $(libempathy_gtk_handwritten_source) \
    $(libempathy_gtk_headers)
include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

uidir = $(datadir)/empathy
ui_DATA = 					\
	empathy-contact-widget.ui		\
	empathy-contact-dialogs.ui		\
	empathy-contact-blocking-dialog.ui	\
	empathy-account-widget-generic.ui	\
	empathy-account-widget-jabber.ui	\
	empathy-account-widget-msn.ui		\
	empathy-account-widget-sip.ui		\
	empathy-account-widget-local-xmpp.ui	\
	empathy-account-widget-irc.ui		\
	empathy-account-widget-icq.ui		\
	empathy-account-widget-yahoo.ui		\
	empathy-account-widget-groupwise.ui	\
	empathy-account-widget-aim.ui		\
	empathy-status-preset-dialog.ui		\
	empathy-log-window.ui			\
	empathy-chat.ui				\
	empathy-search-bar.ui			\
	empathy-individual-widget.ui

empathy-gtk-enum-types.h: stamp-empathy-gtk-enum-types.h
	$(AM_V_GEN)true
stamp-empathy-gtk-enum-types.h: Makefile $(libempathy_gtk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#ifndef __LIBEMPATHY_GTK_ENUM_TYPES_H__\n" \
			--fhead "#define __LIBEMPATHY_GTK_ENUM_TYPES_H__ 1\n\n" \
			--fhead "#include <glib-object.h>\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* __LIBEMPATHY_GTK_ENUM_TYPES_H__ */\n" \
			--fprod "#include <libempathy-gtk/@filename@>\n" \
			--eprod "#define EMPATHY_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(libempathy_gtk_headers) ) > xgen-gth \
	&& (cmp -s xgen-gth empathy-gtk-enum-type.h || cp xgen-gth empathy-gtk-enum-types.h) \
	&& rm -f xgen-gth \
	&& echo timestamp > $(@F)

empathy-gtk-enum-types.c: Makefile $(libempathy_gtk_headers)
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#include <config.h>\n" \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"empathy-gtk-enum-types.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(libempathy_gtk_headers) ) > xgen-gtc \
	&& cp xgen-gtc $(@F) \
	&& rm -f xgen-gtc

EXTRA_DIST =			\
	$(ui_DATA)

if HAVE_GEOCLUE
libempathy_gtk_handwritten_source +=		\
	empathy-location-manager.c

libempathy_gtk_headers +=			\
	empathy-location-manager.h
else
EXTRA_DIST +=					\
	empathy-location-manager.c		\
	empathy-location-manager.h
endif

CLEANFILES =				\
	$(BUILT_SOURCES)		\
	stamp-empathy-gtk-enum-types.h

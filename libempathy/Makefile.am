include $(top_srcdir)/tools/flymake.mk

AM_CPPFLAGS =                                           \
	$(ERROR_CFLAGS)					\
	-I.                                             \
	-I$(top_srcdir)                                 \
	-I$(top_builddir)                               \
	-DDATADIR=\""$(datadir)"\"              	\
	-DLOCALEDIR=\""$(datadir)/locale"\"		\
	-DG_LOG_DOMAIN=\"empathy\"			\
	-DGCR_API_SUBJECT_TO_CHANGE			\
	$(EMPATHY_CFLAGS)				\
	$(YELL_CFLAGS)					\
	$(GEOCLUE_CFLAGS)				\
	$(GEOCODE_CFLAGS)				\
	$(NETWORK_MANAGER_CFLAGS)			\
	$(CONNMAN_CFLAGS)				\
	$(UDEV_CFLAGS)					\
	$(GOA_CFLAGS)					\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED)

BUILT_SOURCES = 					\
	empathy-enum-types.h				\
	empathy-enum-types.c

noinst_LTLIBRARIES = libempathy.la

libempathy_headers =				\
	action-chain-internal.h			\
	empathy-account-settings.h		\
	empathy-auth-factory.h			\
	empathy-camera-monitor.h		\
	empathy-chatroom-manager.h		\
	empathy-chatroom.h			\
	empathy-client-factory.h \
	empathy-connection-managers.h		\
	empathy-connectivity.h			\
	empathy-contact-groups.h		\
	empathy-contact-list.h			\
	empathy-contact-manager.h		\
	empathy-contact.h			\
	empathy-debug.h				\
	empathy-ft-factory.h			\
	empathy-ft-handler.h			\
	empathy-gsettings.h			\
	empathy-presence-manager.h				\
	empathy-individual-manager.h		\
	empathy-irc-network-manager.h		\
	empathy-irc-network.h			\
	empathy-irc-server.h			\
	empathy-keyring.h 			\
	empathy-location.h			\
	empathy-message.h			\
	empathy-request-util.h			\
	empathy-server-sasl-handler.h		\
	empathy-server-tls-handler.h		\
	empathy-status-presets.h		\
	empathy-time.h				\
	empathy-tls-certificate.h		\
	empathy-tls-verifier.h			\
	empathy-tp-chat.h			\
	empathy-tp-contact-factory.h		\
	empathy-tp-contact-list.h		\
	empathy-tp-file.h			\
	empathy-tp-roomlist.h			\
	empathy-tp-streamed-media.h		\
	empathy-types.h				\
	empathy-utils.h

libempathy_handwritten_source =				\
	$(libempathy_headers)				\
	action-chain.c					\
	empathy-account-settings.c			\
	empathy-auth-factory.c				\
	empathy-camera-monitor.c			\
	empathy-chatroom-manager.c			\
	empathy-chatroom.c				\
	empathy-client-factory.c \
	empathy-connection-managers.c			\
	empathy-connectivity.c				\
	empathy-contact-groups.c			\
	empathy-contact-list.c				\
	empathy-contact-manager.c			\
	empathy-contact.c				\
	empathy-debug.c					\
	empathy-ft-factory.c				\
	empathy-ft-handler.c				\
	empathy-presence-manager.c					\
	empathy-individual-manager.c			\
	empathy-irc-network-manager.c			\
	empathy-irc-network.c				\
	empathy-irc-server.c				\
	empathy-keyring.c				\
	empathy-message.c				\
	empathy-request-util.c				\
	empathy-server-sasl-handler.c			\
	empathy-server-tls-handler.c			\
	empathy-status-presets.c			\
	empathy-time.c					\
	empathy-tls-certificate.c			\
	empathy-tls-verifier.c				\
	empathy-tp-chat.c				\
	empathy-tp-contact-factory.c			\
	empathy-tp-contact-list.c			\
	empathy-tp-file.c				\
	empathy-tp-roomlist.c				\
	empathy-tp-streamed-media.c			\
	empathy-utils.c

# these are sources that depend on GOA
goa_sources = \
	empathy-goa-auth-handler.c \
	empathy-goa-auth-handler.h \
	$(NULL)

libempathy_la_SOURCES = \
	$(libempathy_handwritten_source) \
	cheese-camera-device-monitor.c cheese-camera-device-monitor.h

dtddir = $(datadir)/empathy
dtd_DATA = 					\
	empathy-status-presets.dtd		\
	empathy-contact-groups.dtd		\
	empathy-chatroom-manager.dtd		\
	empathy-irc-networks.dtd

ircnetworksdir = $(datadir)/empathy
ircnetworks_DATA =		\
	irc-networks.xml

EXTRA_DIST = 			\
	$(dtd_DATA)		\
	$(ircnetworks_DATA)

CLEANFILES = 				\
	$(BUILT_SOURCES)		\
	stamp-empathy-enum-types.h

if HAVE_GOA
libempathy_la_SOURCES += $(goa_sources)
else
EXTRA_DIST += $(goa_sources)
endif

# do not distribute generated files
nodist_libempathy_la_SOURCES =\
	$(BUILT_SOURCES)

libempathy_la_LIBADD =		\
	$(top_builddir)/extensions/libemp-extensions.la \
	$(top_builddir)/telepathy-yell/telepathy-yell/libtelepathy-yell.la \
	$(GCR_LIBS) \
	$(EMPATHY_LIBS) \
	$(GEOCLUE_LIBS) \
	$(GEOCODE_LIBS) \
	$(NETWORK_MANAGER_LIBS) \
	$(CONNMAN_LIBS) \
	$(UDEV_LIBS) \
	$(GOA_LIBS) \
	$(LIBM)

check_c_sources = \
    $(libempathy_handwritten_source) \
    $(goa_sources) \
    $(NULL)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

empathy-enum-types.h: stamp-empathy-enum-types.h
	$(AM_V_GEN)true
stamp-empathy-enum-types.h: $(libempathy_headers) Makefile
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#ifndef __LIBEMPATHY_ENUM_TYPES_H__\n" \
			--fhead "#define __LIBEMPATHY_ENUM_TYPES_H__ 1\n\n" \
			--fhead "#include <glib-object.h>\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* __LIBEMPATHY_ENUM_TYPES_H__ */\n" \
			--fprod "#include <libempathy/@filename@>\n" \
			--eprod "#define EMPATHY_TYPE_@ENUMSHORT@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(libempathy_headers) ) > xgen-gth \
	&& (cmp -s xgen-gth empathy-enum-type.h || cp xgen-gth empathy-enum-types.h) \
	&& rm -f xgen-gth \
	&& echo timestamp > $(@F)

empathy-enum-types.c: $(libempathy_headers) Makefile
	$(AM_V_GEN)(cd $(srcdir) \
	&& glib-mkenums \
			--fhead "#include <config.h>\n" \
			--fhead "#include <glib-object.h>\n" \
			--fhead "#include \"empathy-enum-types.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(libempathy_headers) ) > xgen-gtc \
	&& cp xgen-gtc $(@F) \
	&& rm -f xgen-gtc

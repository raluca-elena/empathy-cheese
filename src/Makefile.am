include $(top_srcdir)/tools/flymake.mk

CPPFLAGS_COMMON =					\
	$(YELL_CFLAGS)					\
	$(EMPATHY_CFLAGS)				\
	$(EDS_CFLAGS)					\
	$(ERROR_CFLAGS)					\
	-I$(top_srcdir)                          	\
	-DG_LOG_DOMAIN=\"empathy\"			\
	-DBIN_DIR=\"$(bindir)\"                         \
	$(DISABLE_DEPRECATED)				\
	$(WARN_CFLAGS)					\
	$(NULL)

AM_LDFLAGS = $(LIBM)
AM_CPPFLAGS =						\
	$(CPPFLAGS_COMMON)				\
	$(LIBCHAMPLAIN_CFLAGS)				\
	$(NULL)

LDADD =								\
	$(top_builddir)/libempathy-gtk/libempathy-gtk.la	\
	$(top_builddir)/libempathy/libempathy.la		\
	$(top_builddir)/extensions/libemp-extensions.la		\
	$(GCR_LIBS) 						\
	$(EMPATHY_LIBS)						\
	$(YELL_LIBS)						\
	$(LIBCHAMPLAIN_LIBS)					\
	$(NULL)

noinst_LTLIBRARIES = libempathy-accounts-common.la

libempathy_accounts_common_la_SOURCES =					\
	empathy-accounts-common.c empathy-accounts-common.h		\
	empathy-account-assistant.c empathy-account-assistant.h		\
	empathy-accounts-dialog.c empathy-accounts-dialog.h		\
	empathy-auto-salut-account-helper.c empathy-auto-salut-account-helper.h \
	empathy-import-dialog.c empathy-import-dialog.h			\
	empathy-import-pidgin.c empathy-import-pidgin.h			\
	empathy-import-widget.c empathy-import-widget.h			\
	empathy-import-utils.c empathy-import-utils.h			\
	$(NULL)

libempathy_accounts_common_la_LIBADD =					\
        $(top_builddir)/libempathy-gtk/libempathy-gtk.la		\
	$(EDS_LIBS)							\
        $(EMPATHY_LIBS)							\
        $(LIBCHAMPLAIN_LIBS)						\
	$(NULL)

bin_PROGRAMS =			\
	empathy			\
	empathy-accounts	\
	empathy-debugger	\
	$(NULL)

libexec_PROGRAMS = \
	empathy-auth-client \
	empathy-av \
	empathy-chat

empathy_accounts_SOURCES =						\
	empathy-accounts.c empathy-accounts.h				\
	$(NULL)

empathy_accounts_LDADD =						\
	$(LDADD)							\
	libempathy-accounts-common.la					\
	$(NULL)

empathy_debugger_SOURCES =						\
	empathy-debug-window.c empathy-debug-window.h			\
	empathy-debugger.c		 				\
	$(NULL)

empathy_av_SOURCES = \
	empathy-av.c \
	empathy-audio-sink.c \
	empathy-audio-sink.h \
	empathy-audio-src.c \
	empathy-audio-src.h \
	empathy-streamed-media-factory.c \
	empathy-streamed-media-factory.h \
	empathy-streamed-media-handler.c \
	empathy-streamed-media-handler.h \
	empathy-streamed-media-window-fullscreen.c \
	empathy-streamed-media-window-fullscreen.h \
	empathy-streamed-media-window.c \
	empathy-streamed-media-window.h \
	empathy-video-src.c \
	empathy-video-src.h \
	empathy-video-widget.c \
	empathy-video-widget.h \
	ev-sidebar.c \
	ev-sidebar.h \
	empathy-mic-monitor.c \
	empathy-mic-monitor.h
	$(NULL)

empathy_av_CFLAGS = $(EMPATHY_AV_CFLAGS)
empathy_av_LDFLAGS = $(EMPATHY_AV_LIBS) $(LIBM)

empathy_auth_client_SOURCES =						\
	empathy-auth-client.c \
	$(NULL)

empathy_chat_SOURCES =						\
	empathy-about-dialog.c empathy-about-dialog.h			\
	empathy-chat-manager.c empathy-chat-manager.h		\
	empathy-chat-window.c empathy-chat-window.h		\
	empathy-invite-participant-dialog.c empathy-invite-participant-dialog.h \
	empathy-chat.c \
	gedit-close-button.c gedit-close-button.h \
	$(NULL)

if HAVE_CALL

libexec_PROGRAMS += empathy-call

empathy_call_SOURCES = \
       empathy-call.c \
       empathy-call-factory.c \
       empathy-call-factory.h \
       empathy-call-handler.c \
       empathy-call-handler.h \
       empathy-call-window.c \
       empathy-call-window.h \
       empathy-call-window-fullscreen.c \
       empathy-call-window-fullscreen.h \
       empathy-about-dialog.c \
       empathy-about-dialog.h \
       empathy-audio-sink.c \
       empathy-audio-sink.h \
       empathy-audio-src.c \
       empathy-audio-src.h \
       empathy-video-src.c \
       empathy-video-src.h \
       empathy-video-widget.c \
       empathy-video-widget.h \
       empathy-preferences.c \
       empathy-preferences.h \
       ev-sidebar.c \
       ev-sidebar.h \
       empathy-camera-menu.c \
       empathy-camera-menu.h \
       empathy-mic-menu.c \
       empathy-mic-menu.h \
       empathy-rounded-actor.c \
       empathy-rounded-actor.h \
       empathy-rounded-rectangle.c \
       empathy-rounded-rectangle.h \
       empathy-rounded-texture.c \
       empathy-rounded-texture.h \
       empathy-mic-monitor.c \
       empathy-mic-monitor.h

empathy_call_CFLAGS = $(EMPATHY_CALL_CFLAGS) $(VIDEO_EFFECT_CFLAGS)
empathy_call_LDFLAGS = $(EMPATHY_CALL_LIBS) $(VIDEO_EFFECT_LIBS)

endif

empathy_handwritten_source = \
	empathy-about-dialog.c empathy-about-dialog.h			\
	empathy-chat-window.c empathy-chat-window.h			\
	empathy-chatrooms-window.c empathy-chatrooms-window.h		\
	empathy-event-manager.c empathy-event-manager.h			\
	empathy-ft-manager.c empathy-ft-manager.h			\
	empathy-invite-participant-dialog.c empathy-invite-participant-dialog.h \
	empathy-main-window.c empathy-main-window.h			\
	empathy-new-chatroom-dialog.c empathy-new-chatroom-dialog.h	\
	empathy-notifications-approver.c empathy-notifications-approver.h \
	empathy-call-observer.c empathy-call-observer.h			\
	empathy-preferences.c empathy-preferences.h			\
	empathy-status-icon.c empathy-status-icon.h			\
	empathy-chat-manager.c empathy-chat-manager.h			\
	gedit-close-button.c gedit-close-button.h \
	empathy.c

empathy_SOURCES =							\
	$(empathy_handwritten_source)					\
	$(NULL)

empathy_LDADD =								\
	libempathy-accounts-common.la					\
        $(top_builddir)/libempathy-gtk/libempathy-gtk.la		\
        $(top_builddir)/libempathy/libempathy.la			\
        $(top_builddir)/extensions/libemp-extensions.la			\
        $(EMPATHY_LIBS)							\
        $(LIBCHAMPLAIN_LIBS)						\
	$(NULL)

check_c_sources = \
    $(empathy_handwritten_source) \
    $(empathy_logs_SOURCES) \
    $(empathy_handwritten_av_source)

include $(top_srcdir)/tools/check-coding-style.mk
check-local: check-coding-style

uidir = $(datadir)/empathy
ui_DATA = 					\
	empathy-accounts-dialog.ui		\
	empathy-call-window-fullscreen.ui	\
	empathy-call-window.ui			\
	empathy-streamed-media-window.ui	\
	empathy-chat-window.ui			\
	empathy-chatrooms-window.ui		\
	empathy-ft-manager.ui			\
	empathy-import-dialog.ui		\
	empathy-main-window.ui			\
	empathy-main-window-menubar.ui		\
	empathy-new-chatroom-dialog.ui		\
	empathy-preferences.ui			\
	empathy-status-icon.ui

EXTRA_DIST =			\
	$(autostart_DATA)	\
	$(ui_DATA)

if HAVE_LIBCHAMPLAIN
empathy_handwritten_source += 				\
	empathy-map-view.c			\
	empathy-map-view.h

ui_DATA += 					\
	empathy-map-view.ui
else
EXTRA_DIST +=					\
	empathy-map-view.c			\
	empathy-map-view.h			\
	empathy-map-view.ui
endif

dist_man_MANS =			\
	empathy.1 \
	empathy-accounts.1


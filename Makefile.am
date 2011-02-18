MY_SUBDIRS = tools extensions po data telepathy-yell libempathy libempathy-gtk src help tests
NST_SUBDIRS = nautilus-sendto-plugin

DIST_SUBDIRS = $(MY_SUBDIRS) $(NST_SUBDIRS)

SUBDIRS = $(MY_SUBDIRS)

if HAVE_NST
SUBDIRS += $(NST_SUBDIRS)
endif

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS =		\
	--disable-scrollkeeper		\
	--disable-schemas-install

EXTRA_DIST = 			\
        CONTRIBUTORS \
        COPYING-DOCS \
        COPYING.LGPL \
        COPYING.SHARE-ALIKE \
        ChangeLog \
        ChangeLog.old \
        README \
        autogen.sh \
        $(NULL)

DISTCLEANFILES = 		\
        ChangeLog

# Workaround broken scrollkeeper that doesn't remove its files on
# uninstall.
distuninstallcheck_listfiles = find . -type f -print | grep -v '^\./var/scrollkeeper'

distclean-local:
	if test "x$(srcdir)" = "x."; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log EMPATHY_2_27_1.. --stat) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog


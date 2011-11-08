dnl If not 1, append datestamp to the version number
m4_define(empathy_released, 0)

m4_define([empathy_major_version], [3])
m4_define([empathy_minor_version], [3])
m4_define([empathy_micro_version], [1])
m4_define([empathy_nano_version], [0])

dnl Display the nano_version only if it's not '0'
m4_define([empathy_base_version],
          [empathy_major_version.empathy_minor_version.empathy_micro_version])
m4_define([empathy_full_version],
          [m4_if(empathy_nano_version, 0, [empathy_base_version],
              [empathy_base_version].[empathy_nano_version])])

m4_define(empathy_maybe_datestamp,
          m4_esyscmd([if test x]empathy_released[ != x1; then date +.%Y%m%d | tr -d '\n\r'; fi]))

m4_define(empathy_version, empathy_full_version[]empathy_maybe_datestamp)


AC_INIT([Empathy],
        [empathy_version],
        [http://bugzilla.gnome.org/browse.cgi?product=empathy],
        [empathy],
        [https://live.gnome.org/Empathy])

AC_PREREQ([2.64])
AC_COPYRIGHT([
  Copyright (C) 2003-2007 Imendio AB
  Copyright (C) 2007-2010 Collabora Ltd.
])

# Minimal version required

# Hardp deps
FOLKS_REQUIRED=0.6.2
GLIB_REQUIRED=2.30.0
GNUTLS_REQUIRED=2.8.5
GTK_REQUIRED=3.0.2
GSTREAMER_REQUIRED=0.10.32
KEYRING_REQUIRED=2.26.0
GCR_REQUIRED=2.91.4
LIBCANBERRA_GTK_REQUIRED=0.25
LIBNOTIFY_REQUIRED=0.7.0
TELEPATHY_FARSIGHT_REQUIRED=0.0.18
TELEPATHY_GLIB_REQUIRED=0.17.0
TELEPATHY_LOGGER=0.2.10
WEBKIT_REQUIRED=1.3.13
GOA_REQUIRED=3.3.0

# Optional deps
CLUTTER_REQUIRED=1.7.14
CLUTTER_GTK_REQUIRED=0.90.3
ENCHANT_REQUIRED=1.2.0
GEOCLUE_REQUIRED=0.11
ISO_CODES_REQUIRED=0.35
NAUTILUS_SENDTO_REQUIRED=2.90.0
NETWORK_MANAGER_REQUIRED=0.7.0

# Use --enable-maintainer-mode to disable deprecated symbols,
# disable single include and enable GSEAL. If this is not a released empathy,
# maintainer mode is forced
# FIXME: don't disable deprecated symbols until bgo #636654 has been fixed
#ifelse(empathy_released, 1, [], [enable_maintainer_mode="yes"])
#GNOME_MAINTAINER_MODE_DEFINES

# Disable GLib deprecation warnings for now; we don't want to bump the GLib dep
# and make things harder for contributiors without good reason. We'll re-enable
# those warnings later during the cycle (bgo #662662)
AC_DEFINE(GLIB_DISABLE_DEPRECATION_WARNINGS, 1, [Build with GLib deprecated])

# telepathy-yell
prev_top_build_prefix=$ac_top_build_prefix
prev_ac_configure_args=$ac_configure_args
ac_configure_args="$ac_configure_args --disable-shared-library"
AX_CONFIG_DIR([telepathy-yell])
ac_top_build_prefix=$prev_top_build_prefix
ac_configure_args=$prev_ac_configure_args
export PKG_CONFIG_PATH=$PKG_CONFIG_PATH:"$ac_top_build_prefix"telepathy-yell/telepathy-yell

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([configure.ac])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([1.11 no-define -Wno-portability tar-ustar no-dist-gzip dist-xz])

# Support silent build rules. Disable
# by either passing --disable-silent-rules to configure or passing V=1
# to make
AM_SILENT_RULES([yes])

# Check for programs
AC_PROG_CC
AC_HEADER_STDC
AM_PROG_MKDIR_P
AM_PATH_GLIB_2_0
AC_PATH_XTRA

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

GNOME_DOC_INIT([0.17.3])
AC_PATH_PROG(DBUS_BINDING_TOOL, dbus-binding-tool)
GLIB_GSETTINGS
GLIB_GENMARSHAL=`$PKG_CONFIG glib-2.0 --variable=glib_genmarshal`
AC_SUBST(GLIB_GENMARSHAL)

AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from libxslt) is required])
fi
AM_PATH_PYTHON([2.3])

EMPATHY_ARG_VALGRIND


# -----------------------------------------------------------
# Error flags
# -----------------------------------------------------------
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)

AC_ARG_ENABLE(Werror,
  AS_HELP_STRING([--disable-Werror],
                 [compile without -Werror (normally enabled in development builds)]),
    werror=$enableval, :)

AS_COMPILER_FLAG(-Wextra, wextra=yes, wextra=no)
AS_COMPILER_FLAG(-Wno-missing-field-initializers,
        wno_missing_field_initializers=yes,
        wno_missing_field_initializers=no)
AS_COMPILER_FLAG(-Wno-unused-parameter,
        wno_unused_parameter=yes,
        wno_unused_parameter=no)

ifelse(empathy_released, 1, [],
    [
        if test x$werror = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Werror"
        fi
        if test x$wextra = xyes -a \
            x$wno_missing_field_initializers = xyes -a \
            x$wno_unused_parameter = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
        fi
    ])

AS_COMPILER_FLAG(-Wdeclaration-after-statement, ERROR_CFLAGS="$ERROR_CFLAGS -Wdeclaration-after-statement")
AS_COMPILER_FLAG(-Wshadow, ERROR_CFLAGS="$ERROR_CFLAGS -Wshadow")
AS_COMPILER_FLAG(-Wmissing-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-prototypes")
AS_COMPILER_FLAG(-Wmissing-declarations, ERROR_CFLAGS="$ERROR_CFLAGS -Wmissing-declarations")
AS_COMPILER_FLAG(-Wstrict-prototypes, ERROR_CFLAGS="$ERROR_CFLAGS -Wstrict-prototypes")

AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(gprof,
            AS_HELP_STRING([--enable-gprof=@<:@no/yes@:>@],
            [build with support for gprof]),,
            [enable_gprof=no])
if test "x$enable_gprof" != "xno" ; then
    CFLAGS="$CFLAGS -pg"
    LDFLAGS="$LDFLAGS -pg"
fi

AC_SUBST(LDFLAGS)

# -----------------------------------------------------------
# Pkg-Config dependency checks
# -----------------------------------------------------------

AC_CHECK_LIBM
AC_SUBST(LIBM)

PKG_CHECK_MODULES(EMPATHY,
[
   dbus-glib-1
   folks >= $FOLKS_REQUIRED
   folks-telepathy >= $FOLKS_REQUIRED
   glib-2.0 >= $GLIB_REQUIRED
   gio-2.0 >= $GLIB_REQUIRED
   gio-unix-2.0 >= $GLIB_REQUIRED
   gnome-keyring-1 >= $KEYRING_REQUIRED
   gnutls >= $GNUTLS_REQUIRED
   gmodule-export-2.0
   gobject-2.0
   gsettings-desktop-schemas
   gstreamer-0.10 >= $GSTREAMER_REQUIRED
   gstreamer-interfaces-0.10
   libxml-2.0
   telepathy-glib >= $TELEPATHY_GLIB_REQUIRED
   telepathy-logger-0.2 >= $TELEPATHY_LOGGER
   x11
   gtk+-3.0 >= $GTK_REQUIRED
   libcanberra-gtk3 >= $LIBCANBERRA_GTK_REQUIRED
   libnotify >= $LIBNOTIFY_REQUIRED
   gcr-3 >= $GCR_REQUIRED
   libpulse
   libpulse-mainloop-glib
   webkitgtk-3.0 >= $WEBKIT_REQUIRED
   libsoup-2.4
])

PKG_CHECK_MODULES(YELL, [telepathy-yell])

PKG_CHECK_MODULES(EMPATHY_AV,
[
   farsight2-0.10
   telepathy-farsight >= $TELEPATHY_FARSIGHT_REQUIRED
])

AC_DEFINE(EMPATHY_GOA_PROVIDER, "org.gnome.OnlineAccounts",
   [Name of provider for accounts imported from GOA])

# -----------------------------------------------------------
# Call interface
# -----------------------------------------------------------
AC_ARG_ENABLE(call,
            AS_HELP_STRING([--enable-call=@<:@no/yes/auto@:>@],
            [build with Call interface support]),,
            [enable_call=auto])
if test "x$enable_call" != "xno" ; then
   PKG_CHECK_MODULES(EMPATHY_CALL,
   [
      farsight2-0.10
      telepathy-farstream
      clutter-1.0 >= $CLUTTER_REQUIRED
      clutter-gtk-1.0
      clutter-gst-1.0
   ], have_call="yes", have_call="no" )

   if test "x$have_call" = "xyes"; then
      AC_DEFINE(HAVE_CALL, 1, [Define if you have Call channel support])
   fi
else
  have_call=no
fi

if test "x$enable_call" = "xyes" -a "x$have_call" != "xyes"; then
   AC_MSG_ERROR([Could not find Call handler dependencies:

$EMPATHY_CALL_PKG_ERRORS])
fi
AM_CONDITIONAL(HAVE_CALL, test "x$have_call" = "xyes")


# -----------------------------------------------------------
# Call support in tp-logger
# -----------------------------------------------------------
AC_ARG_ENABLE(call-logs,
            AS_HELP_STRING([--enable-call-logs=@<:@no/yes/auto@:>@],
            [build with call logs support]),,
            [enable_call_logs=auto])
if test "x$enable_call_logs" != "xno" ; then
   SAVE_CFLAGS=$CFLAGS
   SAVE_CPPFLAGS=$CPPFLAGS
   SAVE_LDFLAGS=$LDFLAGS
   CFLAGS="$CFLAGS $EMPATHY_CFLAGS"
   CPPFLAGS="$CPPFLAGS $EMPATHY_CFLAGS"
   LDFLAGS="$LDFLAGS $EMPATHY_LDFLAGS"

   AC_CHECK_HEADER(telepathy-logger/call-event.h,
      have_call_logs="yes", have_call_logs="no")

   CFLAGS=$SAVE_CFLAGS
   CPPFLAGS=$SAVE_CPPFLAGS
   LDFLAGS=$SAVE_LDFLAGS

   if test "x$have_call_logs" = "xyes"; then
      AC_DEFINE(HAVE_CALL_LOGS, 1, [Define if you have call log support])
   fi
else
  have_call_logs=no
fi

if test "x$enable_call_logs" = "xyes" -a "x$have_call_logs" != "xyes"; then
   AC_MSG_ERROR([Call logs support requested but telepathy-logger wasn't
                 built with --enable-call])
fi

# -----------------------------------------------------------
# evolution-data-server (about-me)
# -----------------------------------------------------------
AC_ARG_WITH(eds,
            AS_HELP_STRING([--with-eds],
                           [build with evolution-data-server support]),,
            [with_eds=yes])
if test x"$with_eds" = x"yes" ; then
   PKG_CHECK_MODULES(EDS, [libebook-1.2])
   AC_DEFINE(HAVE_EDS, 1, [Define if we have libebook])
fi

# -----------------------------------------------------------
# Enable debug
# -----------------------------------------------------------

AC_ARG_ENABLE(debug,
  AS_HELP_STRING([--disable-debug],
                 [compile without debug code]),
    enable_debug=$enableval, enable_debug=yes )

if test x$enable_debug = xyes; then
  AC_DEFINE(ENABLE_DEBUG, [], [Enable debug code])
fi

# -----------------------------------------------------------
# Language Support
# -----------------------------------------------------------
IT_PROG_INTLTOOL([0.40.0])

AM_GNU_GETTEXT_VERSION([0.17])
AM_GNU_GETTEXT([external])

GETTEXT_PACKAGE=empathy
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE",[Gettext package name])

# -----------------------------------------------------------
# Connectivity integration
# -----------------------------------------------------------
AC_ARG_WITH(connectivity,
              AS_HELP_STRING([--with-connectivity=@<:@nm/connman/auto/no@:>@],
                             [build with connectivity support]), ,
                             with_connectivity=auto)

if test "x$with_connectivity" = "xno"; then
   have_nm=no
   have_connman=no

elif test "x$with_connectivity" = "xconnman"; then
   have_nm=no

   PKG_CHECK_MODULES(CONNMAN,
   [
      dbus-glib-1
   ], have_connman="yes", have_connman="no")

   if test "x$have_connman" = "xyes"; then
      AC_DEFINE(HAVE_CONNMAN, 1, [Define if you have connman dependencies])
   fi

else
   have_connman=no

   PKG_CHECK_MODULES(NETWORK_MANAGER,
   [
      libnm-glib >= $NETWORK_MANAGER_REQUIRED
   ], have_nm="yes", have_nm="no")

   if test "x$have_nm" = "xyes"; then
      AC_DEFINE(HAVE_NM, 1, [Define if you have libnm-glib])
   fi
fi

if test "x$with_connectivity" = "xconnman" -a "x$have_connman" != "xyes"; then
   AC_MSG_ERROR([Couldn't find connman dependencies:

$CONNMAN_PKG_ERRORS])
fi

if test "x$with_connectivity" = "xnm" -a "x$have_nm" != "xyes"; then
   AC_MSG_ERROR([Couldn't find Network Manager dependencies:

$NETWORK_MANAGER_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_NM, test "x$have_nm" = "xyes")
AM_CONDITIONAL(HAVE_CONNMAN, test "x$have_connman" = "xyes")

# -----------------------------------------------------------
# gudev
# -----------------------------------------------------------
AC_ARG_ENABLE(gudev,
              AS_HELP_STRING([--enable-gudev=@<:@no/yes/auto@:>@],
                             [build with gudev support]), ,
                             enable_gudev=auto)

if test "x$enable_gudev" != "xno"; then

   PKG_CHECK_MODULES(UDEV, [gudev-1.0],
        have_gudev="yes", have_gudev="no")

   if test "x$have_gudev" = "xyes"; then
      AC_DEFINE(HAVE_UDEV, 1, [Define if you have gudev])
   fi
else
   have_gudev=no
fi

if test "x$enable_gudev" = "xyes" -a "x$have_gudev" != "xyes"; then
   AC_MSG_ERROR([Could not find gudev dependencies:

$UDEV_PKG_ERRORS])
fi
AM_CONDITIONAL(HAVE_UDEV, test "x$have_gudev" = "xyes")

# -----------------------------------------------------------
# spellchecking checks: enchant and iso-codes
# -----------------------------------------------------------
AC_ARG_ENABLE(spell,
              AS_HELP_STRING([--enable-spell=@<:@no/yes/auto@:>@],
                             [Enable spell checking]), ,
                             enable_spell=auto)

if test "x$enable_spell" != "xno"; then
   PKG_CHECK_MODULES(ENCHANT,
   [
      enchant >= $ENCHANT_REQUIRED,
      iso-codes >= $ISO_CODES_REQUIRED
   ], have_enchant="yes", have_enchant="no")

   if test "x$have_enchant" = "xyes"; then
      AC_MSG_CHECKING([whether iso-codes has iso-639 and iso-3166 domains])
      if $PKG_CONFIG --variable=domains iso-codes | grep 639 > /dev/null && \
         $PKG_CONFIG --variable=domains iso-codes | grep 3166 > /dev/null ; then
            AC_DEFINE_UNQUOTED(ISO_CODES_PREFIX, ["`$PKG_CONFIG --variable=prefix iso-codes`"], [ISO codes prefix])
            AC_DEFINE(HAVE_ENCHANT, [], [Define if you have libenchant])
            result=yes
      else
            result=no
            have_enchant="no"
      fi
      AC_MSG_RESULT([$result])
   fi
else
   have_enchant=no
fi

if test "x$enable_spell" = "xyes" -a "x$have_enchant" != "xyes"; then
   AC_MSG_ERROR([Could not find spell-checking dependencies:

$ENCHANT_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_ENCHANT, test "x$have_enchant" = "xyes")

# -----------------------------------------------------------
# Map view checks: libchamplain
# -----------------------------------------------------------
AC_ARG_ENABLE(map,
              AS_HELP_STRING([--enable-map=@<:@no/yes/auto@:>@],
                             [Enable map view]), ,
                             enable_map=auto)

if test "x$enable_map" != "xno"; then

   PKG_CHECK_MODULES(LIBCHAMPLAIN,
    [
       clutter-gtk-1.0
       champlain-gtk-0.12
       champlain-0.12
    ], have_libchamplain="yes", have_libchamplain="no")

   if test "x$have_libchamplain" = "xyes"; then
      AC_DEFINE(HAVE_LIBCHAMPLAIN, 1, [Define if you have libchamplain])
   fi
else
   have_libchamplain=no
fi

if test "x$enable_map" = "xyes" -a "x$have_libchamplain" != "xyes"; then
   AC_MSG_ERROR([Could not find map view dependencies:

$LIBCHAMPLAIN_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_LIBCHAMPLAIN, test "x$have_libchamplain" = "xyes")

# -----------------------------------------------------------
# location checks: geoclue
# -----------------------------------------------------------
AC_ARG_ENABLE(location,
              AS_HELP_STRING([--enable-location=@<:@no/yes/auto@:>@],
                             [Enable location awareness]), ,
                             enable_location=auto)

if test "x$enable_location" != "xno"; then
   PKG_CHECK_MODULES(GEOCLUE,
   [
      geoclue >= $GEOCLUE_REQUIRED
   ], have_geoclue="yes", have_geoclue="no")

   if test "x$have_geoclue" = "xyes"; then
      AC_DEFINE(HAVE_GEOCLUE, 1, [Define if you have geoclue])
   fi
else
   have_geoclue="no"
fi

if test "x$enable_location" = "xyes" -a "x$have_geoclue" != "xyes"; then
   AC_MSG_ERROR([Could not find location dependencies:

$GEOCLUE_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_GEOCLUE, test "x$have_geoclue" = "xyes")
AC_SUBST(GEOCLUE_CFLAGS)
AC_SUBST(GEOCLUE_LIBS)

# -----------------------------------------------------------
# location checks: geocode-glib
# -----------------------------------------------------------
AC_ARG_ENABLE(geocode,
              AS_HELP_STRING([--enable-geocode=@<:@no/yes/auto@:>@],
                             [Enable geocode support]), ,
                             enable_geocode=auto)

if test "x$enable_geocode" != "xno"; then
   PKG_CHECK_MODULES(GEOCODE,
   [
      geocode-glib
   ], have_geocode="yes", have_geocode="no")

   if test "x$have_geocode" = "xyes"; then
      AC_DEFINE(HAVE_GEOCODE, 1, [Define if you have geocode])
   fi
else
   have_geocode="no"
fi

if test "x$enable_geocode" = "xyes" -a "x$have_geocode" != "xyes"; then
   AC_MSG_ERROR([Could not find geocode dependencies:

$GEOCODE_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_GEOCODE, test "x$have_geocode" = "xyes")
AC_SUBST(GEOCODE_CFLAGS)
AC_SUBST(GEOCODE_LIBS)

# -----------------------------------------------------------
# meego widgets support
# -----------------------------------------------------------
AC_ARG_ENABLE(meego,
              AS_HELP_STRING([--enable-meego=@<:@no/yes@:>@],
                             [Enable meego widgets]), ,
                             enable_meego=no)

if test "x$enable_meego" != "xno"; then
    PKG_CHECK_MODULES(MEEGO,
    [
       mx-gtk-1.0
       gio-unix-2.0
    ], have_meego="yes", have_meego="no")

    if test "x$have_meego" = "xyes"; then
       AC_DEFINE(HAVE_MEEGO, 1, [Define if you have meego])
    fi
else
   have_meego="no"
fi

if test "x$enable_meego" = "xyes" -a "x$have_meego" != "xyes"; then
   AC_MSG_ERROR([Could not find meego dependencies:

$MEEGO_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_MEEGO, test "x$have_meego" = "xyes")
AC_SUBST(MEEGO_CFLAGS)
AC_SUBST(MEEGO_LIBS)

# -----------------------------------------------------------
# nautilus-sendto
# -----------------------------------------------------------
AC_ARG_ENABLE(nautilus-sendto,
              AS_HELP_STRING([--enable-nautilus-sendto=@<:@no/yes/auto@:>@],
                             [build nautilus-sendto plugin]), ,
                             enable_nautilus_sendto=auto)

if test "x$enable_nautilus_sendto" != "xno"; then
   PKG_CHECK_MODULES(NST,
    [
       nautilus-sendto >= $NAUTILUS_SENDTO_REQUIRED
    ], have_nst="yes", have_nst="no")
else
   have_nst=no
fi

if test "x$enable_nautilus_sendto" = "xyes" -a "x$have_nst" != "xyes"; then
   AC_MSG_ERROR([Could not find nautilus-sendto dependencies:

$NST_PKG_ERRORS])
fi

AM_CONDITIONAL(HAVE_NST, test "x$have_nst" = "xyes")

# -----------------------------------------------------------
# goa-mc-plugin
# -----------------------------------------------------------
AC_ARG_ENABLE(goa,
              AS_HELP_STRING([--enable-goa=@<:@no/yes/auto@:>@],
                             [build GOA MC plugin]), ,
                             enable_goa=auto)

if test "x$enable_goa" != "xno"; then
   PKG_CHECK_MODULES(GOA,
    [
       mission-control-plugins
       goa-1.0 >= $GOA_REQUIRED
    ], have_goa="yes", have_goa="no")

   AC_MSG_CHECKING([Mission Control plugins dir])
   MISSION_CONTROL_PLUGINS_DIR=`pkg-config --variable=plugindir mission-control-plugins`

   AC_MSG_RESULT([$MISSION_CONTROL_PLUGINS_DIR])
   AC_SUBST(MISSION_CONTROL_PLUGINS_DIR)
else
   have_goa=no
fi

if test "x$enable_goa" = "xyes" -a "x$have_goa" != "xyes"; then
   AC_MSG_ERROR([Could not find GOA dependencies.])
fi

if test "x$have_goa" = "xyes"; then
   AC_DEFINE(HAVE_GOA, 1, [Define to 1 to enable GNOME Online Accounts])
fi

AM_CONDITIONAL(HAVE_GOA, test "x$have_goa" = "xyes")

# Optional dependency for avatar selection
AC_ARG_WITH([cheese],
            AS_HELP_STRING([--with-cheese],
                           [enable cheese webcam support]),,
            with_cheese=auto)

if test x"$with_cheese" != x"no" ; then
   PKG_CHECK_MODULES(CHEESE, gstreamer-0.10 cheese-gtk >= 2.91.91.1, [have_cheese=yes], [have_cheese=no])
   if test x${have_cheese} = xyes; then
     AC_DEFINE(HAVE_CHEESE, 1, [Define to 1 to enable cheese webcam support])
   fi
   if test x${with_cheese} = xyes && test x${have_cheese} = xno; then
     AC_MSG_ERROR([Cheese configured but not found:

$CHEESE_PKG_ERRORS])
   fi
else
   have_cheese=no
fi
AM_CONDITIONAL(BUILD_CHEESE, test x${have_cheese} = xyes)


# -----------------------------------------------------------
# Coding style checks
# -----------------------------------------------------------
AC_ARG_ENABLE(coding-style-checks,
  AS_HELP_STRING([--disable-coding-style-checks],
                 [do not check coding style using grep]),
    [ENABLE_CODING_STYLE_CHECKS=$enableval],
    [ENABLE_CODING_STYLE_CHECKS=yes])

AC_SUBST([ENABLE_CODING_STYLE_CHECKS])
# -----------------------------------------------------------

AC_CONFIG_FILES([
   Makefile
   data/Makefile
   data/empathy.desktop.in
   data/empathy-accounts.desktop.in
   data/icons/Makefile
   extensions/Makefile
   po/Makefile.in
   libempathy/Makefile
   libempathy-gtk/Makefile
   src/Makefile
   nautilus-sendto-plugin/Makefile
   goa-mc-plugin/Makefile
   help/Makefile
   tests/Makefile
   tests/interactive/Makefile
   tests/xml/Makefile
   tools/Makefile
])
AC_OUTPUT

echo "
Configure summary:

	Compiler....................:  ${CC}
	Compiler Flags..............:  ${CFLAGS} ${ERROR_CFLAGS}
	Linker Flags................:  ${LDFLAGS}
	Prefix......................:  ${prefix}
	Coding style checks.........:  ${ENABLE_CODING_STYLE_CHECKS}

    Features:
	Spell checking (enchant)....:  ${have_enchant}
	Display maps (libchamplain).:  ${have_libchamplain}
	Location awareness (Geoclue):  ${have_geoclue}
	Geocode support (Geocode)...:  ${have_geocode}
	Meego widgets...............:  ${have_meego}
	Cheese webcam support ......:  ${have_cheese}
	Camera monitoring...........:  ${have_gudev}

    Connectivity:
	NetworkManager integration..:  ${have_nm}
	ConnMan integration.........:  ${have_connman}

    Extras:
	Nautilus-sendto plugin......:  ${have_nst}
	GOA MC plugin...............:  ${have_goa}
	Salut E-D-S support.........:  ${with_eds}
	Exp. Call channel handler...:  ${have_call}
	Exp. Call log support.......:  ${have_call_logs}
"

#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="Empathy"
REQUIRED_AUTOMAKE_VERSION=1.9

(test -f $srcdir/configure.ac) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level gnome directory"
    exit 1
}

which gnome-autogen.sh || {
    echo "You need to install gnome-common from the GNOME GIT"
    exit 1
}

# Fetch submodules if needed
if test ! -f telepathy-yell/autogen.sh;
then
  echo "+ Setting up submodules"
  git submodule init
fi
git submodule update

# launch tp-yell's autogen.sh
cd telepathy-yell
sh autogen.sh --no-configure
cd ..

USE_GNOME2_MACROS=1 USE_COMMON_DOC_BUILD=yes . gnome-autogen.sh

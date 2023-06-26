#!/bin/sh
# Run this to generate all the initial makefiles, etc.

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

PKG_NAME="cafe-media"
REQUIRED_AUTOMAKE_VERSION=1.9
REQUIRED_INTLTOOL_VERSION=0.35
USE_COMMON_DOC_BUILD=yes

(test -f $srcdir/configure.ac \
  && test -d $srcdir/cafe-volume-control) || {
    echo -n "**Error**: Directory "\`$srcdir\'" does not look like the"
    echo " top-level $PKG_NAME directory"
    exit 1
}

which cafe-autogen || {
    echo "You need to install cafe-common 1.2.1 or higher"
    exit 1
}

. cafe-autogen "$@"

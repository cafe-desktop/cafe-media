NULL =

SUBDIRS = po data man cafe-volume-control

DISTCHECK_CONFIGURE_FLAGS = \
	--enable-compile-warnings=no \
	CFLAGS='-Wno-deprecated-declarations'

DIST_SUBDIRS =					\
	data					\
	po 					\
	man					\
	cafe-volume-control			\
	$(NULL)

EXTRA_DIST =					\
	.version				\
	autogen.sh				\
	configure				\
	ChangeLog				\
	COPYING					\
	COPYING-DOCS				\
	build-aux/git-version-gen 		\
	its					\
	$(NULL)

DISTCLEANFILES =				\
	cafe-volume-control/org.cafe.applets.GvcApplet.cafe-panel-applet \
	cafe-volume-control/org.cafe.panel.applet.GvcAppletFactory.service \
	cafe-volume-control/org.cafe.applets.GvcApplet.cafe-panel-applet.in \
	$(NULL)

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# see build-aux/git-version-get for details
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

# Build ChangeLog from GIT  history
ChangeLog:
	$(AM_V_GEN) if test -d $(top_srcdir)/.git; then \
		GIT_DIR="$(top_srcdir)/.git" git log --stat > $@; \
	fi

dist: ChangeLog

.PHONY: ChangeLog

-include $(top_srcdir)/git.mk

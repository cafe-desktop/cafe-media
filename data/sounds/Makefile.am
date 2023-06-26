NULL =

sounddir = $(datadir)/sounds/cafe/default/alerts

sound_DATA =			\
	bark.ogg		\
	drip.ogg		\
	glass.ogg		\
	sonar.ogg		\
	$(NULL)

metadata_in_files = cafe-sounds-default.xml.in
metadatadir = $(pkgdatadir)/sounds
metadata_DATA = $(metadata_in_files:.xml.in=.xml)
$(metadata_DATA): $(metadata_in_files)
	$(AM_V_GEN) GETTEXTDATADIR=$(top_srcdir) $(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@

noinst_DATA = cafe-sounds-default.xml.in
CLEANFILES = cafe-sounds-default.xml cafe-sounds-default.xml.in

EXTRA_DIST = $(sound_DATA) cafe-sounds-default.xml.in.in

cafe-sounds-default.xml.in: cafe-sounds-default.xml.in.in Makefile
	$(AM_V_GEN)sed -e 's^\@datadir\@^$(datadir)^g' < $(srcdir)/cafe-sounds-default.xml.in.in > cafe-sounds-default.xml.in.tmp \
	&& mv cafe-sounds-default.xml.in.tmp cafe-sounds-default.xml.in

MAINTAINERCLEANFILES = \
	Makefile.in

-include $(top_srcdir)/git.mk

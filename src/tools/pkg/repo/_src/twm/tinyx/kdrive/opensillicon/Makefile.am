AM_CPPFLAGS = \
	@KDRIVE_INCS@ \
	@KDRIVE_CFLAGS@ \
	-I$(abs_top_srcdir)/../../include

noinst_LIBRARIES = libopensillicon.a
bin_PROGRAMS = X

libopensillicon_a_SOURCES = opensillicon.c opensillicon.h
X_SOURCES = opensilliconinit.c
X_LDADD = @OPEN_SILLICON_CRT0@ libopensillicon.a \
	@KDRIVE_LIBS@ @XSERVER_LIBS@
X_LDFLAGS = @OPEN_SILLICON_LDFLAGS@
X_DEPENDENCIES = @OPEN_SILLICON_CRT0@ libopensillicon.a @KDRIVE_LIBS@ \
	$(filter %.a,@OPEN_SILLICON_LIBS@)

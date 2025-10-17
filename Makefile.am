bin_PROGRAMS = srt2dvbmux

srt2dvbmux_SOURCES = \
    src/main.c \
    src/srt_parser.c \
    src/render_pango.c \
    src/dvb_sub.c \
    src/qc.c \
    src/bench.c \
    src/debug_png.c \
    src/render_ass.c

srt2dvbmux_CFLAGS = $(DEPS_CFLAGS)
srt2dvbmux_LDADD  = $(DEPS_LIBS)

distclean-local:
	rm -rf autom4te.cache
	rm -f aclocal.m4 configure config.log config.status
	rm -f Makefile.in
	rm -f compile config.guess config.sub depcomp install-sh ltmain.sh missing
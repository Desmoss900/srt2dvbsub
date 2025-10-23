bin_PROGRAMS = srt2dvbsub dvdbr2dvbsub

srt2dvbsub_SOURCES = \
    src/srt2dvbsub.c \
    src/srt_parser.c \
    src/render_pango.c \
    src/render_pool.c \
    src/runtime_opts.c \
    src/cpu_count.c \
    src/dvb_sub.c \
    src/qc.c \
    src/bench.c \
    src/debug_png.c \
    src/muxsub.c

# Conditionally include libass-based renderer when configured with --enable-ass
if USE_LIBASS
srt2dvbsub_SOURCES += src/render_ass.c
else
# Provide a tiny stub implementation so code links when libass is not available
srt2dvbsub_SOURCES += src/render_ass_stub.c
endif

srt2dvbsub_CFLAGS = $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(LIBASS_CFLAGS)
srt2dvbsub_LDADD  = $(DEPS_LIBS) $(FFMPEG_LIBS) $(LIBASS_LIBS) -lfontconfig -lm

dvdbr2dvbsub_SOURCES = \
    src/dvdbr2dvbsub.c \
    src/runtime_opts.c \
    src/cpu_count.c \
    src/dvb_sub.c \
    src/qc.c \
    src/bench.c \
    src/debug_png.c \
    src/muxsub.c

dvdbr2dvbsub_CFLAGS = $(DEPS_CFLAGS) $(FFMPEG_CFLAGS)
dvdbr2dvbsub_LDADD  = $(DEPS_LIBS) $(FFMPEG_LIBS) -lm

distclean-local:
	rm -rf autom4te.cache
	rm -f aclocal.m4 configure config.log config.status
	rm -f Makefile.in
	rm -f compile config.guess config.sub depcomp install-sh ltmain.sh missing

# (Out-of-tree build creates build/src at configure time; no helper targets needed.)

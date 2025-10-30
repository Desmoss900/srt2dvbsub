THREAD_SAFE_MUX_CPPFLAGS = @THREAD_SAFE_MUX_CPPFLAGS@

bin_PROGRAMS = srt2dvbsub dvdbr2dvbsub

srt2dvbsub_SOURCES = \
    src/srt2dvbsub.c \
    src/srt_parser.c \
    src/render_pango.c \
    src/render_pool.c \
    src/runtime_opts.c \
    src/cpu_count.c \
    src/dvb_sub.c \
    src/fontlist.c \
    src/dvb_lang.c \
    src/qc.c \
    src/bench.c \
    src/debug_png.c \
    src/muxsub.c \
    src/mux_write.c \
    src/alloc_utils.c \
    src/pool_alloc.c \
    src/utils.c

# Conditionally include libass-based renderer when configured with --enable-ass
if USE_LIBASS
srt2dvbsub_SOURCES += src/render_ass.c
else
# Provide a tiny stub implementation so code links when libass is not available
srt2dvbsub_SOURCES += src/render_ass_stub.c
endif

srt2dvbsub_CFLAGS = $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(LIBASS_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)
srt2dvbsub_LDADD  = $(DEPS_LIBS) $(FFMPEG_LIBS) $(LIBASS_LIBS) -lfontconfig -lm

dvdbr2dvbsub_SOURCES = \
    src/dvdbr2dvbsub.c \
    src/runtime_opts.c \
    src/cpu_count.c \
    src/dvb_sub.c \
    src/fontlist.c \
    src/dvb_lang.c \
    src/qc.c \
    src/bench.c \
    src/debug_png.c \
    src/muxsub.c \
    src/mux_write.c \
    src/alloc_utils.c \
    src/pool_alloc.c \
    src/utils.c

dvdbr2dvbsub_CFLAGS = $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)
dvdbr2dvbsub_LDADD  = $(DEPS_LIBS) $(FFMPEG_LIBS) -lm

# Convenience build modes: debug (with symbols, no -Werror) and release
# (optimized and stripped). These are helper targets that invoke a
# recursive make with per-program CFLAGS overridden. Usage:
#   make debug   # builds with -g -O0 and keeps symbols
#   make release # builds optimized binaries and strips them

DEBUG_CFLAGS = -g -O0 -DDEBUG -Wall -Werror -Wextra
RELEASE_CFLAGS = -O3 -DRELEASE -Wall -Wextra

# Ensure there is a fallback strip command if Automake/config didn't set one
STRIP ?= strip

.PHONY: debug release

debug:
	@echo "Building debug binaries..."
	$(MAKE) clean
	$(MAKE) srt2dvbsub dvdbr2dvbsub \
		srt2dvbsub_CFLAGS="$(DEBUG_CFLAGS) $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(LIBASS_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)" \
		dvdbr2dvbsub_CFLAGS="$(DEBUG_CFLAGS) $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)"

release:
	@echo "Building release binaries (optimized + stripped)..."
#	$(MAKE) clean
	$(MAKE) srt2dvbsub dvdbr2dvbsub \
		srt2dvbsub_CFLAGS="$(RELEASE_CFLAGS) $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(LIBASS_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)" \
		dvdbr2dvbsub_CFLAGS="$(RELEASE_CFLAGS) $(DEPS_CFLAGS) $(FFMPEG_CFLAGS) $(THREAD_SAFE_MUX_CPPFLAGS)"
	@echo "Stripping binaries in build directory..."
	@$(SHELL) -c '\
for prog in srt2dvbsub dvdbr2dvbsub; do \
    found=`find . -type f -name "$$prog" -perm -111 -print -quit 2>/dev/null`; \
    if [ -n "$$found" ]; then \
        echo "Stripping $$found"; \
        if command -v strip >/dev/null 2>&1; then \
            strip "$$found" || true; \
        else \
            echo "warning: strip not found, skipping strip for $$found"; \
        fi; \
    else \
        echo "warning: $$prog not found in build directory, skipping strip"; \
    fi; \
done'

# Make the default 'make' build the release artifacts.
all-local: release

distclean-local:
	rm -rf autom4te.cache
	rm -f aclocal.m4 configure config.log config.status
	rm -f Makefile.in
	rm -f compile config.guess config.sub depcomp install-sh ltmain.sh missing

# (Out-of-tree build creates build/src at configure time; no helper targets needed.)

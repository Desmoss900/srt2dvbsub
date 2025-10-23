AC_INIT([srt2dvbsub], [1.0], [bugs@example.com])

# Default to disabling Automake dependency-tracking unless explicitly enabled.
# This makes the common developer/workflow faster (no per-file dep-tracking)
# and avoids the need to pass --disable-dependency-tracking at configure time.
# The variable name matches Automake's --enable/--disable option and will be
# honored by AM_INIT_AUTOMAKE when it processes options.
if test "x$enable_dependency_tracking" = "x" ; then
  enable_dependency_tracking=no
fi

AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_PROG_CC

# Ensure pkg-config is available
PKG_PROG_PKG_CONFIG

AS_IF([test "$srcdir" = "$builddir"],
  [AC_MSG_ERROR([In-tree builds are not supported. Please run configure from a separate build directory: mkdir build && cd build && ../configure])])

# Use pkg-config for deps
PKG_CHECK_MODULES([DEPS],
  [pangocairo pango cairo fontconfig],
  [],
  [AC_MSG_ERROR([Required libraries not found. Install pangocairo (pango+cairo) and fontconfig.])]
)

# Optional libass support selectable at configure time
AC_ARG_ENABLE([ass],
  AS_HELP_STRING([--enable-ass], [Enable libass subtitle rendering (requires libass)]),
  [enable_ass=$enableval],
  [enable_ass=no])

if test "x$enable_ass" = "xyes" ; then
  PKG_CHECK_MODULES([LIBASS], [libass], [], [AC_MSG_ERROR([libass requested via --enable-ass but not found])])
  AC_DEFINE([HAVE_LIBASS], [1], [Define if libass is available])
else
  # Ensure variables exist even when libass not requested/available
  LIBASS_CFLAGS=""
  LIBASS_LIBS=""
fi
AC_SUBST([LIBASS_CFLAGS])
AC_SUBST([LIBASS_LIBS])
AM_CONDITIONAL([USE_LIBASS], test "x$enable_ass" = "xyes" && test "x$LIBASS_CFLAGS" != "x")

# Also check FFmpeg libs explicitly (development headers)
PKG_CHECK_MODULES([FFMPEG],
  [libavformat >= 58.0.0
   libavcodec  >= 58.0.0
   libavutil   >= 56.0.0
   libswscale  >= 5.0.0],
  [],
  [AC_MSG_ERROR([Missing FFmpeg development libraries (libavformat, libavcodec, libavutil, libswscale)])])
AC_DEFINE([HAVE_FONTCONFIG], [1], [Define if fontconfig is available])

# Generate output files
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

# Ensure the build 'src' directory exists for out-of-tree builds. This avoids
# relying on a Makefile recipe (which can be fragile if tabs are altered) and
# creates the directory at configure time where it's needed for subdir-objects.
# Configure runs from the build directory, so create './src' here.
mkdir -p src
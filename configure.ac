AC_INIT([srt2dvbsub], [0.0.1], [bugs@chili-iptv.de])

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

# Platform-specific compiler flags
case "$host_os" in
  darwin*)
    # macOS: suppress clang warnings about ATOMIC_VAR_INIT deprecation and VLA folding
    CFLAGS="${CFLAGS} -Wno-deprecated-pragma -Wno-gnu-folding-constant"
    ;;
  *)
    # Other platforms: add general warning suppressions if needed
    ;;
esac

# Ensure pkg-config is available
PKG_PROG_PKG_CONFIG

AS_IF([test "$srcdir" = "$builddir"],
  [AC_MSG_ERROR([In-tree builds are not supported. Please run configure from a separate build directory: mkdir build && cd build && ../configure])])

# Also check FFmpeg libs explicitly (development headers)
PKG_CHECK_MODULES([FFMPEG],
  [libavformat >= 58.0.0
   libavcodec  >= 58.0.0
   libavutil   >= 56.0.0
   libswscale  >= 5.0.0],
  [],
  [AC_MSG_ERROR([Missing FFmpeg development libraries (libavformat, libavcodec, libavutil, libswscale)])])

# Use pkg-config for deps. Check each module individually so we can
# report the exact installed version for diagnostics and build-info.
# To inspect versions locally you can run:
#   pkg-config --modversion pangocairo pango cairo fontconfig
# or each individually, e.g. `pkg-config --modversion pangocairo`.

AC_MSG_CHECKING([for pangocairo (pkg-config >= 1.52.1)])
if pkg-config --exists "pangocairo >= 1.52.1"; then
  PANGOCAIRO_VERSION=`pkg-config --modversion pangocairo 2>/dev/null || echo unknown`
  PANGOCAIRO_CFLAGS="`pkg-config --cflags pangocairo 2>/dev/null || echo`"
  PANGOCAIRO_LIBS="`pkg-config --libs pangocairo 2>/dev/null || echo`"
  AC_MSG_RESULT([yes ($PANGOCAIRO_VERSION)])
else
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Required library 'pangocairo' >= 1.52.1 not found. Install/upgrade pangocairo (pango+cairo).])
fi
AC_SUBST([PANGOCAIRO_VERSION])
AC_SUBST([PANGOCAIRO_CFLAGS])
AC_SUBST([PANGOCAIRO_LIBS])

AC_MSG_CHECKING([for pango (pkg-config >= 1.52.1)])
if pkg-config --exists "pango >= 1.52.1"; then
  PANGO_VERSION=`pkg-config --modversion pango 2>/dev/null || echo unknown`
  PANGO_CFLAGS="`pkg-config --cflags pango 2>/dev/null || echo`"
  PANGO_LIBS="`pkg-config --libs pango 2>/dev/null || echo`"
  AC_MSG_RESULT([yes ($PANGO_VERSION)])
else
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Required library 'pango' >= 1.52.1 not found. Install/upgrade pango development files.])
fi
AC_SUBST([PANGO_VERSION])
AC_SUBST([PANGO_CFLAGS])
AC_SUBST([PANGO_LIBS])

AC_MSG_CHECKING([for cairo (pkg-config >= 1.18.0)])
if pkg-config --exists "cairo >= 1.18.0"; then
  CAIRO_VERSION=`pkg-config --modversion cairo 2>/dev/null || echo unknown`
  CAIRO_CFLAGS="`pkg-config --cflags cairo 2>/dev/null || echo`"
  CAIRO_LIBS="`pkg-config --libs cairo 2>/dev/null || echo`"
  AC_MSG_RESULT([yes ($CAIRO_VERSION)])
else
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Required library 'cairo' >= 1.18.0 not found. Install/upgrade cairo development files.])
fi
AC_SUBST([CAIRO_VERSION])
AC_SUBST([CAIRO_CFLAGS])
AC_SUBST([CAIRO_LIBS])

AC_MSG_CHECKING([for fontconfig (pkg-config >= 2.15.0)])
if pkg-config --exists "fontconfig >= 2.15.0"; then
  FONTCONFIG_VERSION=`pkg-config --modversion fontconfig 2>/dev/null || echo unknown`
  FONTCONFIG_CFLAGS="`pkg-config --cflags fontconfig 2>/dev/null || echo`"
  FONTCONFIG_LIBS="`pkg-config --libs fontconfig 2>/dev/null || echo`"
  AC_MSG_RESULT([yes ($FONTCONFIG_VERSION)])
  AC_DEFINE([HAVE_FONTCONFIG], [1], [Define if fontconfig is available])
else
  AC_MSG_RESULT([no])
  AC_MSG_ERROR([Required library 'fontconfig' >= 2.15.0 not found. Install/upgrade fontconfig development files.])
fi
AC_SUBST([FONTCONFIG_VERSION])
AC_SUBST([FONTCONFIG_CFLAGS])
AC_SUBST([FONTCONFIG_LIBS])

# Aggregate discovered CFLAGS/LIBS into the legacy DEPS_* variables used by
# the Makefile. This keeps the Makefile.am unchanged while making the
# individual module flags available for more granular use if needed.
DEPS_CFLAGS="${PANGOCAIRO_CFLAGS} ${PANGO_CFLAGS} ${CAIRO_CFLAGS} ${FONTCONFIG_CFLAGS}"
DEPS_LIBS="${PANGOCAIRO_LIBS} ${PANGO_LIBS} ${CAIRO_LIBS} ${FONTCONFIG_LIBS}"
AC_SUBST([DEPS_CFLAGS])
AC_SUBST([DEPS_LIBS])

# Optional libass support selectable at configure time
AC_ARG_ENABLE([ass],
  AS_HELP_STRING([--enable-ass], [Enable libass subtitle rendering (requires libass)]),
  [enable_ass=$enableval],
  [enable_ass=no])

if test "x$enable_ass" = "xyes" ; then
  PKG_CHECK_MODULES([LIBASS], [libass >= 0.17.1], [], [AC_MSG_ERROR([libass requested via --enable-ass but not found or too old (need >= 0.17.1)])])
  AC_DEFINE([HAVE_LIBASS], [1], [Define if libass is available])
else
  # Ensure variables exist even when libass not requested/available
  LIBASS_CFLAGS=""
  LIBASS_LIBS=""
fi
AC_SUBST([LIBASS_CFLAGS])
AC_SUBST([LIBASS_LIBS])
AM_CONDITIONAL([USE_LIBASS], test "x$enable_ass" = "xyes" && test "x$LIBASS_CFLAGS" != "x")

AC_ARG_ENABLE([thread-safe-mux],
  AS_HELP_STRING([--disable-thread-safe-mux], [Disable mutex guard around mux writes]),
  [enable_thread_safe_mux=$enableval],
  [enable_thread_safe_mux=yes])

if test "x$enable_thread_safe_mux" = "xno"; then
  AC_DEFINE([ENABLE_THREAD_SAFE_MUX], [0], [Define to 1 to enable mutex guard around mux writes])
  THREAD_SAFE_MUX_CPPFLAGS="-DENABLE_THREAD_SAFE_MUX=0"
else
  AC_DEFINE([ENABLE_THREAD_SAFE_MUX], [1], [Define to 1 to enable mutex guard around mux writes])
  THREAD_SAFE_MUX_CPPFLAGS="-DENABLE_THREAD_SAFE_MUX=1"
fi
AC_SUBST([THREAD_SAFE_MUX_CPPFLAGS])

if test "x$enable_thread_safe_mux" = "xno"; then
  AC_MSG_NOTICE([Thread-safe mux disabled])
fi

# Allow explicit override of version info for release builds
AC_ARG_WITH([git-version],
  AS_HELP_STRING([--with-git-version=VERSION], [Override GIT_VERSION (for releases)]),
  [override_git_version="$withval"],
  [override_git_version=""])

AC_ARG_WITH([git-commit],
  AS_HELP_STRING([--with-git-commit=COMMIT], [Override GIT_COMMIT (for releases)]),
  [override_git_commit="$withval"],
  [override_git_commit=""])

AC_ARG_WITH([git-date],
  AS_HELP_STRING([--with-git-date=DATE], [Override GIT_DATE (for releases, ISO format)]),
  [override_git_date="$withval"],
  [override_git_date=""])

# Detect Git version info or fall back to PACKAGE_VERSION, unless overridden
AC_MSG_CHECKING([for git version info])
if test -n "$override_git_version" ; then
  GIT_VERSION="$override_git_version"
  AC_MSG_RESULT([using --with-git-version=$GIT_VERSION])
elif test -d ../.git; then
  GIT_VERSION=`git describe --tags --always --dirty 2>/dev/null || echo "unknown"`
  AC_MSG_RESULT([$GIT_VERSION (detected from git)])
else
  GIT_VERSION="${PACKAGE_VERSION}"
  AC_MSG_RESULT([$GIT_VERSION (fallback to PACKAGE_VERSION)])
fi

AC_MSG_CHECKING([for git commit info])
if test -n "$override_git_commit" ; then
  GIT_COMMIT="$override_git_commit"
  AC_MSG_RESULT([using --with-git-commit=$GIT_COMMIT])
elif test -d ../.git; then
  GIT_COMMIT=`git rev-parse --short HEAD 2>/dev/null || echo "unknown"`
  AC_MSG_RESULT([$GIT_COMMIT (detected from git)])
else
  GIT_COMMIT="beta"
  AC_MSG_RESULT([$GIT_COMMIT (fallback)])
fi

AC_MSG_CHECKING([for git date info])
if test -n "$override_git_date" ; then
  GIT_DATE="$override_git_date"
  AC_MSG_RESULT([using --with-git-date=$GIT_DATE])
elif test -d ../.git; then
  GIT_DATE=`git log -1 --format=%cd --date=iso 2>/dev/null || date -Iseconds`
  AC_MSG_RESULT([$GIT_DATE (detected from git)])
else
  GIT_DATE=`date -Iseconds`
  AC_MSG_RESULT([$GIT_DATE (current date)])
fi

# Export to Makefile substitutions (correct shell assignment)
AC_SUBST(GIT_VERSION)
AC_SUBST(GIT_COMMIT)
AC_SUBST(GIT_DATE)

# Generate concrete version.h in the source tree during configure so it is
# available for both in-tree and out-of-tree builds and so source files that
# include it directly can find it. This avoids brittle Makefile-time sed
# substitutions which can be affected by Autoconf's own @VAR@ processing.
AC_MSG_NOTICE([Generating src/version.h in source tree])
if test -r "$srcdir/src/version.h.in" ; then
  sed -e "s|@GIT_VERSION@|$GIT_VERSION|g" \
      -e "s|@GIT_COMMIT@|$GIT_COMMIT|g" \
      -e "s|@GIT_DATE@|$GIT_DATE|g" \
      "$srcdir/src/version.h.in" > "$srcdir/src/version.h"
else
  AC_MSG_WARN([src/version.h.in not found; version.h will not be generated])
fi

# Generate output files
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

# Ensure the build 'src' directory exists for out-of-tree builds. This avoids
# relying on a Makefile recipe (which can be fragile if tabs are altered) and
# creates the directory at configure time where it's needed for subdir-objects.
# Configure runs from the build directory, so create './src' here.
mkdir -p src

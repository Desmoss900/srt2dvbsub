AC_INIT([srt2dvbmux], [1.0], [bugs@example.com])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AC_PROG_CC

AS_IF([test "$srcdir" = "$builddir"],
  [AC_MSG_ERROR([In-tree builds are not supported. Please build in a separate directory.])])

# Use pkg-config for deps
PKG_CHECK_MODULES([DEPS],
  [libavformat libavcodec libavutil pangocairo libass],
  [],
  [AC_MSG_ERROR([Required libraries not found. Install libavformat, libavcodec, libavutil, pangocairo, libass.])]
)

dnl --- FFmpeg dependency checks ---
PKG_CHECK_MODULES([FFMPEG],
  [libavformat >= 58.0.0
   libavcodec >= 58.0.0
   libavutil >= 56.0.0
   libswscale >= 5.0.0],
  [],
  [AC_MSG_ERROR([Missing FFmpeg development libraries (libavformat, libavcodec, libavutil, libswscale)])])

# Generate output files
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
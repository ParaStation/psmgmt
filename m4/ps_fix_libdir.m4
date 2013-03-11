#
# Fix wrong default value for libdir on x86_64 systems (unless
# overwritten on the commandline). If available, GCC is queried for
# the "multi-os-directory".
#
# Based on this message:
# http://lists.gnu.org/archive/html/autoconf/2008-09/msg00072.html
#
# GCC docs for print-multi-os-directory:
#
# Prints the path to OS libraries for the selected multilib, relative
# to some `lib' subdirectory.  If OS libraries are present in the
# `lib' subdirectory and no multilibs are used, this is usually just
# `.', if OS libraries are present in `libSUFFIX' sibling directories
# this prints e.g. `../lib64', `../lib' or `../lib32', or if OS
# libraries are present in `lib/SUBDIR' subdirectories it prints
# e.g. `amd64', `sparcv9' or `ev6'.
#
AC_DEFUN([PS_FIX_LIBDIR],[dnl
if test "$libdir" = '${exec_prefix}/lib'; then
  if test "$GCC" = yes; then
    ac_multilibdir=`$CC -print-multi-os-directory $CFLAGS $CPPFLAGS`
  else
    ac_multilibdir=.
  fi
  case "$ac_multilibdir" in
    .)      libdir=lib ;;
    ../*)   libdir=`echo $ac_multilibdir | sed 's/^...//' ` ;;
    *)      libdir=lib/$ac_multilibdir ;;
  esac
  libdir='${exec_prefix}/'$libdir
fi])

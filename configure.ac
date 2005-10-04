#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(["psmgmt"],[""],["eicker@par-tec.com"])
AC_CONFIG_AUX_DIR(["scripts"])

AC_CANONICAL_TARGET([]) 

AC_PROG_INSTALL
AC_PROG_RANLIB 

AC_SUBST([CFLAG_WALL])
AC_SUBST([CFLAG_G])
AC_SUBST([CFLAG_COMPAT])
AC_SUBST([CPPFLAG_MMD])
AC_SUBST([DEPEXTENSION])
AC_SUBST([TRU64FILEADD])
AC_SUBST([LIBCURSES])

case $target_os in
linux|linux-gnu)
	CFLAG_WALL="-W -Wall -Wno-unused-parameter"
	CFLAG_G=-g
	CFLAG_COMPAT=
	CPPFLAG_MMD=-MMD
	DEPEXTENSION=.d
	TRU64FILEADD=
	LIBCURSES=ncurses
	;;
*)
	echo "Unknown os ($target_os)"
	exit 1
	;;
esac

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([lib/Makefile])
AC_CONFIG_FILES([lib/Makefile.sharedlib])
AC_CONFIG_FILES([lib/Makefile.staticlib])
AC_CONFIG_FILES([lib/pse/Makefile])
AC_CONFIG_FILES([lib/psi/Makefile])
AC_CONFIG_FILES([lib/pscommon/Makefile])
AC_CONFIG_FILES([lib/pslog/Makefile])
AC_CONFIG_FILES([lib/putil/Makefile])
AC_CONFIG_FILES([bin/Makefile])
AC_CONFIG_FILES([dist/Makefile])


AC_OUTPUT

#
# Local Variables:
#  compile-command: "autoconf"
# End:
#

#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(["psmgmt"],[""],["eicker@par-tec.com"])
AC_CONFIG_AUX_DIR(["scripts"])

AC_CANONICAL_TARGET([]) 

AC_PROG_INSTALL
AC_PROG_RANLIB 

# Test for stack protector support in target C library.
case "$target" in
     *-*-linux*)
	AC_CACHE_CHECK(__stack_chk_fail in target GNU C library,
	gcc_cv_libc_provides_ssp,
	[gcc_cv_libc_provides_ssp=no

	glibc_header_dir=/usr/include
	# glibc 2.4 and later provides __stack_chk_fail and
	# either __stack_chk_guard, or TLS access to stack guard canary.
	if test -f $glibc_header_dir/features.h \
	   && egrep '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GNU_LIBRARY__[ 	]+([1-9][0-9]|[6-9])' \
	   $glibc_header_dir/features.h > /dev/null; then
	   if egrep '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC__[ 	]+([1-9][0-9]|[3-9])' \
	      $glibc_header_dir/features.h > /dev/null; then
	      gcc_cv_libc_provides_ssp=yes
	   elif egrep '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC__[ 	]+2' \
	      $glibc_header_dir/features.h > /dev/null \
	      && egrep '^@<:@ 	@:>@*#[ 	]*define[ 	]+__GLIBC_MINOR__[ 	]+([1-9][0-9]|[4-9])' \
		 $glibc_header_dir/features.h > /dev/null; then
		 gcc_cv_libc_provides_ssp=yes
	fi
      fi]) ;;
     *) gcc_cv_libc_provides_ssp=no ;;
esac

if test x$gcc_cv_libc_provides_ssp = xyes; then
   TARGET_LIBC_PROVIDES_SSP=yes
else
   TARGET_LIBC_PROVIDES_SSP=no
fi

AC_SEARCH_LIBS([numa_available], [numa], [NUMA=yes])
AC_SEARCH_LIBS([elan_nullcap], [elanctrl], [ELANCTRL=yes])

AC_SUBST([CFLAG_WALL])
AC_SUBST([CFLAG_G])
AC_SUBST([CFLAG_COMPAT])
AC_SUBST([CPPFLAG_MMD])
AC_SUBST([DEPEXTENSION])
AC_SUBST([TRU64FILEADD])
AC_SUBST([LIBCURSES])
AC_SUBST([TARGET_LIBC_PROVIDES_SSP])
AC_SUBST([NUMA])
AC_SUBST([ELANCTRL])

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

# Test for lib or lib64
AC_SUBST([LIB_DIRNAME])
case $target_cpu in
x86_64)
	LIB_DIRNAME=lib64
	;;
powerpc64)
	LIB_DIRNAME=lib64
	target_cpu=ppc64
	;;
*)
	LIB_DIRNAME=lib
	;;
esac

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([bin/Makefile])
AC_CONFIG_FILES([doc/Makefile])
AC_CONFIG_FILES([include/Makefile])
AC_CONFIG_FILES([lib/Makefile])
AC_CONFIG_FILES([lib/Makefile.sharedlib])
AC_CONFIG_FILES([lib/Makefile.staticlib])
AC_CONFIG_FILES([lib/pse/Makefile])
AC_CONFIG_FILES([lib/psi/Makefile])
AC_CONFIG_FILES([lib/pscommon/Makefile])
AC_CONFIG_FILES([lib/pslog/Makefile])
AC_CONFIG_FILES([lib/putil/Makefile])
AC_CONFIG_FILES([scripts/Makefile])


AC_OUTPUT

#
# Local Variables:
#  compile-command: "autoconf"
# End:
#

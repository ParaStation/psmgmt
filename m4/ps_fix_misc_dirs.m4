#
# We want some dirs to have other defaults.
#
AC_DEFUN([PS_FIX_MISC_DIRS],[dnl
if test "$sysconfdir" = '${prefix}/etc'; then
  sysconfdir='/etc'
fi
if test "$localstatedir" = '${prefix}/var'; then
  localstatedir='/var'
fi
if test "$pamdir" = '${libdir}/security'; then
  pamdir="/usr$(echo $libdir | sed s,\${exec_prefix},,)/security"
fi])

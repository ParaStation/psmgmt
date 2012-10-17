#
# ParaStation
#
# Copyright (C) 2012 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Thomas Moschny <moschny@par-tec.com>
#

#
# Call AC_SEARCH_LIBS, but do not pollute LIBS with the
# result. Instead, use the variable given a third arg.
#
AC_DEFUN([PS_SEARCH_LIBS], [dnl
  ps_search_libs_save_LIBS="$LIBS"
  LIBS="${$3}"
  AC_SEARCH_LIBS([$1], [$2],[dnl
    $3="$LIBS"
    $4
  ], [$5], [$6])
  LIBS="$ps_search_libs_save_LIBS"
  AC_SUBST([$3])
])



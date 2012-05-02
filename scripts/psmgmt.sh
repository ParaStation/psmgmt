#
# ParaStation
#
# Copyright (C) 2005-2010 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Authors:      Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
#               Norbert Eicker <n.eicker@fz-juelich.de>
#
# Script to set paths for ParaStation for each user at login time, to
# be put in /etc/profile.d/.
#
# Note: Changes made here should be made analogously in
# psmgmt.csh.
#
# $Id$
#

_psdir="/opt/parastation"

if test -d "${_psdir}/bin" ; then

    export PATH="${PATH}:${_psdir}/bin"
    if test `id -ur` -eq 0; then
	export PATH="${PATH}:${_psdir}/sbin"
    fi

    if test -d "${_psdir}/man" ; then
	if test -z "${MANPATH}" ; then
	    export MANPATH="`test -x /usr/bin/manpath && /usr/bin/manpath 2> /dev/null`"
	fi
	if test -z "${MANPATH}" ; then
	    export MANPATH="${_psdir}/man"
	else
	    export MANPATH="${MANPATH}:${_psdir}/man"
	fi
    fi
fi

unset _psdir

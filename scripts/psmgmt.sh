#
# Script to set paths for ParaStation for each user at login time, to
# be put in /etc/profile.d/.
#
# Note: Changes made here should be made analogously in
# psmgmt.csh.
#
# @author
#         Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
#         Norbert Eicker <n.eicker@fz-juelich.de>
#
# $Id$
#

_psdir="/opt/parastation"

if test -d "${_psdir}/bin" ; then

    export PATH="${PATH}:${_psdir}/bin"

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

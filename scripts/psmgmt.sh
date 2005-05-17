#! /bin/sh
#
# Script to set paths for ParaStation for each user at login time, to
# be put in /etc/profile.d/.
#
# Note: Changes made here should be made analogously in
# psmgmt.csh.
#
# @author
#         Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
#
# $Id$
#

_psconf="/etc/parastation.conf"
_psdir="/opt/parastation"

if test -r "${_psconf}" ; then

    _re1='^[[:space:]]*InstallDir\b'
    _re2='[[:space:]]*\(.*\)$'
    _psdir=`sed -e '/'${_re1}'/!d' -e 's/'${_re1}${_re2}'/\1/' "${_psconf}"`

fi

if test -d "${_psdir}/bin" ; then

    export PATH="${PATH}:${_psdir}/bin"
    
    if test -d "${_psdir}/man" ; then
	if test "${MANPATH}" ; then
	    export MANPATH="${MANPATH}:${_psdir}/man"
	fi
    fi
fi

unset _psdir
unset _psconf

#! /usr/bin/csh -f
#
# Script to set paths for ParaStation for each user at login time, to
# be put in /etc/profile.d/.
#
# Note: Changes made here should be made analogously in
# psmgmt.sh.
#
# @author
#         Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
#
# $Id$
#

if ( -e /etc/parastation.conf ) then
    set _psdir = `< /etc/parastation.conf grep '^InstallDir' | sed 's,^InstallDir[[:space:]]\+\(.*\)$,\1,'`

    if ( -d ${_psdir}/bin ) then
        set path = ( ${path} ${_psdir}/bin )

        if ( -d ${_psdir}/man ) then
            if ( ${?MANPATH} ) then
                setenv MANPATH ${MANPATH}:${_psdir}/man
            endif
        endif
    endif

    unset _psdir
endif

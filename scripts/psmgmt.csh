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

set _psconf="/etc/parastation.conf"
set _psdir="/opt/parastation"

if ( -e "${_psconf}" ) then

    set _re1='^[[:space:]]*InstallDir\b'
    set _re2='[[:space:]]*\(.*\)$'
    set _psdir=`sed -e '/'${_re1}'/!d' -e 's/'${_re1}${_re2}'/\1/' "${_psconf}"`

endif

if ( -d "${_psdir}/bin" ) then
    
    set path = ( ${path} "${_psdir}/bin" )

    if ( -d "${_psdir}/man" ) then
	if ( ${?MANPATH} ) then
	    setenv MANPATH "${MANPATH}:${_psdir}/man"
        endif
    endif
endif

unset _psdir
unset _psconf

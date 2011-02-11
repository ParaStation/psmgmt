#
# Script to set paths for ParaStation for each user at login time, to
# be put in /etc/profile.d/.
#
# Note: Changes made here should be made analogously in
# psmgmt.sh.
#
# @author
#         Thomas Moschny <moschny@ipd.uni-karlsruhe.de>
#         Norbert Eicker <n.eicker@fz-juelich.de>
#
# $Id$
#

set _psdir="/opt/parastation"

if ( -d "${_psdir}/bin" ) then

    set path = ( ${path} "${_psdir}/bin" )
    if ( `id -ur` == 0 ) then
	set path = ( ${path} "${_psdir}/sbin" )
    endif

    if ( -d "${_psdir}/man" ) then
	if ( ! ${?MANPATH} ) then
	    setenv MANPATH "`(test -x /usr/bin/manpath && ( /usr/bin/manpath > /dev/tty ) >& /dev/null)`"
	endif
	if ( ${?MANPATH} ) then
	    setenv MANPATH "${MANPATH}:${_psdir}/man"
	else
	    setenv MANPATH "${_psdir}/man"
	endif
    endif
endif

unset _psdir

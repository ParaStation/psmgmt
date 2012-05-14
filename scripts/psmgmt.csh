#
# ParaStation
#
# Copyright (C) 2005-2011 ParTec Cluster Competence Center GmbH, Munich
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
# psmgmt.sh.
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

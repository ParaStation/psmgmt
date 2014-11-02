#
# ParaStation
#
# Copyright (C) 2013 ParTec Cluster Competence Center GmbH, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:	Michael Rauh <rauh@par-tec.com>
#

include $(top_srcdir)/common.am

dist_plugin_DATA = psmom.conf
dist_doc_DATA = psmom-prologue-example psmom-epilogue-example psmom-timeout-example

dist_pamconf_DATA = psmom
EXTRA_DIST = psmom.pam

psmom: $(srcdir)/psmom.pam
	$(INSTALL) $^ $@

CLEANFILES=psmom.pam

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(pamconfdir)

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

include $(top_srcdir)/common.am

dist_plugin_DATA = psresport.conf psaccount.conf

# install, but do not pack, since it is generated
nodist_doc_DATA = parastation.conf.tmpl

parastation.conf.tmpl: $(srcdir)/parastation.conf
	$(INSTALL) $^ $@

CLEANFILES = parastation.conf.tmpl

install-exec-local:
	$(INSTALL) -d $(DESTDIR)$(localstatedir)/log/psaccounter

EXTRA_DIST = MessageTypes

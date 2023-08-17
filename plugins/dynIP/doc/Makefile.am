#
# ParaStation
#
# Copyright (C) 2023 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
include $(top_srcdir)/common.am

DOCDIR_AWSPC = AWS_ParallelCluster
EXTRA_DIST = $(DOCDIR_AWSPC)

TARGET_DIR = $(docdir)/dynIP

install-data-local:
	cd "$(srcdir)"; for dir in $$(find "$(DOCDIR_AWSPC)" -type d); do \
		test -z "$(TARGET_DIR)/$${dir}" || $(MKDIR_P) "$(TARGET_DIR)/$${dir}" ;\
		find "$${dir}" -type f -exec $(INSTALL_DATA) {} "$(TARGET_DIR)/$${dir}" \; ;\
	done

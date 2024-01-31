#
# ParaStation
#
# Copyright (C) 2023-2024 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#

include $(top_srcdir)/common.am

dist_noinst_DATA = README.bpf

if BPF_ENABLED
dist_jaillib_SCRIPTS = bpf_cgroup_device
jaillib_PROGRAMS = bpf_device_loader
bpf_device_loader_SOURCES = load_bpf.c
bpf_device_loader_LDADD = -lbpf -lpopt
endif

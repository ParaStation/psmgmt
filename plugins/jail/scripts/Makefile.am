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

dist_jaillib_SCRIPTS = jail-config.inc jail-v1-cpuacct.inc		\
		       jail-v1-cpuset.inc jail-functions.inc		\
		       jail-main.sh jail-v1-oom.inc			\
		       jail-term-v1-cpuacct.inc jail-term-v1-cpuset.inc	\
		       jail-v1-memory.inc jail-v1-devices.inc		\
		       jail-term-v1-memory.inc jail-term-v1-devices.inc	\
		       jail-init.sh

if BPF_ENABLED
jaillib_PROGRAMS = bpf_device_loader
bpf_device_loader_SOURCES = load_bpf.c
bpf_device_loader_LDADD = -lbpf
endif

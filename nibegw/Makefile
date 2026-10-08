############################################################################
# nibegw/Makefile
#
# SPDX-License-Identifier: EPL-2.0
#
############################################################################

include $(APPDIR)/Make.defs

PROGNAME  = $(CONFIG_NIBEGW_PROGNAME)
PRIORITY  = SCHED_PRIORITY_DEFAULT
STACKSIZE = $(CONFIG_DEFAULT_TASK_STACKSIZE)
MODULE    = $(CONFIG_NIBEGW)

MAINSRC = nibegw_main.c
CSRCS   = nibegw_gw.c nibegw_proto.c nibegw_conf.c

ifeq ($(CONFIG_NIBEGW_BOOT),y)
PROGNAME  += nibegwboot
PRIORITY  += $(CONFIG_SYSTEM_NSH_PRIORITY)
STACKSIZE += $(CONFIG_SYSTEM_NSH_STACKSIZE)
MAINSRC   += nibegw_boot.c
CSRCS     += nibegw_net.c
endif

include $(APPDIR)/Application.mk

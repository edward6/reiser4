#
# reiser4/plugin/item/Makefile
#

ifdef CONFIG_REISER4_CHECK
CFLAGS_static_stat.o += -O0
#CFLAGS_sde.o += -O0
#CFLAGS_cde.o += -O0
CFLAGS_internal.o += -O0
#CFLAGS_tail.o += -O0
#CFLAGS_extent.o += -O0
CFLAGS_item.o += -O0
endif

obj-y	+= static_stat.o sde.o cde.o internal.o tail.o extent.o item.o

include $(TOPDIR)/Rules.make

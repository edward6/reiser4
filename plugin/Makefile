#
# reiser4/plugin/Makefile
#

ifdef CONFIG_REISER4_CHECK
CFLAGS_object.o += -O0
CFLAGS_hash.o += -O0
CFLAGS_tail_policy.o += -O0
CFLAGS_plugin.o += -O0
CFLAGS_symlink.o += -O0
endif

obj-y	+= object.o hash.o tail_policy.o plugin.o symlink.o

obj-y	+= node/
obj-y	+= item/
obj-y	+= dir/
obj-y	+= file/
obj-y	+= oid/
obj-y	+= space/
obj-y	+= disk_format/
obj-y	+= security/

include $(TOPDIR)/Rules.make

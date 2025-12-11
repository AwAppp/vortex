ROOT_DIR := $(realpath ../../..)
include $(ROOT_DIR)/config.mk

PROJECT := matreduce

SRC_DIR := $(VORTEX_HOME)/tests/regression/$(PROJECT)

SRCS := $(SRC_DIR)/main.cpp

VX_SRCS := $(SRC_DIR)/kernel.cpp

OPTS ?= -n8

include ../common.mk

# Child kernel as separate binary
child_kernel.dump: child_kernel.elf
	$(VX_DP) -D $< > $@

child_kernel.vxbin: child_kernel.elf
	OBJCOPY=$(VX_CP) $(VORTEX_HOME)/kernel/scripts/vxbin.py $< $@

CHILD_STARTUP_ADDR = 0x80010000

CHILD_LDFLAGS = -Wl,-Bstatic,--gc-sections,-T,$(VORTEX_HOME)/kernel/scripts/link$(XLEN).ld,--defsym=STARTUP_ADDR=$(CHILD_STARTUP_ADDR) $(VORTEX_KN_PATH)/libvortex.a $(VX_LIBS)

child_kernel.elf: $(SRC_DIR)/child_kernel.cpp
	$(VX_CXX) $(VX_CFLAGS) $^ $(CHILD_LDFLAGS) -o $@

all: $(PROJECT) kernel.vxbin kernel.dump child_kernel.vxbin child_kernel.dump

clean-kernel:
	rm -rf *.elf *.vxbin *.dump

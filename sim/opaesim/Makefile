include ../common.mk

DESTDIR ?= $(CURDIR)

CONFIG_FILE := $(DESTDIR)/opaesim_config.stamp
SRC_DIR := $(VORTEX_HOME)/sim/opaesim
AFU_DIR := $(RTL_DIR)/afu/opae

CXXFLAGS += -std=c++17 -Wall -Wextra -Wfatal-errors -Wno-array-bounds
CXXFLAGS += -fPIC -Wno-maybe-uninitialized
CXXFLAGS += -I$(SRC_DIR) -I$(ROOT_DIR)/hw -I$(SW_COMMON_DIR) -I$(DESTDIR)
CXXFLAGS += -I/$(THIRD_PARTY_DIR)/softfloat/source/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/ext/spdlog/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/ext/yaml-cpp/include
CXXFLAGS += -I$(THIRD_PARTY_DIR)/ramulator/src
CXXFLAGS += -DXLEN_$(XLEN)
CXXFLAGS += $(CONFIGS)

LDFLAGS += -shared $(THIRD_PARTY_DIR)/softfloat/build/Linux-x86_64-GCC/softfloat.a
LDFLAGS += -Wl,-rpath,$(THIRD_PARTY_DIR)/ramulator -L$(THIRD_PARTY_DIR)/ramulator -lramulator -pthread

# control RTL debug tracing states
DBG_TRACE_FLAGS += -DDBG_TRACE_PIPELINE
DBG_TRACE_FLAGS += -DDBG_TRACE_MEM
DBG_TRACE_FLAGS += -DDBG_TRACE_CACHE
DBG_TRACE_FLAGS += -DDBG_TRACE_AFU
DBG_TRACE_FLAGS += -DDBG_TRACE_SCOPE
DBG_TRACE_FLAGS += -DDBG_TRACE_GBAR
DBG_TRACE_FLAGS += -DDBG_TRACE_TCU

# Control logic analyzer monitors
DBG_SCOPE_FLAGS += -DDBG_SCOPE_AFU
DBG_SCOPE_FLAGS += -DDBG_SCOPE_ISSUE
DBG_SCOPE_FLAGS += -DDBG_SCOPE_FETCH
DBG_SCOPE_FLAGS += -DDBG_SCOPE_LSU

DBG_FLAGS += -DDEBUG_LEVEL=$(DEBUG) -DVCD_OUTPUT $(DBG_TRACE_FLAGS)

SRCS = $(SW_COMMON_DIR)/util.cpp $(SW_COMMON_DIR)/mem.cpp $(SW_COMMON_DIR)/softfloat_ext.cpp $(SW_COMMON_DIR)/rvfloats.cpp $(SW_COMMON_DIR)/dram_sim.cpp
SRCS += $(DPI_DIR)/util_dpi.cpp $(DPI_DIR)/float_dpi.cpp
SRCS += $(SRC_DIR)/fpga.cpp $(SRC_DIR)/opae_sim.cpp

RTL_PKGS = $(AFU_DIR)/local_mem_cfg_pkg.sv $(AFU_DIR)/ccip/ccip_if_pkg.sv
RTL_PKGS += $(RTL_DIR)/VX_gpu_pkg.sv $(RTL_DIR)/fpu/VX_fpu_pkg.sv

FPU_INCLUDE = -I$(RTL_DIR)/fpu
ifneq (,$(findstring -DFPU_FPNEW, $(CONFIGS)))
	RTL_PKGS += $(THIRD_PARTY_DIR)/cvfpu/src/fpnew_pkg.sv $(THIRD_PARTY_DIR)/cvfpu/src/common_cells/src/cf_math_pkg $(THIRD_PARTY_DIR)/cvfpu/src/fpu_div_sqrt_mvp/hdl/defs_div_sqrt_mvp.sv
	FPU_INCLUDE += -I$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/include -I$(THIRD_PARTY_DIR)/cvfpu/src/common_cells/src -I$(THIRD_PARTY_DIR)/cvfpu/src/fpu_div_sqrt_mvp/hdl -I$(THIRD_PARTY_DIR)/cvfpu/src
endif
RTL_INCLUDE = -I$(SRC_DIR) -I$(RTL_DIR) -I$(DPI_DIR) -I$(RTL_DIR)/libs -I$(RTL_DIR)/interfaces -I$(RTL_DIR)/core -I$(RTL_DIR)/mem -I$(RTL_DIR)/cache $(FPU_INCLUDE)
RTL_INCLUDE += -I$(AFU_DIR) -I$(AFU_DIR)/ccip

RTL_INCLUDE += -I$(RTL_DIR)/kmu

# Add V extension sources
ifneq (,$(findstring -DEXT_V_ENABLE, $(CONFIGS)))
	RTL_PKGS += $(RTL_DIR)/vpu/VX_vpu_pkg.sv
	RTL_INCLUDE += -I$(RTL_DIR)/vpu
endif

# Add TCU extension sources
ifneq (,$(findstring -DEXT_TCU_ENABLE, $(CONFIGS)))
	RTL_PKGS += $(RTL_DIR)/tcu/VX_tcu_pkg.sv
	RTL_INCLUDE += -I$(RTL_DIR)/tcu
	RTL_INCLUDE += -I$(RTL_DIR)/tcu/bhf
	RTL_INCLUDE += -I$(THIRD_PARTY_DIR)/hardfloat/source/RISCV
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/fNToRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/addRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/bsg_hardfloat_pkg.sv
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/HardFloat_primitives.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/HardFloat_rawFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/isSigNaNRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/mulRecFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/recFNToFN.v
	RTL_PKGS += $(THIRD_PARTY_DIR)/hardfloat/source/recFNToRecFN.v
endif

RTL_PKGS += $(RTL_DIR)/VX_trace_pkg.sv

TOP = vortex_afu_shim

VL_FLAGS += --language 1800-2009 --assert -Wall -Wpedantic
VL_FLAGS += -Wno-DECLFILENAME -Wno-REDEFMACRO
VL_FLAGS += --x-initial unique --x-assign unique
VL_FLAGS += -DSIMULATION -DSV_DPI
VL_FLAGS += -DXLEN_$(XLEN)
VL_FLAGS += $(CONFIGS)
VL_FLAGS += verilator.vlt
VL_FLAGS += $(RTL_INCLUDE)
VL_FLAGS += $(RTL_PKGS)

# Extract RTL directories from include directories
RTL_DIRS := $(patsubst -I%,%,$(filter -I%,$(RTL_INCLUDE)))

# Discover RTL source files from source directories
RTL_SRCS := $(shell find $(RTL_DIRS) -type f \( -name '*.v' -o -name '*.vh' -o -name '*.sv' \))

# Enable Verilator multithreaded simulation
THREADS ?= $(shell python3 -c 'import multiprocessing as mp; print(mp.cpu_count())')
VL_FLAGS += -j $(THREADS)
#VL_FLAGS += --threads $(THREADS)

# Debugging
ifdef DEBUG
	VL_FLAGS += --trace --trace-structs $(DBG_FLAGS)
	CXXFLAGS += -g -O0 $(DBG_FLAGS)
else
	VL_FLAGS += -DNDEBUG
	CXXFLAGS += -O2 -DNDEBUG
endif

# Enable scope analyzer
ifdef SCOPE
	VL_FLAGS += -DSCOPE $(DBG_SCOPE_FLAGS)
	CXXFLAGS += -DSCOPE
	SCOPE_JSON = $(DESTDIR)/scope.json
endif

# Enable perf counters
ifdef PERF
	VL_FLAGS += -DPERF_ENABLE
	CXXFLAGS += -DPERF_ENABLE
endif

# use our OPAE shim
VL_FLAGS += -DNOPAE
CXXFLAGS += -DNOPAE

PROJECT := libopae-c-sim.so

.PHONY: all force clean

all: $(DESTDIR)/$(PROJECT)

$(DESTDIR)/vortex.xml: $(CONFIG_FILE)
	verilator --xml-only -O0 $(VL_FLAGS) $(TOP) --xml-output $@

$(DESTDIR)/scope.json: $(DESTDIR)/vortex.xml
	$(SCRIPT_DIR)/scope.py $^ -o $@

$(DESTDIR)/vortex_afu.h : $(AFU_DIR)/vortex_afu.vh
	$(SCRIPT_DIR)/gen_config.py -i $^ -o $@

$(DESTDIR)/$(PROJECT): $(SRCS) $(DESTDIR)/vortex_afu.h $(SCOPE_JSON) $(CONFIG_FILE) $(RTL_SRCS)
	verilator --build --exe $(VL_FLAGS) --cc $(TOP) --top-module $(TOP) $(SRCS) -CFLAGS '$(CXXFLAGS)' -LDFLAGS '$(LDFLAGS)' --Mdir $@.obj_dir -o $@

# updates the timestamp when flags changed.
$(CONFIG_FILE): force
	@mkdir -p $(@D)
	@printf '%s\n' "$(VL_FLAGS)" > $@.tmp
	@if ! cmp -s $@.tmp $@; then \
	  mv $@.tmp $@; \
	else \
	  rm $@.tmp; \
	fi

clean:
	rm -rf $(DESTDIR)/$(PROJECT).obj_dir
	rm -f $(DESTDIR)/vortex.xml $(DESTDIR)/scope.json $(DESTDIR)/vortex_afu.h $(DESTDIR)/$(PROJECT)

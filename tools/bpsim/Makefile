# Builds the replay harness (bpsim) and the trace decoder (trace2stream).
#   make -C tools/bpsim            -> bin/bpsim, bin/bpsim_hp{11..17}, bin/trace2stream, bin/bpsim_nobmi2
# Predictor and BTB sources are compiled with ChampSim's own flags (global.options + module.options),
# so their code is compiled exactly as in a ChampSim build. Run `make` at the repo root first
# (vcpkg dependencies must be installed).
ROOT := $(abspath ../..)
VCPKG := $(ROOT)/vcpkg_installed/x64-linux
OBJ := $(ROOT)/.cache/bpsim_obj
BIN := $(ROOT)/bin

CHAMPSIM_FLAGS := --std=c++17 -O3 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wno-unused-parameter -DCHAMPSIM_MODULE
INCLUDES := -I$(ROOT)/inc -isystem $(VCPKG)/include -I$(ROOT)
LDFLAGS := -L$(VCPKG)/lib -L$(VCPKG)/lib/manual-link
LDLIBS := -llzma -lz -lbz2 -lfmt

CHAMPSIM_SRC := src/tracereader.cc src/address.cc src/chrono.cc
MODULE_SRC := branch/wisard/wisard.cc branch/bimodal/bimodal.cc branch/gshare/gshare.cc branch/perceptron/perceptron.cc \
              tools/bpsim/reference/wisard_mpp_v2/wisard.cc \
              btb/basic_btb/basic_btb.cc btb/basic_btb/direct_predictor.cc btb/basic_btb/indirect_predictor.cc btb/basic_btb/return_stack.cc
ADAPTER_SRC := tools/bpsim/adapters/adapter_wisard.cc tools/bpsim/adapters/adapter_wisard_mpp_v2.cc tools/bpsim/adapters/adapter_bimodal.cc \
               tools/bpsim/adapters/adapter_gshare.cc tools/bpsim/adapters/adapter_perceptron.cc tools/bpsim/adapters/adapter_btb.cc \
               tools/bpsim/adapters/adapter_wnn.cc tools/bpsim/adapters/factory.cc
HP_SRC := branch/hashed_perceptron/hashed_perceptron.cc tools/bpsim/adapters/adapter_hashed_perceptron.cc

common_obj := $(patsubst %.cc,$(OBJ)/common/%.o,$(CHAMPSIM_SRC) $(MODULE_SRC) $(ADAPTER_SRC))
wnn_bmi2 := $(OBJ)/bmi2/branch/wnn/wnn_core.o
wnn_portable := $(OBJ)/portable/branch/wnn/wnn_core.o
HP_BITS := 11 12 13 14 15 16 17

.PHONY: all clean
all: $(BIN)/bpsim $(BIN)/trace2stream $(BIN)/wnn_info $(BIN)/bst_compact $(BIN)/bpsim_nobmi2 $(foreach k,$(HP_BITS),$(BIN)/bpsim_hp$(k))

$(OBJ)/common/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CHAMPSIM_FLAGS) $(INCLUDES) -MMD -c -o $@ $<

$(OBJ)/bmi2/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CHAMPSIM_FLAGS) -mbmi2 $(INCLUDES) -MMD -c -o $@ $<

$(OBJ)/portable/%.o: $(ROOT)/%.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CHAMPSIM_FLAGS) $(INCLUDES) -MMD -c -o $@ $<

define hp_rules
$(OBJ)/hp$(1)/%.o: $(ROOT)/%.cc
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CHAMPSIM_FLAGS) -DHP_TABLE_BITS=$(1) $$(INCLUDES) -MMD -c -o $$@ $$<
$(BIN)/bpsim_hp$(1): $(OBJ)/common/tools/bpsim/bpsim.o $(common_obj) $(wnn_bmi2) $(patsubst %.cc,$(OBJ)/hp$(1)/%.o,$(HP_SRC))
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(LDFLAGS) -o $$@ $$^ $$(LDLIBS)
endef
$(foreach k,$(HP_BITS),$(eval $(call hp_rules,$(k))))

$(BIN)/bpsim: $(BIN)/bpsim_hp11
	cp $< $@

$(BIN)/bpsim_nobmi2: $(OBJ)/common/tools/bpsim/bpsim.o $(common_obj) $(wnn_portable) $(patsubst %.cc,$(OBJ)/hp11/%.o,$(HP_SRC))
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/trace2stream: $(OBJ)/common/tools/bpsim/trace2stream.o $(patsubst %.cc,$(OBJ)/common/%.o,$(CHAMPSIM_SRC))
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/bst_compact: $(OBJ)/common/tools/bpsim/bst_compact.o $(OBJ)/common/tools/bpsim/adapters/adapter_btb.o $(OBJ)/common/src/address.o \
                    $(OBJ)/common/btb/basic_btb/basic_btb.o $(OBJ)/common/btb/basic_btb/direct_predictor.o $(OBJ)/common/btb/basic_btb/indirect_predictor.o \
                    $(OBJ)/common/btb/basic_btb/return_stack.o
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BIN)/wnn_info: $(OBJ)/common/tools/bpsim/wnn_info.o $(wnn_bmi2)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -rf $(OBJ) $(BIN)/bpsim $(BIN)/wnn_info $(BIN)/bst_compact $(BIN)/bpsim_nobmi2 $(BIN)/trace2stream $(foreach k,$(HP_BITS),$(BIN)/bpsim_hp$(k))

-include $(shell find $(OBJ) -name '*.d' 2>/dev/null)

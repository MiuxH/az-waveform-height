# SPDX-License-Identifier: GPL-3.0-only
# Build using the pinned official SDK ARM64 container (Build-Community.py).
# make test runs the hook adapter fixtures: SAN="plain asan tsan".
# The generic SDK services-only harness cannot exercise native firmware hooks.

MOD_ID          := miuxh-wave-height
SRCS            := height.c
AZ_SDK_INCLUDE  ?= .
AZ_SDK          ?= .azmod-sdk
MAX_REDRAWS     ?= 4          # the most repaints a second your tick asks for
SAN             ?= plain asan tsan
TEST_CC         ?= clang
TEST_CXX        ?= clang++
OUT             ?= out
DEBUG           ?= 0          # 1: add -g (debug info only; the code is the same)

# make's built-in default is cc; use gcc unless CC was given on the command line
ifeq ($(origin CC),default)
CC := gcc
endif
CFLAGS  := -O2 -Wall -Wextra -Werror=implicit-function-declaration \
           -fPIC -fvisibility=hidden -fno-omit-frame-pointer \
           -fexceptions -fasynchronous-unwind-tables \
           -I$(AZ_SDK_INCLUDE)
LDFLAGS := -shared -static-libgcc -Wl,--no-undefined -Wl,--hash-style=gnu
LDLIBS  :=        # add -lpthread if you start a thread, -lm for math functions

# GCC 10+ on aarch64 defaults to outline atomics (a libgcc helper). The SDK's GCC 7
# does not know the flag; probe for it so the same Makefile works on both.
NO_OUTLINE_ATOMICS := $(shell printf 'int main(void){return 0;}\n' | \
    $(CC) -mno-outline-atomics -x c -o /dev/null - 2>/dev/null && echo -mno-outline-atomics)
CFLAGS += $(NO_OUTLINE_ATOMICS)

CXX_SRCS   := $(filter %.cpp %.cc,$(SRCS))
CXXFLAGS   := -O2 -Wall -Wextra -fPIC -fvisibility=hidden -fvisibility-inlines-hidden \
              -fno-omit-frame-pointer -fexceptions -fasynchronous-unwind-tables \
              -I$(AZ_SDK_INCLUDE) $(NO_OUTLINE_ATOMICS)
CXXLDFLAGS := -shared -Wl,--no-undefined -Wl,--hash-style=gnu \
              -Wl,--version-script=$(OUT)/exports.map
ifeq ($(strip $(DEBUG)),1)
CFLAGS   += -g
CXXFLAGS += -g
endif

comma := ,
space := $(subst ,, )

.PHONY: all clean test
all: $(OUT)/$(MOD_ID).so

ifeq ($(strip $(CXX_SRCS)),)
$(OUT)/$(MOD_ID).so: $(SRCS) height_model.h firmware.h icon_mask.h $(AZ_SDK_INCLUDE)/azmod-mod.h | $(OUT)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LDLIBS)
else
OBJS := $(patsubst %,$(OUT)/obj/%.o,$(SRCS))
$(OUT)/obj/%.c.o: %.c $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OUT)/obj/%.cpp.o: %.cpp $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OUT)/obj/%.cc.o: %.cc $(AZ_SDK_INCLUDE)/azmod-mod.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<
$(OUT)/exports.map: | $(OUT)
	printf '{ global: az_mod_v1_entry; local: *; };\n' > $@
$(OUT)/$(MOD_ID).so: $(OBJS) $(OUT)/exports.map
	$(CXX) $(CXXLDFLAGS) -o $@ $(OBJS) $(LDLIBS)
endif

$(OUT):
	mkdir -p $(OUT)

test:
	python3 tests/run.py --cc $(CC) --passes $(SAN)

clean:
	rm -rf $(OUT)

# Compile the lifted C, the engine's generic runtime and the game runtime (every
# Win32 slice and the D3D9/D3DX layer) into build.nosync/lhp, the native game.
# Run as: make -C build -j8 [OPT=1] [GEN=<lift output dir>] [app]
# setup.sh passes every path below. The graphics libraries come from tools/deps.
ROOT := $(abspath ..)
GEN ?= $(ROOT)/generated.nosync
# ENGINE must match the lifter that wrote GEN: GEN/recomp_types.h and the
# runtime share the thread-state layout.
ENGINE ?= $(ROOT)/engine
OUT := $(ROOT)/build.nosync
OBJ := $(OUT)/obj
DEPS ?= $(OUT)/deps
DXVK_PREFIX ?= $(DEPS)/dxvk
VKD3D_PREFIX ?= $(DEPS)/vkd3d
KK_PREFIX ?= $(DEPS)/kosmickrisp
BREW_PREFIX ?= /opt/homebrew
FAUDIO := $(BREW_PREFIX)/opt/faudio

CC := clang
# TSO=1 (default): lifted guest loads/stores keep x86 ordering (ldapr/stlr).
TSO ?= 1
OPT ?= 2
CFLAGS := -std=gnu11 -DRECOMP_TSO=$(TSO) -O$(OPT) -DRECOMP_OPT=$(OPT) -fwrapv -fno-strict-aliasing -ffp-contract=off -arch arm64 -I $(ENGINE)/tools/xrecomp/include -I $(GEN) -I $(ROOT)/runtime/include -I $(ENGINE)/runtime/include -w -DRECOMP_GLUE_NO_MAIN
# Host headers for the game runtime: DXVK, SDL2, FAudio, vkd3d-shader.
HOST_CFLAGS := -I $(DXVK_PREFIX)/include/dxvk -I $(BREW_PREFIX)/include -I $(BREW_PREFIX)/include/SDL2 -I $(FAUDIO)/include -I $(VKD3D_PREFIX)/include -I $(VKD3D_PREFIX)/include/vkd3d
LDLIBS := -L$(DXVK_PREFIX)/lib -ldxvk_d3d9 -Wl,-rpath,$(DXVK_PREFIX)/lib \
	-L$(VKD3D_PREFIX)/lib -lvkd3d-shader -Wl,-rpath,$(VKD3D_PREFIX)/lib \
	-L$(FAUDIO)/lib -lFAudio -Wl,-rpath,$(FAUDIO)/lib \
	-L$(BREW_PREFIX)/lib -lSDL2 -framework CoreGraphics -framework CoreFoundation -lobjc -lc++

# Generic engine runtime: guest memory, glue, guest calls, host functions,
# heap, diagnostics, overlay text.
ENGINE_RT := recomp_glue guest_mem guest_call host_funcs guest_heap dlmalloc diag hud_core
# Game runtime: every file in runtime/src except hp_main.c, the load-only test driver.
HP_SRCS := $(filter-out %/hp_main.c,$(wildcard $(ROOT)/runtime/src/*.c))

GEN_SRCS := $(wildcard $(GEN)/*.c)
GEN_OBJS := $(patsubst $(GEN)/%.c,$(OBJ)/gen_%.o,$(GEN_SRCS))
ENGINE_OBJS := $(patsubst %,$(OBJ)/rt_%.o,$(ENGINE_RT))
HP_OBJS := $(patsubst $(ROOT)/runtime/src/%.c,$(OBJ)/hp_%.o,$(HP_SRCS))

GEN_HEADERS := $(wildcard $(GEN)/*.h) $(ENGINE)/tools/xrecomp/include/recomp_types.h
RT_HEADERS := $(wildcard $(ROOT)/runtime/include/*.h) $(wildcard $(ENGINE)/runtime/include/*.h) $(ENGINE)/tools/xrecomp/include/recomp_types.h

# Make does not track CFLAGS, ENGINE or GEN. These stamps change when they do, so
# switching GEN rebuilds the generated objects and switching OPT, TSO or ENGINE
# rebuilds everything.
FLAGS_STAMP := $(OBJ)/flags.stamp
GEN_STAMP := $(OBJ)/gen.stamp
$(shell mkdir -p $(OBJ); \
  [ "$$(cat $(FLAGS_STAMP) 2>/dev/null)" = "O$(OPT) TSO$(TSO) $(ENGINE)" ] || echo "O$(OPT) TSO$(TSO) $(ENGINE)" > $(FLAGS_STAMP); \
  [ "$$(cat $(GEN_STAMP) 2>/dev/null)" = "$(GEN)" ] || echo "$(GEN)" > $(GEN_STAMP))

all: $(OUT)/lhp

# -export_dynamic: crash backtraces name the lifted functions (sub_<VA>).
# The header padding lets tools/package/make_app.sh rewrite install names and rpaths.
$(OUT)/lhp: $(GEN_OBJS) $(ENGINE_OBJS) $(HP_OBJS)
	$(CC) -arch arm64 -Wl,-export_dynamic -Wl,-headerpad_max_install_names -o $@ $^ $(LDLIBS)

$(OBJ)/gen_%.o: $(GEN)/%.c $(GEN_HEADERS) $(FLAGS_STAMP) $(GEN_STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(ENGINE_OBJS): $(OBJ)/rt_%.o: $(ENGINE)/runtime/src/%.c $(RT_HEADERS) $(FLAGS_STAMP)
	$(CC) $(CFLAGS) -c $< -o $@

$(HP_OBJS): $(OBJ)/hp_%.o: $(ROOT)/runtime/src/%.c $(RT_HEADERS) $(FLAGS_STAMP)
	$(CC) $(CFLAGS) $(HOST_CFLAGS) -c $< -o $@

# Build and run every runtime test (tests/run_all.sh) against ENGINE.
# Pass more options with TEST_ARGS, for example TEST_ARGS=--no-gpu.
TEST_ARGS ?=
test:
	HPRECOMP_DEPS_DIR="$(DEPS)" $(ROOT)/tests/run_all.sh --engine $(ENGINE) $(TEST_ARGS)

# Double-clickable bundle "build.nosync/LEGO Harry Potter Years 1-4.app" with every
# non-system library inside it (tools/package/make_app.sh). The launcher uses
# HP_GAME_DIR (the game folder) and HP_EXE (the game image) by default.
APP_NAME := LEGO Harry Potter Years 1-4
HP_GAME_DIR ?=
HP_EXE ?= $(ROOT)/orig.nosync/lhp_win32.exe

app: $(OUT)/lhp
	DXVK_PREFIX="$(DXVK_PREFIX)" VKD3D_PREFIX="$(VKD3D_PREFIX)" KK_PREFIX="$(KK_PREFIX)" \
	BREW_PREFIX="$(BREW_PREFIX)" HP_GAME_DIR="$(HP_GAME_DIR)" HP_EXE="$(HP_EXE)" \
	$(ROOT)/tools/package/make_app.sh "$(OUT)/lhp" "$(OUT)/$(APP_NAME).app"

.PHONY: all test app

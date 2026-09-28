# SteamFrameRaylibQuickstart -- raylib + C apps for the Steam Frame.
#
# Everything (raylib 6.0, the OpenXR loader) is vendored as source tarballs
# under external/ and built from source, so a fresh checkout needs only a C
# compiler, cmake, and X11/GL/Vulkan dev headers. See README.md.
#
# Host (your aarch64 or x86_64 Linux box):
#   make                      build libs, examples and tools (debug)
#   make CONFIG=release       optimized build
#   make run EX=toolbox       run an example (simulator if no OpenXR runtime)
#   make sim EX=toolbox       force the desktop simulator
#   make shot EX=toolbox      headless screenshot of the simulator (Xvfb)
#
# Steam Frame (Linux arm64 inside Valve's Steam Runtime 4.0 arm64 SDK container):
#   make frame                         build everything for the headset
#   make package EX=toolbox            stage dist/toolbox/ (binary + launch.sh + vrpreferences.json)
#   make frame-pair                    once: press "Pair devkit" on the headset, confirm the prompt
#   make frame-go EX=toolbox           build + upload + register in Steam + launch + follow log
#   make frame-shot EX=toolbox         both-eye screenshot pulled back to shots/frame/
#   make frame-status / frame-probe / frame-logs / frame-stop
#   (details: scripts/frame.sh, README "Steam Frame")
#
# New project:
#   make new-app NAME=mygame  -> apps/mygame/main.c from the hello template
#
# Tests (headless; docs/TESTING.md):
#   make test                 every C test suite (tests/<suite>/main.c), with red legs
#   make test T=mech          one suite (or T=mech/knob-orbit-quarter-turn)
#   make test-audit           every break switch is proven by a test
#   make regress              golden-image replays (tests/regress/)
#
# OpenXR without a headset (Monado simulated HMD, software rendering):
#   make monado-image         build the test container once
#   make test-xr EX=hello     run under Monado, save the compositor screenshot
#
#   make clean / make distclean

CONFIG     ?= debug
TARGET_TAG ?= host
BUILD      ?= build/$(TARGET_TAG)-$(CONFIG)
EX         ?= toolbox

CC         ?= cc
CMAKE      ?= cmake

RAYLIB_VER      := 6.0
RAYLIB_TARBALL  := external/raylib-$(RAYLIB_VER)-src.tar.gz
RAYLIB_DIR      := external/raylib-$(RAYLIB_VER)
RAYLIB_SRC      := $(RAYLIB_DIR)/src
RAYLIB_STAMP    := $(RAYLIB_DIR)/.extracted

OPENXR_VER      := 1.1.63
OPENXR_TARBALL  := external/OpenXR-SDK-release-$(OPENXR_VER).tar.gz
OPENXR_DIR      := external/OpenXR-SDK-release-$(OPENXR_VER)
OPENXR_STAMP    := $(OPENXR_DIR)/.extracted
OPENXR_LIB      := $(BUILD)/openxr/src/loader/libopenxr_loader.a

# raylib: desktop GLFW on X11 with OpenGL 3.3 (the Frame's Mesa stack provides
# GL through Zink/Freedreno; the XR runtime gets frames via sfxr).
RAYLIB_DEFINES  := -DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33 -D_GLFW_X11
RAYLIB_MODULES  := rcore rshapes rtextures rtext rmodels raudio rglfw
RAYLIB_OBJS     := $(patsubst %,$(BUILD)/raylib/%.o,$(RAYLIB_MODULES))
RAYLIB_LIB      := $(BUILD)/libraylib.a

ifeq ($(CONFIG),release)
  OPT := -O2 -DNDEBUG
else ifeq ($(CONFIG),test)
  OPT := -O1 -g -DSFXR_TESTING    # test build: break switches exist (sfxr_break.h)
else
  OPT := -O0 -g
endif

WARN     := -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
INCLUDES := -I$(RAYLIB_SRC) -I$(OPENXR_DIR)/include -Isfxr/include -Ivrui/include
CFLAGS   += -std=gnu11 $(OPT) $(WARN) $(INCLUDES)
LDLIBS   := $(OPENXR_LIB) -lstdc++ -lGL -lX11 -lm -lpthread -ldl -lrt

SFXR_SRCS := $(wildcard sfxr/src/*.c)
VRUI_SRCS := $(wildcard vrui/src/*.c)
SFXR_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(SFXR_SRCS))
VRUI_OBJS := $(patsubst %.c,$(BUILD)/obj/%.o,$(VRUI_SRCS))
SFXR_LIB  := $(BUILD)/libsfxr.a
VRUI_LIB  := $(BUILD)/libvrui.a

EXAMPLES  := $(notdir $(patsubst %/,%,$(dir $(wildcard examples/*/main.c apps/*/main.c))))
EX_BINS   := $(patsubst %,$(BUILD)/bin/%,$(EXAMPLES))
TOOLS     := xr_probe sfxrec_dump
TOOL_BINS := $(patsubst %,$(BUILD)/bin/%,$(TOOLS))

.PHONY: test test-bins test-audit
.PHONY: all libs examples tools run sim shot frame frame-shell package new-app fake-frame record replay regress regress-bless frame-record \
        frame-pair frame-status frame-probe frame-deploy frame-run frame-shot frame-logs frame-stop frame-go \
        monado-image test-xr clean distclean
.SECONDARY:

all: examples tools

libs: $(RAYLIB_LIB) $(OPENXR_LIB) $(SFXR_LIB) $(VRUI_LIB)
examples: $(EX_BINS)
tools: $(TOOL_BINS)

# --- vendored sources --------------------------------------------------------

# Our few fixes to raylib live in patches/ (see patches/README.md for why).
RAYLIB_PATCHES := $(sort $(wildcard patches/raylib-$(RAYLIB_VER)-*.patch))

$(RAYLIB_STAMP): $(RAYLIB_TARBALL) $(RAYLIB_PATCHES)
	rm -rf $(RAYLIB_DIR)
	tar -xzf $< -C external
	for p in $(RAYLIB_PATCHES); do echo "patching raylib: $$p"; patch -s -p1 -d $(RAYLIB_DIR) < $$p || exit 1; done
	touch $@

$(OPENXR_STAMP): $(OPENXR_TARBALL)
	tar -xzf $< -C external
	touch $@

$(BUILD)/raylib/%.o: $(RAYLIB_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 -O2 -w $(RAYLIB_DEFINES) -I$(RAYLIB_SRC) -I$(RAYLIB_SRC)/external/glfw/include \
	    -c $(RAYLIB_SRC)/$*.c -o $@

$(RAYLIB_LIB): $(RAYLIB_OBJS)
	ar rcs $@ $^

# Static OpenXR loader: nothing to ship next to the binary, and the loader
# finds the active runtime (SteamVR on the Frame) through the usual JSON.
$(OPENXR_LIB): $(OPENXR_STAMP)
	$(CMAKE) -S $(OPENXR_DIR) -B $(BUILD)/openxr -DCMAKE_BUILD_TYPE=Release \
	    -DDYNAMIC_LOADER=OFF -DBUILD_TESTS=OFF -DBUILD_API_LAYERS=OFF \
	    -DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_WITH_WAYLAND_HEADERS=OFF \
	    -DBUILD_WITH_XCB_HEADERS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON >/dev/null
	$(CMAKE) --build $(BUILD)/openxr --target openxr_loader --parallel $(shell nproc) >/dev/null
	@test -f $@ && echo "built $@"

# --- our libraries -------------------------------------------------------------

$(BUILD)/obj/%.o: %.c $(RAYLIB_STAMP) $(OPENXR_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Archives are rebuilt from scratch: `ar r` never drops members, so a renamed
# or deleted source file would otherwise linger in the library.
$(SFXR_LIB): $(SFXR_OBJS)
	rm -f $@ && ar rcs $@ $^

$(VRUI_LIB): $(VRUI_OBJS)
	rm -f $@ && ar rcs $@ $^

-include $(SFXR_OBJS:.o=.d) $(VRUI_OBJS:.o=.d)

# --- examples & tools ----------------------------------------------------------

# Every examples/<name>/ and apps/<name>/ with a main.c becomes bin/<name>
# (all .c files in that folder are compiled together).
$(BUILD)/bin/%: examples/%/*.c $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(OPENXR_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iexamples/$* -o $@ $(filter %.c,$^) $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(LDLIBS)

$(BUILD)/bin/%: apps/%/*.c $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(OPENXR_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iapps/$* -o $@ $(filter %.c,$^) $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(LDLIBS)

$(BUILD)/bin/sfxrec_dump: tools/sfxrec_dump.c $(RAYLIB_STAMP) $(OPENXR_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DRAYMATH_STATIC_INLINE -Isfxr/src -o $@ tools/sfxrec_dump.c -lm

$(BUILD)/bin/xr_probe: tools/xr_probe.c $(OPENXR_LIB) $(OPENXR_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ tools/xr_probe.c $(OPENXR_LIB) -lstdc++ -lm -lpthread -ldl

# --- tests (built with CONFIG=test into build/host-test) --------------------------

TEST_SUITES := $(notdir $(patsubst %/,%,$(dir $(wildcard tests/*/main.c))))
TEST_BINS   := $(patsubst %,$(BUILD)/tests/%,$(TEST_SUITES))
SFXT_OBJ    := $(BUILD)/obj/sfxt/src/sfxt.o

$(BUILD)/obj/sfxt/%.o: CFLAGS += -Isfxt/include -Isfxr/src

$(BUILD)/tests/%: tests/%/*.c $(SFXT_OBJ) $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(OPENXR_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Isfxt/include -o $@ $(filter %.c,$^) $(SFXT_OBJ) $(VRUI_LIB) $(SFXR_LIB) $(RAYLIB_LIB) $(LDLIBS)

-include $(SFXT_OBJ:.o=.d)

test-bins: $(TEST_BINS)

test:
	$(MAKE) CONFIG=test test-bins
	BUILD=build/host-test scripts/test.sh $(T)

test-audit:
	$(MAKE) CONFIG=test test-bins
	BUILD=build/host-test scripts/test.sh --audit

# --- run on this machine -------------------------------------------------------

run: $(BUILD)/bin/$(EX)
	$(BUILD)/bin/$(EX)

sim: $(BUILD)/bin/$(EX)
	SFXR_BACKEND=sim $(BUILD)/bin/$(EX)

shot: $(BUILD)/bin/$(EX)
	scripts/shot-sim.sh $(BUILD)/bin/$(EX) shots/$(EX)-sim.png $(SHOT_ACTIONS)

# --- Steam Frame -------------------------------------------------------------

# Valve's devkit client offers "Steam Linux Runtime 4.0 ARM64" for native Frame
# titles (compat tool SteamLinuxRuntime_4-arm64), so build in the matching SDK.
# For the 3.0/sniper runtime instead: make frame FRAME_SDK=.../sniper/sdk/arm64:latest
FRAME_SDK ?= registry.gitlab.steamos.cloud/steamrt/steamrt4/sdk/arm64:latest

frame:
	scripts/frame-build.sh $(FRAME_SDK) all

frame-shell:
	scripts/frame-build.sh $(FRAME_SDK) shell

package:
	scripts/package.sh $(EX) build/frame-release

# --- Record / replay regression tests (see scripts/regress.sh) ------------------
# Record a simulator session into tests/regress/$(NAME)/ (close the window to stop):
record: $(BUILD)/bin/$(EX)
	@test -n "$(NAME)" || { echo "usage: make record EX=toolbox NAME=my-test"; exit 1; }
	mkdir -p tests/regress/$(NAME)
	echo $(EX) > tests/regress/$(NAME)/app
	SFXR_BACKEND=sim SFXR_RECORD=tests/regress/$(NAME)/input.sfxrec $(BUILD)/bin/$(EX)
	@echo "recorded tests/regress/$(NAME) -- now: make regress-bless T=$(NAME)"
# Replay a recording with a visible window (watch what the test does):
replay: $(BUILD)/bin/$(EX)
	SFXR_REPLAY=tests/regress/$(T)/input.sfxrec SFXR_MIRROR=1 $(BUILD)/bin/$$(cat tests/regress/$(T)/app)
regress: all
	scripts/regress.sh $(T)
regress-bless: all
	scripts/regress.sh --bless $(T)

# --- Steam Frame over SSH (see scripts/frame.sh; target stored in .frame-host) ---
frame-pair:
	scripts/frame.sh pair $(FRAME_HOST)
frame-status:
	scripts/frame.sh status
frame-probe:
	scripts/frame.sh probe
frame-deploy:
	scripts/frame.sh deploy $(EX)
frame-run:
	scripts/frame.sh run $(EX) $(SECS)
frame-shot:
	scripts/frame.sh shot $(EX) $(SHOT_FRAME)
frame-logs:
	scripts/frame.sh logs $(EX)
frame-stop:
	scripts/frame.sh stop $(EX)
frame-record:
	scripts/frame.sh record $(EX) $(NAME) $(SECS)
# One command for the edit/test loop: build in the SDK, upload, launch, follow the log.
frame-go: frame
	scripts/frame.sh deploy $(EX)
	scripts/frame.sh run $(EX) $(SECS)

# Rehearsal target: a container that behaves like a Frame in Developer Mode
# (pairing service, Steam devkit IPC, Monado as the VR runtime).
fake-frame:
	docker build -q -t sfq-fake-frame -f docker/fake-frame.Dockerfile docker >/dev/null
	-docker rm -f sfq-fake-frame >/dev/null 2>&1
	docker run -d --name sfq-fake-frame sfq-fake-frame >/dev/null
	@IP=$$(docker inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' sfq-fake-frame); \
	  echo "fake Frame running at $$IP. Pair with it like a real headset:"; \
	  echo "  export FRAME_KEY=~/.ssh/sfq_fakeframe FRAME_HOSTFILE=.frame-host-fake"; \
	  echo "  scripts/frame.sh pair $$IP     # then deploy/run/shot/logs as usual"

new-app:
	scripts/new-app.sh $(NAME)

# --- OpenXR test runtime (Monado) -----------------------------------------------

monado-image:
	docker build -t sfq-monado -f docker/monado.Dockerfile docker

test-xr: $(BUILD)/bin/$(EX)
	scripts/test-xr.sh $(BUILD)/bin/$(EX) shots/$(EX)-monado-$(or $(BACKEND),auto).png $(or $(BACKEND),auto)

# --- housekeeping ---------------------------------------------------------------

clean:
	rm -rf build shots dist

distclean: clean
	rm -rf $(RAYLIB_DIR) $(OPENXR_DIR)

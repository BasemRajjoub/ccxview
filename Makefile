# ccxview -- Linux / macOS build. Windows: build.bat
#   make            standalone build -> build/ccxview (Linux: launcher + bin/ + lib/)
#   make PORTABLE=1 same, runs on glibc >= 2.34 (see src/glibc_old.h)
#   scripts/pack-binaries.sh   the release archives in dist/ (what CI attaches to a release)
#   make bundle-mesa   + Mesa llvmpipe in build/lib/mesa (software renderer, large)
#   scripts/build-portable.sh   the same inside a glibc-2.17 container (podman/docker)
#   make win        Windows cross build, zig cc if zig is on PATH else mingw-w64 (ZIG= forces mingw; CI uses it) -> build/win/ccxview.exe (static runtime,
#                   no console window; WIN_CONSOLE=1 for one)
#   make wasm       browser build (Emscripten) -> build/web/ccxview.html
#   make test       headless unit tests
#   make bench      headless timing tool
#   make gen        synthetic .frd generator
#   make corpus     parse every .frd under $(CCX_EXAMPLES)
#   make fuzz       byte-flip the sample files through every reader (never crash)
#   make samples    test files into build/samples (showcase + elements + examples + synthetic 1M/5M)
#   scripts/solve_showcase.sh [showcase|elements]   regenerate + solve a sample deck with ccx
#
# Every source compiles to its own object under build/obj/<variant>/ (with a .d file
# listing the headers it includes), so a change rebuilds only what it touches, and
# make -j compiles in parallel. CI wraps the compilers in ccache (.github/workflows/).

CC      ?= cc
# OPT is the optimisation for every build; RELEASE=1 (the shipped binaries, implied by
# PORTABLE=1, win and wasm) adds link-time optimisation. No -march: the binaries travel.
OPT     ?= -O3 -fno-math-errno
RELEASE ?= $(PORTABLE)
LTO      = $(if $(filter 1,$(RELEASE)),$(if $(filter Darwin,$(UNAME)),-flto,-flto=auto -Wno-maybe-uninitialized),)   # LTO sees across files and guesses wrong about init
CFLAGS  ?= $(OPT) -g $(LTO)
CFLAGS  += -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-format-truncation
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -DSOKOL_GLCORE -Ivendor
# PORTABLE=1: ask for old glibc symbol versions, so the binary runs on glibc >= 2.34
PORTABLE ?= 0
ifeq ($(PORTABLE),1)
  CPPFLAGS += -include src/glibc_old.h
endif

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
  GUI_LIBS = -framework Cocoa -framework QuartzCore -framework OpenGL
  IMPL_FLAGS = -x objective-c
  CORE_EXTRA = vendor/tinyfiledialogs.c
else
  GUI_CFLAGS := $(shell pkg-config --cflags x11 xi xcursor gl 2>/dev/null)
  GUI_LIBS   := $(shell pkg-config --libs x11 xi xcursor gl 2>/dev/null || echo -lX11 -lXi -lXcursor -lGL) -ldl -lpthread -lm
endif

CORE = src/frd.c src/mesh.c src/field.c src/calc.c src/os.c src/filedlg.c src/dat.c src/gauss.c src/inp.c src/inp_localsys.c src/fbd.c src/cgx.c src/sta.c src/log.c src/cfg.c src/export.c src/path.c src/video.c src/video_h264.c src/video_mp4.c
APP  = src/app.c src/app_field.c src/app_overlay.c src/app_path.c src/app_linearize.c src/app_cam.c src/app_load.c src/app_settings.c src/app_gauss.c src/app_deck.c src/app_fbd.c src/render.c src/ui.c src/ui_style.c src/ui_panels.c src/ui_view.c src/ui_bars.c src/ui_windows.c src/ui_plots.c src/font_data.c src/gpu.c
CCX_EXAMPLES ?= $(HOME)/CalculiX-Examples

# the release version (VERSION; bumping it on master makes a release, see .github/workflows),
# given only to app.c so a bump recompiles that one file
VERSION_STR := $(shell cat VERSION 2>/dev/null)
VERSION_DEF =
$(addsuffix /src/app.o,build/obj/dev build/obj/release build/obj/portable build/win/obj build/web/obj): VERSION
$(addsuffix /src/app.o,build/obj/dev build/obj/release build/obj/portable build/win/obj build/web/obj): VERSION_DEF = -DCV_VERSION_NUM=$(VERSION_STR)

ifeq ($(UNAME),Darwin)
all: build/ccxview
else
all: build/bin/ccxview
	scripts/bundle.sh build/bin/ccxview
bundle-mesa: build/bin/ccxview
	scripts/bundle.sh build/bin/ccxview --mesa
endif

# one object directory per set of flags, so switching PORTABLE or RELEASE never mixes them
OBJ = build/obj/$(if $(filter 1,$(PORTABLE)),portable,$(if $(filter 1,$(RELEASE)),release,dev))
NATIVE_OBJ = $(patsubst %.c,$(OBJ)/%.o,$(CORE) $(CORE_EXTRA) $(APP))
DEPS = -MMD -MP -MF $(@:.o=.d) -MT $@

$(OBJ)/src/sokol_impl.o: src/sokol_impl.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) $(OPT) $(LTO) $(DEPS) -std=c99 -w $(IMPL_FLAGS) -c $< -o $@
$(OBJ)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(VERSION_DEF) $(GUI_CFLAGS) $(CFLAGS) $(DEPS) -c $< -o $@

build/ccxview build/bin/ccxview: $(NATIVE_OBJ) $(OBJ)/src/sokol_impl.o
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ -static-libgcc $(GUI_LIBS) -lm

# the vendored MP4 muxer reads unaligned words on purpose: it is compiled
# without the sanitizers, everything of ours with them
build/video_mp4_nosan.o: src/video_mp4.c src/video_impl.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(OPT) -std=c99 -w -c $< -o $@
TEST_OBJ = $(patsubst %.c,build/obj/test/%.o,tests/test_main.c $(filter-out src/video_mp4.c,$(CORE)) src/gpu.c)
build/obj/test/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCV_DEBUG -fsanitize=address,undefined $(DEPS) -c $< -o $@
build/test_main: $(TEST_OBJ) build/video_mp4_nosan.o
	$(CC) $(CFLAGS) -fsanitize=address,undefined $^ -o $@ -lm -lpthread -ldl

test: build/test_main
	./build/test_main
	scripts/check-sources.sh
	sh tests/launcher.sh

build/bench: $(patsubst %.c,$(OBJ)/%.o,tests/bench.c $(CORE))
	$(CC) $(CFLAGS) $^ -o $@ -lm -lpthread

build/gen_frd: tests/gen_frd.c tests/frd_write.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/gen_frd.c -o $@ -lm

bench: build/bench
gen: build/gen_frd

corpus: build/bench
	find "$(CCX_EXAMPLES)" -name '*.frd' -print0 | xargs -0 -n1 ./build/bench --quiet

# byte-flip every sample file 300 times each; the readers must never crash
fuzz: build/bench
	for f in samples/showcase/showcase.frd samples/showcase/showcase.inp samples/showcase/showcase.dat \
	         samples/showcase/showcase.sta samples/showcase/showcase.cvg samples/elements/elements.frd \
	         samples/elements/elements.inp samples/elements/elements.dat; do ./build/bench --fuzz 300 "$$f" || exit 1; done

SAMPLES = Kasten/Kasten.frd NonLinear/3PB/Biegung.frd Contact/Eyebar/eyebar.frd \
          Elements/Shell/shell.frd Linear/Plates/plates.frd Thermal/Thermografie/Naht.frd \
          Streifen/b.frd Test/BeamSections/b32.frd

samples: build/gen_frd
	@mkdir -p build/samples
	cp samples/showcase/showcase.inp samples/showcase/showcase.frd samples/showcase/showcase.dat \
	   samples/showcase/showcase.sta samples/showcase/showcase.cvg samples/elements/elements.inp \
	   samples/elements/elements.frd samples/elements/elements.dat samples/elements/elements.sta build/samples/
	-for f in $(SAMPLES); do cp "$(CCX_EXAMPLES)/$$f" "build/samples/$$(echo $$f | tr / _)" 2>/dev/null; done
	[ -e build/samples/synthetic_1M_ascii.frd ] || ./build/gen_frd 1000000 build/samples/synthetic_1M_ascii.frd
	[ -e build/samples/synthetic_5M_binary.frd ] || ./build/gen_frd 5000000 build/samples/synthetic_5M_binary.frd --binary

# ---- Windows cross build. Either mingw-w64 (MINGW names the compiler; MINGW_LDFLAGS
# adds e.g. -L<dir of libmcfgthread.a> on toolchains that need it) or, with ZIG=zig
# (or a path to it), zig cc + zig rc: one self-contained toolchain, nothing to install
# on NixOS beyond the zig package.
MINGW ?= x86_64-w64-mingw32-gcc
WINDRES ?= $(MINGW:gcc=windres)
MINGW_LDFLAGS ?=
ZIG ?= $(shell command -v zig 2>/dev/null)
# GUI subsystem: no cmd window behind the viewer. WIN_CONSOLE=1 keeps one (debugging).
# debug info: kept apart for the release (pack-binaries.sh), read by scripts/symbolize.sh.
# Old mingw gcc (8.x) dies on -g with LTO; WIN_G=-g1 (lines only) or WIN_G= there.
WIN_G ?= -g
WIN_SUBSYS = $(if $(filter 1,$(WIN_CONSOLE)),-mconsole,-mwindows)
WIN_SRC = $(CORE) $(APP) vendor/tinyfiledialogs.c
ifneq ($(ZIG),)
WIN_CC = $(ZIG) cc --target=x86_64-windows-gnu
WIN_LTO =                                   # zig's lld cannot LTO against the mingw C runtime
WIN_ICON = build/win/icon.res
WIN_DEFS =
WIN_LINK = $(if $(filter 1,$(WIN_CONSOLE)),-Wl$(,)--subsystem$(,)console,-Wl$(,)--subsystem$(,)windows)
else
WIN_CC = $(MINGW)
WIN_LTO = -flto
WIN_ICON = build/win/icon.o
WIN_DEFS = -D__USE_MINGW_ANSI_STDIO=1                # printf with %zu on the old msvcrt too
WIN_LINK = $(WIN_SUBSYS) $(MINGW_LDFLAGS) -static -static-libgcc
endif
, := ,
# zig cc caches every compile by content (ZIG_GLOBAL_CACHE_DIR), but not when asked for
# dependency files: so with zig each object depends on every header and zig skips what
# did not change; mingw writes .d files like the native build
WIN_CFLAGS = $(OPT) $(WIN_G) $(WIN_LTO) -std=c99 $(WIN_DEFS) -DSOKOL_GLCORE -Ivendor $(if $(ZIG),,$(DEPS))
WIN_HDRS = $(if $(ZIG),$(wildcard src/*.h vendor/*.h) vendor/miniz.c,)
WIN_OBJ = $(patsubst %.c,build/win/obj/%.o,$(WIN_SRC))
win: build/win/ccxview.exe
build/win/obj/src/sokol_impl.o: src/sokol_impl.c $(WIN_HDRS)
	@mkdir -p $(dir $@)
	$(WIN_CC) $(WIN_CFLAGS) -w -c $< -o $@
build/win/obj/%.o: %.c $(WIN_HDRS)
	@mkdir -p $(dir $@)
	$(WIN_CC) $(WIN_CFLAGS) -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	    -Wno-format-truncation -Wno-cast-function-type -Wno-typedef-redefinition $(VERSION_DEF) -c $< -o $@
build/win/icon.o: res/ccxview.rc res/ccxview.ico
	@mkdir -p build/win
	$(WINDRES) $< -O coff -o $@
build/win/icon.res: res/ccxview.rc res/ccxview.ico
	@mkdir -p build/win
	$(ZIG) rc /fo $@ $<
build/win/ccxview.exe: $(WIN_OBJ) build/win/obj/src/sokol_impl.o $(WIN_ICON)
	$(WIN_CC) $(OPT) $(WIN_G) $(WIN_LTO) $^ -o $@ $(WIN_LINK) \
	    -lkernel32 -luser32 -lgdi32 -lshell32 -lopengl32 -lcomdlg32 -lole32
	@echo "built $@ (test with: wine $@ model.frd)"

clean:
	rm -rf build

.PHONY: all test bench gen corpus samples clean bundle-mesa win fuzz

# ---- browser build (Emscripten). One self-contained HTML file with the showcase
# model inside; CI publishes it as the GitHub Pages site. No threads (plain hosting
# has no SharedArrayBuffer), so a model loads on the main thread.
EMCC ?= emcc
WASM_SRC = $(CORE) $(APP) src/web.c src/sokol_impl.c
WASM_EMBED = $(foreach f,frd dat inp sta cvg,--embed-file samples/showcase/showcase.$(f)@/showcase.$(f))
WASM_OBJ = $(patsubst %.c,build/web/obj/%.o,$(WASM_SRC))
wasm: build/web/ccxview.html
build/web/obj/%.o: %.c
	@mkdir -p $(dir $@)
	$(EMCC) -O3 -flto -std=gnu99 -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Wno-macro-redefined \
	    -Wno-typedef-redefinition -DSOKOL_GLES3 -Ivendor $(VERSION_DEF) $(DEPS) -c $< -o $@
build/web/ccxview.html: $(WASM_OBJ) web/shell.html samples/showcase/showcase.frd
	$(EMCC) -O3 -flto $(WASM_OBJ) -o $@ --shell-file web/shell.html $(WASM_EMBED) \
	    -sSINGLE_FILE=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFORCE_FILESYSTEM=1 \
	    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB \
	    -sEXPORTED_FUNCTIONS=_main,_cv_web_unload -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToUTF8 \
	    -lGL

# the headers each object was built from (written by -MMD)
-include $(NATIVE_OBJ:.o=.d) $(OBJ)/src/sokol_impl.d $(OBJ)/tests/bench.d $(WIN_OBJ:.o=.d) $(WASM_OBJ:.o=.d) $(TEST_OBJ:.o=.d)

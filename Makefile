# ccxview -- Linux / macOS build. Windows: build.bat
#   make            standalone build -> build/ccxview (Linux: launcher + bin/ + lib/)
#   make PORTABLE=1 same, runs on glibc >= 2.34 (see src/glibc_old.h)
#   scripts/pack-binaries.sh   copy the Linux and Windows builds into binaries/
#   make bundle-mesa   + Mesa llvmpipe in build/lib/mesa (software renderer, large)
#   scripts/build-portable.sh   the same inside a glibc-2.17 container (podman/docker)
#   make win        Windows cross build with mingw-w64 -> build/win/ccxview.exe (static runtime)
#   make test       headless unit tests
#   make bench      headless timing tool
#   make gen        synthetic .frd generator
#   make corpus     parse every .frd under $(CCX_EXAMPLES)
#   make fuzz       byte-flip the sample files through every reader (never crash)
#   make samples    test files into build/samples (showcase + examples + synthetic 1M/5M)
#   scripts/solve_showcase.sh   regenerate + solve samples/showcase with ccx

CC      ?= cc
CFLAGS  ?= -O2 -g
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

CORE = src/frd.c src/mesh.c src/field.c src/os.c src/filedlg.c src/dat.c src/gauss.c src/inp.c src/fbd.c src/cgx.c src/sta.c src/log.c src/cfg.c src/export.c src/path.c src/video.c src/video_h264.c src/video_mp4.c
APP  = src/app.c src/app_field.c src/app_cam.c src/app_load.c src/app_settings.c src/app_gauss.c src/app_deck.c src/app_fbd.c src/render.c src/ui.c src/gpu.c
CCX_EXAMPLES ?= $(HOME)/CalculiX-Examples

ifeq ($(UNAME),Darwin)
all: build/ccxview
else
all: build/bin/ccxview
	scripts/bundle.sh build/bin/ccxview
bundle-mesa: build/bin/ccxview
	scripts/bundle.sh build/bin/ccxview --mesa
endif

build/sokol_impl.o: src/sokol_impl.c
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) -O2 -std=c99 -w $(IMPL_FLAGS) -c $< -o $@

build/ccxview build/bin/ccxview: $(CORE) $(APP) build/sokol_impl.o src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(GUI_CFLAGS) $(CFLAGS) $(CORE) $(CORE_EXTRA) $(APP) build/sokol_impl.o -o $@ \
	    -static-libgcc $(GUI_LIBS) -lm

# the vendored MP4 muxer reads unaligned words on purpose: it is compiled
# without the sanitizers, everything of ours with them
build/video_mp4_nosan.o: src/video_mp4.c src/video_impl.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) -O2 -std=c99 -w -c $< -o $@
build/test_main: tests/test_main.c $(filter-out src/video_mp4.c,$(CORE)) build/video_mp4_nosan.o src/*.h tests/*.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -DCV_DEBUG -fsanitize=address,undefined tests/test_main.c $(filter-out src/video_mp4.c,$(CORE)) \
	    build/video_mp4_nosan.o src/gpu.c -o $@ -lm -lpthread -ldl

test: build/test_main
	./build/test_main
	scripts/check-sources.sh
	sh tests/launcher.sh

build/bench: tests/bench.c $(CORE) src/*.h
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/bench.c $(CORE) -o $@ -lm -lpthread

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
	         samples/showcase/showcase.sta samples/showcase/showcase.cvg; do ./build/bench --fuzz 300 "$$f" || exit 1; done

SAMPLES = Kasten/Kasten.frd NonLinear/3PB/Biegung.frd Contact/Eyebar/eyebar.frd \
          Elements/Shell/shell.frd Linear/Plates/plates.frd Thermal/Thermografie/Naht.frd \
          Streifen/b.frd Test/BeamSections/b32.frd

samples: build/gen_frd
	@mkdir -p build/samples
	cp samples/showcase/showcase.inp samples/showcase/showcase.frd samples/showcase/showcase.dat \
	   samples/showcase/showcase.sta samples/showcase/showcase.cvg build/samples/
	-for f in $(SAMPLES); do cp "$(CCX_EXAMPLES)/$$f" "build/samples/$$(echo $$f | tr / _)" 2>/dev/null; done
	[ -e build/samples/synthetic_1M_ascii.frd ] || ./build/gen_frd 1000000 build/samples/synthetic_1M_ascii.frd
	[ -e build/samples/synthetic_5M_binary.frd ] || ./build/gen_frd 5000000 build/samples/synthetic_5M_binary.frd --binary

# ---- Windows cross build (mingw-w64). MINGW names the compiler; MINGW_LDFLAGS adds
# e.g. -L<dir of libmcfgthread.a> on toolchains that need it (nixpkgs).
MINGW ?= x86_64-w64-mingw32-gcc
MINGW_LDFLAGS ?=
WIN_SRC = $(CORE) $(APP) vendor/tinyfiledialogs.c
win: build/win/ccxview.exe
build/win/sokol_impl.o: src/sokol_impl.c
	@mkdir -p build/win
	$(MINGW) -O2 -std=c99 -w -DSOKOL_GLCORE -Ivendor -c $< -o $@
build/win/ccxview.exe: $(WIN_SRC) build/win/sokol_impl.o src/*.h
	$(MINGW) -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	    -Wno-format-truncation -Wno-cast-function-type -DSOKOL_GLCORE -Ivendor \
	    $(WIN_SRC) build/win/sokol_impl.o -o $@ $(MINGW_LDFLAGS) -static -static-libgcc \
	    -lkernel32 -luser32 -lgdi32 -lshell32 -lopengl32 -lcomdlg32 -lole32
	@echo "built $@ (test with: wine $@ model.frd)"

clean:
	rm -rf build

.PHONY: all test bench gen corpus samples clean bundle-mesa win fuzz

# ---- browser build (Emscripten). One self-contained HTML file with the showcase
# model inside; serve docs/ with GitHub Pages. No threads (plain hosting has no
# SharedArrayBuffer), so a model loads on the main thread.
EMCC ?= emcc
WASM_SRC = $(CORE) $(APP) src/web.c src/sokol_impl.c
WASM_EMBED = $(foreach f,frd dat inp sta cvg,--embed-file samples/showcase/showcase.$(f)@/showcase.$(f))
wasm: docs/index.html
docs/index.html: $(WASM_SRC) src/*.h web/shell.html samples/showcase/showcase.frd
	$(EMCC) -O2 -std=gnu99 -Wall -Wno-unused-parameter -Wno-missing-field-initializers -Wno-macro-redefined -Wno-typedef-redefinition -DSOKOL_GLES3 -Ivendor \
	    $(WASM_SRC) -o $@ --shell-file web/shell.html $(WASM_EMBED) \
	    -sSINGLE_FILE=1 -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sFORCE_FILESYSTEM=1 \
	    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=8MB \
	    -sEXPORTED_FUNCTIONS=_main,_cv_web_unload -sEXPORTED_RUNTIME_METHODS=FS,UTF8ToString,stringToUTF8 \
	    -lGL

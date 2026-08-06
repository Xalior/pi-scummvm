#
# ScummVM as a bootable Raspberry Pi kernel image.
#
# One board per invocation (RAPI_BOARD, default rpi5). The board selects
# three things at once: the circle-stdlib world the kernel is compiled and
# linked against, the shim archive built against that world, and — through
# the world's Config.mk — the CPU tuning and RASPPI value. Each board's
# newlib and libc++ are built for its own architecture, so an image is
# never portable between boards.
#
#   make                     the default board's image
#   make RAPI_BOARD=rpi3     that board's image
#   make sources             print the ScummVM source files this build uses
#   make clean-board         drop just this board's build/<board>/ tree
#
# Everything this invocation generates lands under build/<board>/: the
# kernel's own objects, every ScummVM object, and the final
# .elf/.map/.lst/.img. The object names are shared across boards but their
# contents are not (different -mcpu, different -DRASPPI, different world),
# so a flat build directory would silently relink one board's objects into
# another board's kernel — a corrupt image with no build error. A directory
# per board keeps all three isolated: no `make clean` between boards, and
# three boards can build at the same time without touching each other.
#
# ScummVM's own source tree (../scummvm) is never written into: it is
# read-only input, and every object it produces is redirected here.
#
# Prerequisite: the board's world and shim archive exist —
# `make -C ../circle-libsdl2 deps`, or `make deps` at the repository root.
#

include ../mk/toolchain.mk

RAPI_BOARD ?= rpi5

include ../circle-libsdl2/circle-stdlib-$(RAPI_BOARD)/Config.mk

ROOT = ..
SHIM = $(ROOT)/circle-libsdl2
SVM  = $(ROOT)/scummvm

OBJDIR = build/$(RAPI_BOARD)

# Circle's own image names, which the Raspberry Pi firmware selects between
# from the model sections of config.txt: the Pi 3 world boots kernel8.img,
# the Pi 4 world kernel8-rpi4.img, the Pi 5 world kernel_2712.img.
IMAGE_rpi3 = kernel8
IMAGE_rpi4 = kernel8-rpi4
IMAGE_rpi5 = kernel_2712
IMAGE = $(IMAGE_$(RAPI_BOARD))
ifeq ($(IMAGE),)
$(error unknown RAPI_BOARD '$(RAPI_BOARD)' — expected rpi3, rpi4 or rpi5)
endif

TARGET = $(OBJDIR)/$(IMAGE)

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := image

# ---------------------------------------------------------------------------
# WHICH ENGINE THIS BUILD PLAYS
# ---------------------------------------------------------------------------
#
# SCUMM — the LucasArts engine. Monkey Island, Day of the Tentacle, Sam &
# Max, Indiana Jones, Maniac Mansion, Loom, Full Throttle, The Dig.
#
# ONE ENGINE, not all of them. ScummVM interprets a hundred different
# adventure games, each engine a separate body of code, and a build with all
# of them is several hundred megabytes of object files and a kernel image far
# past what a Raspberry Pi boots comfortably. Every engine added costs build
# time and image size, so they are added deliberately.
#
# STATIC_PLUGIN rather than DYNAMIC_PLUGIN because there is no dynamic
# loader on this machine: the engine is linked into the kernel image, and
# ScummVM's plugin machinery registers it from a table at startup (see
# svmgen/engines/plugins_table.h).
#
# SCUMM has two sub-engines of its own, and they are separate switches
# because they are separate bodies of code inside the same engine:
#
#   SCUMM_7_8   the version 7 and 8 games — Full Throttle, The Dig, Curse of
#               Monkey Island. ON. It brings the digital iMUSE sound engine
#               and the SMUSH video player with it, both of which are
#               ScummVM's own code and need nothing from outside.
#   HE          the Humongous Entertainment games — Putt-Putt, Freddi Fish,
#               Backyard Sports. OFF. It is a different family of games from
#               the LucasArts ones this build is for, and it depends on two
#               features this port does not have: high-resolution 16-bit
#               colour, and the Bink video decoder.
#
# Adding another engine is these lines, its directory in SVM_MODULES below,
# and its entry in the two tables in svmgen/engines/. Nothing else.
ENABLE_SCUMM     := STATIC_PLUGIN
ENABLE_SCUMM_7_8 := 1

# ---------------------------------------------------------------------------
# The feature selection, as ScummVM's own makefiles read it
# ---------------------------------------------------------------------------
#
# ScummVM's module.mk files decide which sources exist by testing make
# variables, the same ones its `configure` would have written into config.mk.
# Set here, in the same spellings, so upstream's own file lists answer for
# this build without being copied or edited.
#
# Every one of these must agree with the matching line in svmgen/config.h:
# the makefile variable decides whether a file is COMPILED, the C define
# decides what is INSIDE it, and a build where the two disagree either links
# a file full of nothing or fails to find a symbol whose source was never
# offered.
DETECTION_STATIC := 1
SDL_BACKEND      := 1
USE_SDL2         := 1
USE_SCALERS      := 1
USE_ASPECT       := 1

# The three sound components SCUMM declares in its own configure.engine, and
# which ScummVM's configure would switch on for any build with SCUMM in it.
# All three are ScummVM's own synthesisers, written in C++ and depending on
# nothing outside the tree, so there is no reason for this port not to have
# them and one very good reason to: without the FM Towns one the engine does
# not link at all, because SCUMM's Towns music driver calls straight into it.
#
#   USE_MIDI                 the MIDI machinery, including the Adlib
#                            synthesiser that plays these games' music on a
#                            machine with no MIDI hardware.
#   USE_FMTOWNS_PC98_AUDIO   the FM Towns and PC-98 sound chips, for the
#                            Japanese releases.
#   USE_SID_AUDIO            the Commodore 64 sound chip, for the C64
#                            Maniac Mansion and Zak McKracken.
#
# SCUMM declares a fourth, imgui, which is its in-game debugger interface.
# That one is left out: it needs the Dear ImGui library and a renderer
# backend written against it, and neither exists here.
USE_MIDI               := 1
USE_FMTOWNS_PC98_AUDIO := 1
USE_SID_AUDIO          := 1

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------

# The kernel wrapper, the syscall layer, this port's own backend and the SDL
# surfaces the shim does not implement. Each file says at its top what it is
# answering for.
HOST_SRCS = main.cpp kernel.cpp defaults.cpp circle_syscalls.cpp \
	circle_stubs.cpp sdl2_surface.cpp sdl2_timer.cpp scummvm_backend.cpp
HOST_OBJS = $(addprefix $(OBJDIR)/,$(HOST_SRCS:.cpp=.o))

# ScummVM's own source list, taken from ScummVM's own makefiles.
#
# Each module.mk states its module's sources in MODULE_OBJS and ends by
# including rules.mk, which builds the compile rules. LOAD_RULES_MK is that
# file's own switch for "list the sources but do not build any rules", and
# ScummVM sets it exactly this way when it wants a second reading of the
# same files. So the includes below cost nothing but the list, and this
# build's rules stay this build's own.
#
# srcdir is the name module.mk uses for the tree it belongs to.
srcdir := $(SVM)
LOAD_RULES_MK :=

# The shared modules, as Makefile.common lists them, minus four that build
# something other than the game: `test` is the unit-test runner, `devtools`
# the data-file authoring programs, `po` the translations (this build has no
# USE_TRANSLATION) and `doc` the manual.
SVM_MODULES = \
	base engines engines/scumm gui backends video image graphics audio math \
	common common/compression common/formats

# Each module is read, then its list is taken and frozen before the next one
# is read. Freezing is the whole point of the assignments being `:=`.
# ScummVM's own makefiles write `DETECT_OBJS += $$(MODULE)/detection.o`, and
# make keeps that as the TEXT `$$(MODULE)/detection.o` rather than as a path,
# expanding it wherever it is finally used. Read all the modules first and
# every detection object would come out named after the LAST module read —
# a file that does not exist, and a build that stops with nothing to say.
SVM_OBJS :=
SVM_DETECT_OBJS :=

$(foreach m,$(SVM_MODULES),$(eval MODULE :=)$(eval MODULE_OBJS :=)$(eval DETECT_OBJS :=)$(eval include $(SVM)/$(m)/module.mk)$(eval SVM_OBJS := $(SVM_OBJS) $(addprefix $(MODULE)/,$(MODULE_OBJS)))$(eval SVM_DETECT_OBJS := $(SVM_DETECT_OBJS) $(DETECT_OBJS)))

# The two files backends/platform/sdl/module.mk contributes. That file is
# the one module.mk in the tree that does not use rules.mk — it appends to
# OBJS itself — so reading it the way the others are read would either miss
# its sources or drag in a platform's worth of them. The rest of what it
# offers is per-operating-system startup (posix/, win32/, macosx/ and so
# on), and this port's own is scummvm_backend.cpp beside this makefile.
SVM_OBJS += backends/platform/sdl/sdl.o backends/platform/sdl/sdl-window.o

# The POSIX filesystem, which is how ScummVM reaches the SD card. Named here
# rather than by setting the POSIX makefile variable, because that variable
# would also ask for GTK dialogs, a Unity taskbar and a dlopen plugin
# provider, none of which exist here. The C define POSIX is set in DEFINE
# below, which is what these three files actually test.
SVM_OBJS += \
	backends/fs/posix/posix-fs.o \
	backends/fs/posix/posix-fs-factory.o \
	backends/fs/posix/posix-iostream.o

# Game detection. With DETECTION_STATIC the detection code is linked into the
# executable instead of loaded as a plugin, and each engine's detection.cpp
# is named by its own module.mk in DETECT_OBJS rather than in MODULE_OBJS.
SVM_OBJS += $(SVM_DETECT_OBJS)

# The sources behind those objects. A handful of ScummVM's files are C rather
# than C++, so each object asks the filesystem which of the two it came from
# instead of assuming.
SVM_SRCS = $(foreach o,$(SVM_OBJS),$(if $(wildcard $(SVM)/$(o:.o=.cpp)),$(o:.o=.cpp),$(o:.o=.c)))

SVM_CXX_OBJS = $(addprefix $(OBJDIR)/svm/,$(patsubst %.cpp,%.o,$(filter %.cpp,$(SVM_SRCS))))
SVM_C_OBJS   = $(addprefix $(OBJDIR)/svm/,$(patsubst %.c,%.o,$(filter %.c,$(SVM_SRCS))))

OBJS = $(HOST_OBJS) $(SVM_CXX_OBJS) $(SVM_C_OBJS)

# ScummVM is a C++11 project and says so; gnu++17 is what the rest of this
# family builds at and what the shim's headers expect.
STANDARD = -std=gnu++17

# Stated, not inherited: this is Circle's own default, written down so the
# build's optimization level is a fact in the file rather than a forensic
# reconstruction.
OPTIMIZE = -O2

include $(CIRCLEHOME)/Rules.mk

# The world must be the multicore one circle-libsdl2 configures: the shim's
# core-split machinery cannot exist without it, and an object compiled
# against a single-core world links without complaint and then misbehaves.
# Every object here inherits the flag through Rules.mk's CPPFLAGS, so this
# check covers the whole build.
ifeq ($(findstring ARM_ALLOW_MULTI_CORE,$(DEFINE)),)
$(error the payload requires the multicore circle-stdlib world — build it with 'make -C ../circle-libsdl2 deps')
endif

# WHERE THIS GAME LIVES ON THE CARD. One card carries several games, and two
# of them writing a `scummvm.ini` into the FAT root would each silently
# overwrite the other's. Every game therefore keeps to a directory of its
# own, and nothing of this port's ever touches the root.
#
# It is a build-time constant, not a boot switch: a card's layout is decided
# when the card is staged, and a game that could be relocated at boot would
# only mean two places to look for the same file.
#
# The path is used ONLY by this port's own host layer — the working
# directory, the configuration file's name, and the search path ScummVM is
# given for its own data. Upstream ScummVM is never told about it and needs
# no patch: it looks where it is pointed.
RAPI_GAME_DIR ?= /games/scummvm
DEFINE += -DRAPI_GAME_DIR='"$(RAPI_GAME_DIR)"'

# What ScummVM's own build would have put in DEFINES.
#
#   SDL_BACKEND    the backend family this port belongs to. Every SDL-derived
#                  platform sets it, and it is what selects the SDL event,
#                  graphics, mixer, mutex and timer managers.
#   USE_SDL2       SDL 2 rather than SDL 1.2 or SDL 3. circle-libsdl2
#                  implements the SDL 2 API.
#   POSIX          this world is newlib with Circle's glue underneath:
#                  open, read, stat and opendir all exist, which is what
#                  ScummVM's POSIX filesystem asks for. Set as a C define
#                  only — see the note on the filesystem sources above.
#   ENABLE_SCUMM   the one engine, linked statically, and
#   ENABLE_SCUMM_7_8
#                  its version 7 and 8 sub-engine.
#   DATA_PATH      where ScummVM looks for the files it ships itself, the
#                  engine data and the theme. The game's own directory, so
#                  the whole of ScummVM's world is under one path.
#   DISABLE_TEXT_CONSOLE
#                  there is no terminal to fall back on, so ScummVM's
#                  debugger console is drawn into the game window instead.
#   SYSTEM_NOT_SUPPORTING_D_TYPE
#                  upstream's own switch for a directory entry that does not
#                  carry the file's type. Newlib's does not — it is not part
#                  of POSIX — so ScummVM asks the filesystem with stat()
#                  instead, which is the fallback that switch selects.
DEFINE += -DSDL_BACKEND -DUSE_SDL2 -DPOSIX
DEFINE += -DSYSTEM_NOT_SUPPORTING_D_TYPE
DEFINE += -DENABLE_SCUMM=$(ENABLE_SCUMM) -DENABLE_SCUMM_7_8
DEFINE += -DDATA_PATH='"$(RAPI_GAME_DIR)"'
DEFINE += -DDISABLE_TEXT_CONSOLE

# ScummVM's sources include their own headers by path from the tree root
# ("common/scummsys.h"), and SDL's by the spelling a desktop sdl2-config
# include path provides ("SDL.h"), so the shim's SDL2/ directory goes on the
# path in its own right alongside the SDL2/-prefixed one this project's own
# code uses.
#
# svmgen/ comes FIRST and carries the three files ScummVM's `configure`
# generates — config.h and the two plugin tables — so that this build's
# answers are found before anything upstream might leave in the tree.
# The engines directory is on the path in its own right because that is
# where an engine's own headers are reached from: engines/sky/sky.cpp writes
# `#include "sky/text.h"`, which only resolves with engines/ as a root.
# ScummVM's own top-level makefile does the same.
INCLUDE := -I $(CURDIR)/svmgen \
	-I $(SVM) -I $(SVM)/engines \
	-I $(SHIM)/include -I $(SHIM)/include/SDL2 \
	$(CIRCLE_STDLIB_INCLUDES) $(INCLUDE)

# ScummVM's own compiler selection for its C++ sources. Both the run-time
# type information and the exceptions are LEFT ON, and neither is optional:
# ScummVM's plugin machinery finds an engine by dynamic_cast, and the C++
# runtime under this port throws — a payload built without exceptions cannot
# unwind through the library.
SVM_CXXFLAGS = -Wno-multichar -Wno-unused-parameter

# The shim comes in whole, every member, rather than only the members the
# link happens to need. An object file linked straight into the kernel beats
# an archive member of the same name and does it silently, so a stub in
# circle_stubs.cpp left behind after the library implemented the real thing
# would quietly win — the game would keep calling the stub and nothing would
# say so. Pulling the archive in whole turns that into a duplicate-symbol
# error at link time, which is the only moment it can still be cheap to fix.
LIBS = --whole-archive $(SHIM)/libSDL2-$(RAPI_BOARD).a --no-whole-archive \
	$(CIRCLE_STDLIB_LIBS)

# ---------------------------------------------------------------------------
# Filesystem syscalls, redirected at link time
# ---------------------------------------------------------------------------
#
# The game runs on the application core, and newlib's Circle glue drives
# FatFs and the SD card straight from wherever it is called — which off core
# 0 is illegal. circle_syscalls.cpp puts the shim's any-core I/O service
# underneath the C library instead; these flags are what put it there.
#
# --wrap rather than redefinition, because redefinition cannot work here:
# the glue defines _open, _read, _write, _close, _lseek, _fstat, opendir,
# readdir, closedir, dup and more in a SINGLE object file. Redefining part
# of that set either collides at link time or, worse, leaves the originals
# linked in beside the replacements with no diagnostic. --wrap renames the
# references instead and leaves the vendored glue untouched — and it is what
# keeps __real_* reachable, which is how the service performs the real work
# once it has reached core 0.
#
# Every name here must also appear in circle_syscalls.cpp: a wrap with no
# __wrap_ definition is an undefined reference, so the link is the check.
WRAPPED_SYSCALLS = \
	_open _close _read _write _lseek _fstat _stat lstat _unlink _rename \
	_fcntl mkdir chdir getcwd dup dup2 \
	opendir readdir closedir rewinddir

# ---------------------------------------------------------------------------
# SDL functions the shim owns and this port extends
# ---------------------------------------------------------------------------
#
# circle-libsdl2 implements SDL_CreateRGBSurface and SDL_FreeSurface for
# 32-bit surfaces and refuses anything else. ScummVM's game screen, its
# overlay and its mouse cursor are 8-bit paletted, so this port adds those
# depths — and adding them means intercepting the pair, because a surface
# with a palette cannot be freed by a function that knows nothing about
# palettes. sdl2_surface.cpp holds both.
#
# The four texture calls are the same arrangement one level up. ScummVM's
# SDL 2 screen is RGB565 and it asks for a streaming texture in that format;
# the shim's textures are ARGB8888 and nothing else. So a texture the shim
# made is remembered here with the format its owner believes it has, and the
# conversion happens on the way in. See sdl2_surface.cpp.
#
# SDL_PollEvent and SDL_Delay are wrapped for a third reason: they are where
# this port's SDL_AddTimer timers are serviced. See sdl2_timer.cpp.
#
# --wrap for the same reason as the syscalls above: it leaves the library's
# implementations in place and reachable as __real_*, so the wrapper handles
# the new case and hands every other one straight back. A plain redefinition
# would collide with the archive, which is linked whole precisely so that it
# does.
WRAPPED_SDL = SDL_CreateRGBSurface SDL_FreeSurface SDL_PollEvent SDL_Delay \
	SDL_CreateTexture SDL_UpdateTexture SDL_QueryTexture SDL_DestroyTexture

# The link is driven by the cross ld directly (Circle's Rules.mk sets
# LD = <prefix>ld for GCC builds), so these are plain linker options, not
# compiler-driver -Wl, ones.
LDFLAGS += $(addprefix --wrap=,$(WRAPPED_SYSCALLS) $(WRAPPED_SDL))

# ---------------------------------------------------------------------------
# Compilation
# ---------------------------------------------------------------------------

# This project's own objects, board-scoped. These more-specific rules win
# over Rules.mk's in-place %.o/%.d, so nothing is written beside the source.
$(OBJDIR)/%.o: %.cpp | $(OBJDIR)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) $(SVM_CXXFLAGS) -c -o $@ $<

$(OBJDIR)/%.d: %.cpp | $(OBJDIR)
	@$(CPP) $(CPPFLAGS) $(SVM_CXXFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

# ScummVM's objects, mirroring its source tree under $(OBJDIR)/svm/. The
# source tree itself is the upstream submodule, and is never written to.
$(OBJDIR)/svm/%.o: $(SVM)/%.cpp
	@mkdir -p $(dir $@)
	@echo "  CPP   $@"
	@$(CPP) $(CPPFLAGS) $(SVM_CXXFLAGS) -c -o $@ $<

$(OBJDIR)/svm/%.d: $(SVM)/%.cpp
	@mkdir -p $(dir $@)
	@$(CPP) $(CPPFLAGS) $(SVM_CXXFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

$(OBJDIR)/svm/%.o: $(SVM)/%.c
	@mkdir -p $(dir $@)
	@echo "  CC    $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/svm/%.d: $(SVM)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -M -MG -MT $(@:.d=.o) -MT $@ -MF $@ $<

$(OBJDIR):
	@mkdir -p $(OBJDIR)

-include $(DEPS)

# What this build reads out of the upstream tree, for when the question is
# "was that file compiled" and the answer has to come from the build rather
# than from memory.
.PHONY: sources
sources:
	@for s in $(SVM_SRCS); do echo $$s; done

# The image link rule. sdl-app.mk lets the caller choose the linker script,
# so this build supplies its own: the shim's script with room made at image
# offset 0x800 for the patchable-defaults block. Everything else about the
# script — including the TLS-section adjacency binutils 2.44 and later
# require — is inherited from the shim's.
SDL_APP_LDSCRIPT = $(CURDIR)/scummvm-defaults.ld
include $(SHIM)/sdl-app.mk

# The other half of the guarantee the linker script's ASSERTs begin: check
# the BYTES of the finished image, not just the link. A pre-boot writer
# refuses to stamp an image whose magic is not at 0x800, so an image that
# lost it would boot and then quietly ignore everything ever sent to it —
# including --rapi-debug-uart, which is the channel used to drive the board
# when nobody is at the bench. Better to have no image than that one.
.PHONY: image
image: $(TARGET).img
	@if [ "`dd if=$(TARGET).img bs=4 skip=512 count=1 2>/dev/null`" = "PM8D" ]; \
	then \
		echo "  GATE  $(TARGET).img defaults block at 0x800: present"; \
	else \
		echo "  GATE  $(TARGET).img: defaults block ABSENT at 0x800 — image deleted"; \
		rm -f $(TARGET).img; \
		exit 1; \
	fi

# Board-scoped clean: drop just this board's tree, leaving the other two
# boards' images and objects untouched.
.PHONY: clean-board
clean-board:
	rm -rf $(OBJDIR)

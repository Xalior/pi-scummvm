# harvest-sources.mk — ask ScummVM's own makefiles for this build's source
# list, and print it.
#
# Run by `make regen-sources` at the repository root, which writes the answer
# into mk/upstream-sources.mk. It is not part of a build: reading a hundred
# module.mk files and asking the filesystem about every object was work the
# build repeated on every invocation, for every board, to arrive at the same
# answer each time.
#
# Run it again when the scummvm submodule pin moves or a switch in
# mk/scummvm-features.mk changes. Nothing else changes the answer.

SVM ?= ../scummvm

include scummvm-features.mk

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

.PHONY: print
print:
	@printf '%s\n' $(SVM_SRCS)

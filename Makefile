#
# pi-scummvm — ScummVM as a bootable bare-metal Raspberry Pi image.
#
#   make check-toolchain     report the cross compiler this build will use
#   make deps                the three circle-stdlib worlds and the shim
#                            archives built against them (long: the worlds
#                            build newlib and libc++ from source)
#   make deps-rpi4           the same for one board only, for a machine that
#                            cannot hold three worlds at once
#   make rpi5 | rpi4 | rpi3  one board's kernel image
#   make kernels             all three, built in parallel
#   make verify              truth-gate: every image exists and is non-empty
#   make netboot             stage the Pi 5 image and its boot configuration
#                            into build/netboot-rpi5/
#   make media               fetch this game's data into media/ — a separate
#                            step, run by a person, never by a build
#   make card                stage the whole card into build/sd-card/,
#                            copying in whatever media/ holds
#   make clean-boards        drop every board's build tree
#
# The three boards never share mutable state: each has its own circle-stdlib
# world, its own shim archive and its own object directory, so building them
# at the same time is safe and building one never disturbs another.
#
# The libc++ sources every world is built from are one immutable git tag, and
# CIRCLE_LLVM says where that checkout lives. The default puts it beside this
# repository, which is right for a plain clone and for a CI runner. Point
# several projects at one directory to fetch it once for all of them:
#
#   make deps CIRCLE_LLVM=/path/to/circle-llvm
#

include mk/toolchain.mk

# Stated explicitly because the first rule this file sees comes from an
# included makefile, and that would otherwise decide the default goal.
.DEFAULT_GOAL := kernels

BOARDS ?= rpi3 rpi4 rpi5

IMAGE_rpi3 = kernel8.img
IMAGE_rpi4 = kernel8-rpi4.img
IMAGE_rpi5 = kernel_2712.img

# Where the game's own directory sits on the card. It has to be stated here
# as well as in host/Makefile because the card staging below builds that
# directory, and both have to name the same place.
GAME_DIR = games/scummvm

.PHONY: deps kernels verify netboot media card clean-boards $(BOARDS)
.PHONY: $(addprefix deps-,$(BOARDS))

deps:
	$(MAKE) -C circle-libsdl2 deps

# One board's dependencies: its own circle-stdlib world and the shim archive
# built against it. A machine with a small disk — a CI runner, most obviously
# — builds one board at a time and keeps only that board's world.
# Written as a static pattern rule over the board list rather than a plain
# pattern rule: these targets are phony, and make does not apply pattern rules
# to phony targets — it would quietly answer "nothing to be done" and leave
# the world unbuilt.
$(addprefix deps-,$(BOARDS)): deps-%:
	$(MAKE) -C circle-libsdl2 world BOARD=$*
	$(MAKE) -C circle-libsdl2 libSDL2-$*.a BOARD=$*

$(BOARDS): check-toolchain
	$(MAKE) -C host RAPI_BOARD=$@

# All three at once. Each sub-make owns a different world and a different
# output directory, so there is nothing for them to collide on.
#
# Each board is waited for BY PID, and its status kept. A bare `wait` reports
# only that the shell has no children left — it is success whatever the jobs
# did — so a board that failed to build would leave this target reporting
# success, and the truth-gate would then pass the board's PREVIOUS image,
# still on disk.
kernels: check-toolchain
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# Truth-gate: ask the filesystem, not the exit codes. An image that is
# missing or empty fails here even if the build claimed success.
verify:
	@fail=0; \
	for b in $(BOARDS); do \
		case $$b in \
			rpi3) img=host/build/rpi3/$(IMAGE_rpi3) ;; \
			rpi4) img=host/build/rpi4/$(IMAGE_rpi4) ;; \
			rpi5) img=host/build/rpi5/$(IMAGE_rpi5) ;; \
		esac; \
		if [ -s "$$img" ]; then \
			echo "  OK    $$img ($$(wc -c < $$img | tr -d ' ') bytes)"; \
		else \
			echo "  FAIL  $$img missing or empty"; fail=1; \
		fi; \
	done; \
	exit $$fail

# The Pi 5 netboot bundle: the image the Pi 5 firmware looks for, plus the
# boot configuration it must be served alongside. Copy the contents into the
# TFTP root the board boots from (the Raspberry Pi firmware files themselves
# come from that root's existing installation, not from here).
NETBOOT_DIR = build/netboot-rpi5
netboot: rpi5
	@mkdir -p $(NETBOOT_DIR)
	@cp host/build/rpi5/$(IMAGE_rpi5) $(NETBOOT_DIR)/
	@cp host/config.txt host/cmdline.txt $(NETBOOT_DIR)/
	@echo "  STAGED $(NETBOOT_DIR)/"
	@ls -l $(NETBOOT_DIR)/

# ---------------------------------------------------------------------------
# The game's data
# ---------------------------------------------------------------------------
#
# media/ is where a game's own files live on the machine that builds the
# card. It is not tracked and it is never shipped: the files in it belong to
# the game's publisher, not to this project.
#
# THIS TARGET CANNOT FETCH ANYTHING, and it says so and fails. Every SCUMM
# game is somebody's commercial product; not one of them is given away, and
# there is no address this project could honestly download one from. What is
# left for the target to do is say precisely what to supply and where to put
# it — and then exit non-zero, because a step that could not deliver must
# not report success to whatever called it.
MEDIA_DIR = media

media:
	@echo "This repository ships no game data, and cannot fetch any."
	@echo ""
	@echo "Every SCUMM game is a commercial product. Use a copy you own:"
	@echo "the Steam, GOG and disc releases all install the game's files"
	@echo "as ordinary files."
	@echo ""
	@echo "  Copy ONE game's files into $(MEDIA_DIR)/game/, then run 'make card'."
	@echo ""
	@echo "See README.md for which games this build plays and what their"
	@echo "files are called."
	@exit 1

# ---------------------------------------------------------------------------
# The card
# ---------------------------------------------------------------------------
#
# Staged into a directory to copy onto media formatted elsewhere: the three
# kernels, the boot configuration, ScummVM's own interface theme out of the
# upstream checkout, and whatever media/ holds.
#
# The theme is staged as an unpacked directory rather than the .zip upstream
# also ships, because this build has no zlib and cannot open a zip.
#
# THIS TARGET NEVER DOWNLOADS ANYTHING and does not depend on `media`. A card
# staged on a machine with an empty media/ is a complete card except for the
# game, and it says which part is missing rather than failing — that is a
# legitimate build, and it is the one a continuous-integration runner makes.
#
# game/ is one game's directory. The kernel boots ScummVM pointed at it and
# asks ScummVM to work out what is in there, so whatever is copied in is what
# plays.
CARD_DIR = build/sd-card
card: kernels
	@rm -rf $(CARD_DIR)
	@mkdir -p $(CARD_DIR)/$(GAME_DIR)/themes $(CARD_DIR)/$(GAME_DIR)/game
	@cp host/build/rpi3/$(IMAGE_rpi3) $(CARD_DIR)/
	@cp host/build/rpi4/$(IMAGE_rpi4) $(CARD_DIR)/
	@cp host/build/rpi5/$(IMAGE_rpi5) $(CARD_DIR)/
	@cp host/config.txt host/cmdline.txt $(CARD_DIR)/
	@cp -R scummvm/gui/themes/scummclassic $(CARD_DIR)/$(GAME_DIR)/themes/
	@if [ -d $(MEDIA_DIR)/game ] && [ -n "`ls -A $(MEDIA_DIR)/game 2>/dev/null`" ]; then \
		cp -R $(MEDIA_DIR)/game/. $(CARD_DIR)/$(GAME_DIR)/game/; \
		echo "  MEDIA  $(MEDIA_DIR)/game/ -> $(CARD_DIR)/$(GAME_DIR)/game/"; \
	else \
		echo "  MEDIA  none: $(MEDIA_DIR)/game/ is empty or absent"; \
	fi
	@echo "  STAGED $(CARD_DIR)/"
	@echo ""
	@echo "  Not staged, and to be added by hand:"
	@echo "    the Raspberry Pi firmware files, in the card's root"
	@if [ -z "`ls -A $(CARD_DIR)/$(GAME_DIR)/game 2>/dev/null`" ]; then \
		echo "    one SCUMM game's files, in $(CARD_DIR)/$(GAME_DIR)/game/"; \
		echo "                             (or in $(MEDIA_DIR)/game/, and stage again)"; \
	fi

clean-boards:
	@for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b clean-board; done
	rm -rf $(NETBOOT_DIR) $(CARD_DIR)

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
#   make regen-sources       ask ScummVM's own makefiles for the source
#                            list again and write mk/upstream-sources.mk
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

.PHONY: deps kernels rebuild regen-sources verify netboot media card clean-boards $(BOARDS)
.PHONY: $(addprefix deps-,$(BOARDS)) $(addprefix rebuild-,$(BOARDS))

deps:
	+@$(NOT_DRY_RUN)
	$(MAKE) -C circle-libsdl2 deps

# One board's dependencies: its own circle-stdlib world and the shim archive
# built against it. A machine with a small disk — a CI runner, most obviously
# — builds one board at a time and keeps only that board's world.
# Written as a static pattern rule over the board list rather than a plain
# pattern rule: these targets are phony, and make does not apply pattern rules
# to phony targets — it would quietly answer "nothing to be done" and leave
# the world unbuilt.
$(addprefix deps-,$(BOARDS)): deps-%:
	+@$(NOT_DRY_RUN)
	$(MAKE) -C circle-libsdl2 world BOARD=$*
	$(MAKE) -C circle-libsdl2 libSDL2-$*.a BOARD=$*

# ---------------------------------------------------------------------------
# ScummVM's source list
# ---------------------------------------------------------------------------
#
# Which of ScummVM's sources this build compiles is ScummVM's own answer, not
# this project's: its module.mk files name a module's sources and decide which
# of them exist by testing the switches in mk/scummvm-features.mk. Reading them
# is a walk over a hundred files and a filesystem lookup for every object, and
# the answer only changes when the submodule pin moves or a switch does.
#
# So it is asked once and written down. mk/upstream-sources.mk is that answer,
# checked in and read by the build as a plain list; mk/harvest-sources.mk is
# the walk that produces it.
#
# Run this after moving the scummvm pin or changing a feature switch, and
# commit what it writes.
.PHONY: regen-sources
regen-sources:
	+@$(NOT_DRY_RUN)
	@{ \
		echo "# ScummVM's source list, as ScummVM's own module.mk files give it"; \
		echo "# for the feature switches in mk/scummvm-features.mk."; \
		echo "#"; \
		echo "# GENERATED — do not edit. Produced by mk/harvest-sources.mk, which"; \
		echo "# reads upstream's makefiles; run 'make regen-sources' to write it"; \
		echo "# again after the scummvm submodule pin moves or a feature switch"; \
		echo "# changes. Nothing else changes the answer, and the build never"; \
		echo "# reads upstream's makefiles itself."; \
		echo "#"; \
		echo "# Paths are relative to the scummvm submodule."; \
		echo "SVM_SRCS := \\"; \
		$(MAKE) -s -C mk -f harvest-sources.mk print \
			| sed -e '$$ ! s/$$/ \\/' -e 's/^/\t/'; \
	} > mk/upstream-sources.mk.new
	@test -s mk/upstream-sources.mk.new || { \
		echo "  FAIL  the harvest produced no source list"; \
		rm -f mk/upstream-sources.mk.new; exit 1; }
	@mv mk/upstream-sources.mk.new mk/upstream-sources.mk
	@echo "  GEN   mk/upstream-sources.mk ($$(grep -c '\.c' mk/upstream-sources.mk) sources)"

$(BOARDS): check-toolchain
	+@$(NOT_DRY_RUN)
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
	+@$(NOT_DRY_RUN)
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# One board from nothing: its build tree is removed before the build, so no
# object can be inherited from a previous one. Written as a static pattern rule
# over the board list for the same reason deps-% is.
$(addprefix rebuild-,$(BOARDS)): rebuild-%: check-toolchain
	+@$(NOT_DRY_RUN)
	$(MAKE) -C host RAPI_BOARD=$* rebuild

# All three from nothing, in parallel, waited for by PID exactly as kernels is.
rebuild: check-toolchain
	+@$(NOT_DRY_RUN)
	@pids=; fail=0; \
	for b in $(BOARDS); do $(MAKE) -C host RAPI_BOARD=$$b rebuild & pids="$$pids $$!"; done; \
	for p in $$pids; do wait $$p || fail=1; done; \
	exit $$fail

# Truth-gate: ask the filesystem, not the exit codes. An image that is
# missing, empty, or does not carry the defaults block at offset 0x800 fails
# here even if the build claimed success.
#
# What this cannot tell you is whether the image was built from the sources as
# they now stand. That is a question about the build, not about the file, and
# `make rebuild` is the only answer to it.
verify:
	@fail=0; \
	for b in $(BOARDS); do \
		case $$b in \
			rpi3) img=host/build/rpi3/$(IMAGE_rpi3) ;; \
			rpi4) img=host/build/rpi4/$(IMAGE_rpi4) ;; \
			rpi5) img=host/build/rpi5/$(IMAGE_rpi5) ;; \
		esac; \
		if [ ! -s "$$img" ]; then \
			echo "  FAIL  $$img missing or empty"; fail=1; \
		elif [ "`dd if=$$img bs=4 skip=512 count=1 2>/dev/null`" != "PM8D" ]; then \
			echo "  FAIL  $$img has no defaults block at 0x800"; fail=1; \
		else \
			echo "  OK    $$img ($$(wc -c < $$img | tr -d ' ') bytes, defaults block present)"; \
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
# Every SCUMM game is somebody's commercial product except one: LucasArts
# gave away a demo of Day of the Tentacle on magazine cover discs in 1997,
# and it is still legitimately available. `make media` fetches that one
# file this repository can honestly download, and says so. For every other
# SCUMM game — the full Day of the Tentacle included — there is no address
# this project could download from; a copy you own goes into media/game/ by
# hand, in place of or after the demo.
MEDIA_DIR = media

DOTT_DEMO_ZIP_URL = https://archive.org/download/day-of-the-tentacle-demo/Day%20of%20the%20Tentacle%20demo.zip
DOTT_DEMO_SHA256  = 712d84a4142b5816d6c1ffee2df3d9d75be6879e9e53327b1ae2ef62eb4625dc

# sha256sum on Linux, shasum on macOS. Whichever exists; if neither does the
# target stops rather than accepting a download it cannot check.
SHA256SUM := $(firstword $(shell command -v sha256sum 2>/dev/null) \
                         $(shell command -v shasum 2>/dev/null))

media:
	@if [ -z "$(SHA256SUM)" ]; then \
		echo "  MEDIA no checksum tool on this machine (sha256sum or shasum)"; \
		echo "        — refusing to download something that cannot be verified."; \
		exit 1; \
	fi
	@command -v unzip >/dev/null 2>&1 || { \
		echo "  MEDIA no 'unzip' on this machine — the demo is distributed as"; \
		echo "        a zip and cannot be unpacked without it."; \
		exit 1; }
	@mkdir -p $(MEDIA_DIR)/game
	@if [ -f "$(MEDIA_DIR)/game/DOTTDEMO.000" ]; then \
		echo "  MEDIA $(MEDIA_DIR)/game/DOTTDEMO.000 already here — verifying"; \
	else \
		echo "  MEDIA fetching $(DOTT_DEMO_ZIP_URL)"; \
		curl -fL --retry 3 -o "$(MEDIA_DIR)/dott-demo.zip.part" "$(DOTT_DEMO_ZIP_URL)" || { \
			rm -f "$(MEDIA_DIR)/dott-demo.zip.part"; \
			echo "  MEDIA download failed"; exit 1; }; \
		mv "$(MEDIA_DIR)/dott-demo.zip.part" "$(MEDIA_DIR)/dott-demo.zip"; \
		got=`$(SHA256SUM) -a 256 "$(MEDIA_DIR)/dott-demo.zip" 2>/dev/null || $(SHA256SUM) "$(MEDIA_DIR)/dott-demo.zip"`; \
		got=`echo "$$got" | awk '{print $$1}'`; \
		if [ "$$got" != "$(DOTT_DEMO_SHA256)" ]; then \
			echo "  MEDIA SHA256 MISMATCH for $(MEDIA_DIR)/dott-demo.zip"; \
			echo "        expected $(DOTT_DEMO_SHA256)"; \
			echo "        got      $$got"; \
			echo "        the file has been left in place for inspection, and is"; \
			echo "        NOT safe to put on a card."; \
			exit 1; \
		fi; \
		echo "  MEDIA $(MEDIA_DIR)/dott-demo.zip verified against this project's own SHA256"; \
		unzip -q -j -o "$(MEDIA_DIR)/dott-demo.zip" "DOTTDEMO.000" "DOTTDEMO.001" "DOTTDEMO.EXE" \
			"MONSTER.SOU" "GMIDI.IMS" "ROLAND.IMS" "ADLIB.IMS" -d "$(MEDIA_DIR)/game" || { \
			rm -f "$(MEDIA_DIR)/dott-demo.zip"; \
			echo "  MEDIA could not unpack the demo zip"; exit 1; }; \
		rm -f "$(MEDIA_DIR)/dott-demo.zip"; \
	fi
	@for f in DOTTDEMO.000 DOTTDEMO.001 MONSTER.SOU; do \
		if [ ! -f "$(MEDIA_DIR)/game/$$f" ]; then \
			echo "  MEDIA $(MEDIA_DIR)/game/$$f is missing after unpacking — the"; \
			echo "        archive did not contain what this target expects."; \
			exit 1; \
		fi; \
	done
	@head -c 4 "$(MEDIA_DIR)/game/MONSTER.SOU" | grep -q "SOU " || { \
		echo "  MEDIA $(MEDIA_DIR)/game/MONSTER.SOU does not begin with the SCUMM"; \
		echo "        SOU magic"; exit 1; }; \
	echo "  MEDIA $(MEDIA_DIR)/game/ verified ($$(wc -c < $(MEDIA_DIR)/game/DOTTDEMO.001 | tr -d ' ') byte resource file, $$(wc -c < $(MEDIA_DIR)/game/MONSTER.SOU | tr -d ' ') byte sound file)"
	@printf '%s\n' \
		"Day of the Tentacle demo — the only freely distributable SCUMM game" \
		"" \
		"Source:   $(DOTT_DEMO_ZIP_URL)" \
		"Item:     https://archive.org/details/day-of-the-tentacle-demo" \
		"Fetched:  `date -u '+%Y-%m-%d %H:%M:%S UTC'`" \
		"SHA256:   $(DOTT_DEMO_SHA256)  (of the source zip; computed from this" \
		"          download, not independently published)" \
		"" \
		"What it is: LucasArts' 1997 promotional demo of Day of the Tentacle," \
		"taken from PC Gamer (UK)'s June 1997 cover disc — the SCUMM engine's" \
		"own index/resource files (DOTTDEMO.000/.001), its speech and sound" \
		"resource (MONSTER.SOU) and its music driver data, the same shape the" \
		"full retail game uses." \
		"" \
		"Licence: LucasArts' original demo distribution terms — given away" \
		"freely on magazine cover discs to sell the retail game. Not the" \
		"retail data. This repository does not redistribute it. Day of the" \
		"Tentacle is a trademark of LucasArts Entertainment Company LLC." \
		"" \
		"Every other SCUMM game, including the full Day of the Tentacle, is" \
		"paid data with no free equivalent and is not fetched by this target." \
		"Copy your own into $(MEDIA_DIR)/game/, replacing this demo's files," \
		"and run 'make card' again." \
		> $(MEDIA_DIR)/provenance.txt
	@echo "  MEDIA provenance written to $(MEDIA_DIR)/provenance.txt"

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

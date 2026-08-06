# scummvm-features.mk — which engine this build plays, and the feature
# switches ScummVM's own makefiles read.
#
# Read twice: by host/Makefile, which compiles the build these switches
# select, and by mk/harvest-sources.mk, which asks ScummVM's own module.mk
# files which sources those switches bring in. The two must agree, which is
# why there is one copy of them.
#
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

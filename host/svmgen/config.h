//
// config.h — the build configuration ScummVM's own `configure` would write.
//
// ScummVM discovers its platform by running a shell script that compiles and
// links test programs. Nothing here can be linked without a Circle kernel
// around it, so that script cannot answer for this machine, and this file
// answers instead: it is upstream's own interface, written down rather than
// probed.
//
// common/scummsys.h reads it only when HAVE_CONFIG_H is defined, so host/
// Makefile defines that alongside the rest of ScummVM's compiler defines.
// Without it this file sits on the include path unopened and every switch
// below reads as off, which compiles and links and then behaves as a build
// with no game detection, no scalers and no music drivers.
//
// It is deliberately arranged in the same order and with the same spellings
// as a generated one, so the two can be read side by side when upstream adds
// a switch. A line is `#define` where the feature is present and `#undef`
// where it is not — never absent, because an absent line reads as a feature
// nobody has thought about yet.
//
// The shape of this machine, and what follows from it:
//
//   No shared libraries anywhere.  Every optional codec, decoder and network
//   library ScummVM can use is off: there is nothing to link against and no
//   loader to find it. That includes zlib, so nothing on the card may arrive
//   compressed.
//
//   One engine, statically linked.  DETECTION_STATIC puts game detection in
//   the executable rather than in a plugin, and DYNAMIC_MODULES stays off:
//   there is no dlopen here.
//
//   Software rendering only.  The Raspberry Pi has no bare-metal GPU driver,
//   so USE_OPENGL and every shader path are off. ScummVM draws into system
//   memory, which is what circle-libsdl2 presents from.
//
#ifndef CONFIG_H
#define CONFIG_H

#define SCUMM_LITTLE_ENDIAN
#undef SCUMM_BIG_ENDIAN

// Kept on. AArch64 tolerates unaligned access on ordinary memory, so this
// costs a little speed rather than buying correctness — but the game data is
// read straight off the card into structures ScummVM casts over, and the one
// place that would fault is a place with no debugger attached.
#define SCUMM_NEED_ALIGNMENT

#undef USE_ELF_LOADER
#undef DYNAMIC_MODULES
#define DETECTION_STATIC
#undef DETECTION_FULL
#undef USE_MT32EMU
#undef DISABLE_NUKED_OPL
#undef USE_RGB_COLOR
#undef USE_HIGHRES
#define USE_SAVEGAME_TIMESTAMP
#define USE_SCALERS
#undef USE_HQ_SCALERS
#undef USE_EDGE_SCALERS
#define USE_ASPECT
#undef USE_OGG
#undef USE_VORBIS
#undef USE_TREMOR
#undef ENABLE_OPL2LPT
#undef USE_RETROWAVE
#undef USE_FLAC
#undef USE_MAD
#undef USE_ALSA
#undef USE_JPEG
#undef USE_PNG
#undef USE_FAAD
#undef USE_SEQ_MIDI
#undef USE_SNDIO
#undef USE_TIMIDITY
#undef USE_ZLIB
#undef USE_A52
#undef USE_LIBCURL
#undef USE_BASIC_NET
#undef USE_HTTP
#undef USE_OPENMPT
#undef USE_MIKMOD
#undef USE_DLC
#undef USE_SCUMMVMDLC
#undef USE_DOCKTILEPLUGIN
#undef USE_FLUIDSYNTH
#undef USE_FLUIDLITE
#undef USE_SONIVOX
#undef USE_READLINE
#undef USE_TEXT_CONSOLE_FOR_DEBUGGER
#undef USE_GTK
#undef USE_FREETYPE2
#undef USE_GLES_MODE
#undef USE_OPENGL
#undef USE_GLAD
#undef USE_OPENGL_GAME
#undef USE_OPENGL_SHADERS
#undef USE_NASM
#undef USE_PANDOC
#undef USE_CURL
#undef USE_FRIBIDI
#undef ENABLE_TEST_CPP_11
#undef USE_DISCORD
#undef ENABLE_VKEYBD
#undef ENABLE_EVENTRECORDER
#undef USE_TRANSLATION
#undef USE_TASKBAR
#undef USE_SYSTEM_PRINTING
#undef USE_SYSDIALOGS
#undef USE_TTS
#undef USE_BINK
#undef USE_UPDATES
#undef USE_CLOUD

// Windows only: it decides whether the resource script embeds engine data
// and fonts in the executable. Nothing on this platform reads it.
#undef BUILTIN_RESOURCES

// The vector units ScummVM would dispatch to at run time. AArch64 always has
// NEON, but ScummVM's NEON paths are selected by asking SDL at run time
// whether the processor has it, and this port's SDL implementation is a
// bare-metal shim with no such enquiry to make.
#undef SCUMMVM_SSE2
#undef SCUMMVM_AVX2
#undef SCUMMVM_NEON

/* components */
/* #define USE_CDTOONS */
/* #define USE_ENET */
#define USE_FMTOWNS_PC98_AUDIO
/* #define USE_GIF */
/* #define USE_HNM */
/* #define USE_IMGUI */
/* #define USE_INDEO3 */
/* #define USE_INDEO45 */
/* #define USE_JYV1 */
/* #define USE_LUA */
/* #define USE_MFC */
/* #define USE_VPX */
/* #define USE_THEORADEC */
#define USE_MIDI
/* #define USE_MPCDEC */
/* #define USE_MPEG2 */
/* #define USE_QDM2 */
#define USE_SID_AUDIO
/* #define USE_SVQ1 */
/* #define USE_TINYGL */
/* #define USE_TRUEMOTION1 */
/* #define USE_UNIVERSALTRACKER */
/* #define USE_VGMTRANS_AUDIO */
/* #define USE_XAN */
/* end of components */

#endif /* CONFIG_H */

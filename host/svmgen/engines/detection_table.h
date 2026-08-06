/*
 * detection_table.h — the game-detection plugins linked into this build.
 *
 * ScummVM can detect every game it knows about even when the engine that
 * plays it is absent; a full build therefore carries a detection entry for
 * every engine. This build carries one, matching the one engine it can play,
 * so a card holding anything else is told plainly that no engine here
 * recognises it.
 *
 * Included by base/plugins.cpp. The counterpart is plugins_table.h beside
 * this file.
 */
#if defined(ENABLE_SCUMM) || defined(DETECTION_FULL)
LINK_PLUGIN(SCUMM_DETECTION)
#endif

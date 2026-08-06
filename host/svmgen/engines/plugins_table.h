/*
 * plugins_table.h — the engines linked into this build.
 *
 * ScummVM's `configure` writes this file from every engine's
 * configure.engine, one entry per engine, each guarded so that only the
 * enabled ones are linked. This build enables exactly one, so the generated
 * file would be this file with a hundred disabled entries around it.
 *
 * Included by base/plugins.cpp, and reached before upstream's own copy
 * because this directory comes first on the include path.
 */
#if PLUGIN_ENABLED_STATIC(SCUMM)
LINK_PLUGIN(SCUMM)
#endif

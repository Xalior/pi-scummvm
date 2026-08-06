//
// scummvm_backend.cpp — the backend ScummVM runs on here, and the entry
// point that starts it.
//
// ScummVM reaches its platform through one object, an OSystem. Upstream
// ships a family of them: OSystem_SDL holds everything common to a machine
// with SDL underneath, and each operating system subclasses it to answer the
// three or four questions only that system can answer — where the
// configuration file lives, where the log goes, which filesystem to use.
// Every one of those subclasses comes with a main() beside it.
//
// This board is not an operating system, so none of them fits, and this file
// is its own: a small subclass of OSystem_SDL, and the entry point the
// kernel calls in place of upstream's main(). Nothing upstream is edited;
// the parent class does all the work, exactly as it does for Linux.
//
// WHAT THIS MACHINE ANSWERS DIFFERENTLY:
//
//   The filesystem is POSIX.  Circle's newlib glue provides open, stat,
//   opendir and readdir, so ScummVM's POSIX filesystem works unchanged. It
//   reaches the SD card through the syscall layer in circle_syscalls.cpp,
//   which marshals every call to the core that owns the hardware.
//
//   There is no home directory and no environment.  ScummVM's POSIX backend
//   builds every path it uses out of $HOME and the XDG variables, and there
//   are none here. Every path is under RAPI_GAME_DIR instead, which is where
//   the card puts this game.
//
//   There is no log file.  The board's log is the serial port, and
//   OSystem_SDL already writes there through stderr — which the syscall
//   layer turns into the shim's log ring. A second copy on the card would
//   be a file nobody can read while the board is running.
//
#include "common/scummsys.h"

#include "backends/platform/sdl/sdl.h"
#include "backends/fs/posix/posix-fs-factory.h"
#include "backends/saves/default/default-saves.h"
#include "base/main.h"

#include "common/config-manager.h"

namespace {

// ScummVM's own files and everything it writes, all under the one directory
// this game owns on the card. The build sets RAPI_GAME_DIR; nothing here
// invents a path of its own.
const char *const kConfigFile = RAPI_GAME_DIR "/scummvm.ini";
const char *const kSavePath   = RAPI_GAME_DIR "/saves";

class OSystem_RAPI final : public OSystem_SDL {
public:
	void init() override {
		// The filesystem ScummVM will use for every file it opens. It has
		// to exist before OSystem_SDL::init, which is where the search
		// paths are first consulted.
		_fsFactory = new POSIXFilesystemFactory();

		OSystem_SDL::init();
	}

	void initBackend() override {
		// Saved games. The default manager would ask the base class where
		// to put them, and that answer is built from a home directory this
		// machine does not have. Naming the directory outright is both
		// shorter and the only correct answer here.
		if (_savefileManager == nullptr)
			_savefileManager = new DefaultSaveFileManager(Common::Path(kSavePath, '/'));

		OSystem_SDL::initBackend();
	}

protected:
	Common::Path getDefaultConfigFileName() override {
		return Common::Path(kConfigFile, '/');
	}

	// An empty path means "do not open a log file". The board's log is the
	// serial port; see the note at the top of this file.
	Common::Path getDefaultLogFileName() override {
		return Common::Path();
	}
};

} // namespace

// The kernel's entry into ScummVM, called on the application core once the
// board is up and the shim's core split is armed. It is what upstream's
// posix-main.cpp would be if this were an operating system: build the
// backend, hand it to ScummVM, run, tear down.
//
// extern "C" because the kernel is the caller and this is the one symbol it
// needs by name; everything else about the program stays C++.
extern "C" int rapi_scummvm_main(int argc, char **argv) {
	g_system = new OSystem_RAPI();
	assert(g_system);

	g_system->init();

	const int res = scummvm_main(argc, argv);

	g_system->destroy();

	return res;
}

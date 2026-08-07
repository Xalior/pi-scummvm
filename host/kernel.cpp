//
// kernel.cpp — brings the board up, then hands control to ScummVM.
//
// ScummVM is an ordinary command-line program: it expects a working standard
// library, a filesystem holding its game data, and an SDL2 implementation.
// This file supplies the first two and starts the third, then calls the
// entry point in scummvm_backend.cpp with a fixed argument list.
//
// EVERYTHING SCUMMVM TOUCHES LIVES IN ONE DIRECTORY ON THE CARD,
// RAPI_GAME_DIR, which the host Makefile sets and nothing upstream knows
// about. One card carries several games, and two of them writing a
// configuration file into the card's root would each silently overwrite the
// other's.
//
// Three things point at it, and between them they cover every path ScummVM
// can reach: the build sets DATA_PATH to it, which is where ScummVM looks
// for the files it ships itself — the engine data and the interface theme;
// the backend in scummvm_backend.cpp puts the configuration file and the
// saved games there; and this kernel makes it the working directory before
// the program starts, so anything opened by a relative name lands there too
// rather than in the root.
//
// This kernel also decides the core layout (see kernel.h for the roles) and
// hands one core to the shim's presentation worker. The library never starts
// a core; electing one is the host's job, and this is where it happens. The
// program itself knows none of it: it calls plain SDL, and its file access
// reaches the marshalled I/O service through the syscall layer in
// circle_syscalls.cpp.
//
#include "kernel.h"
#include "defaults.h"
#include "defaultsblock.h"
#include <circle/startup.h>
#include <circle/machineinfo.h>
#include <SDL2/SDL_circle.h>
#include <SDL2/SDL_error.h>
#include <unistd.h>
#include <atomic>

// This port's own entry point, in scummvm_backend.cpp: it builds the
// backend ScummVM runs on and calls scummvm_main. It stands in for the
// per-operating-system main() upstream ships one of for every platform it
// supports, none of which describes a board with no operating system.
extern "C" int rapi_scummvm_main(int argc, char **argv);

void CGlueStdioInit(CConsole &rConsole);

static const char From[] = "scummvm";

// The program's command line.
//
// ScummVM with no arguments opens its launcher, which lists the games named
// in its configuration file — and on a fresh card there is no configuration
// file and the list is empty. So the card is asked instead:
// `--auto-detect` with a `--path` means "work out which game is in that
// directory and play it", which is upstream's own answer for a machine with
// nothing configured on it. Any SCUMM game the user copies into that one
// directory is the game that boots.
//
// --scale-factor=2 suits the games this engine mostly plays. They draw
// 320x200, ScummVM's aspect correction makes that 320x240, and two of those
// is the 640x480 declared below — so ScummVM's own output is exactly the
// size of the display and the shim has nothing left to rescale. THE VERSION
// 7 AND 8 GAMES DRAW 640x480 ALREADY: for those, append --scale-factor=1
// through the defaults block, which is what that block is for. Getting it
// wrong costs a rescaling pass, not a picture.
//
// --no-fullscreen because there is nothing else on this screen to be
// fullscreen over. The shim's window is the whole display already, and
// asking for fullscreen only makes ScummVM ask the window manager that does
// not exist here for something it already has.
//
// These are the BAKED arguments. Anything written into the image's defaults
// block is appended to them before the program runs, so a boot can add to
// this — or override any of it, since a later setting of the same option
// wins — without the card or the build changing.
static const char *ScummVMArgv[] = {
    "scummvm",
    "--path=" RAPI_GAME_DIR "/game",
    "--scale-factor=2",
    "--no-fullscreen",
    "--auto-detect",
};

// The final list: the baked arguments, plus whatever the block carries.
// Sized for the block's worst case — every byte of its capacity a
// single-character argument — on top of the baked ones, plus the
// terminating null.
static const char *s_FinalArgv[sizeof(ScummVMArgv) / sizeof(ScummVMArgv[0])
                               + DEFAULTS_BUFFER_BYTES / 2 + 1];
static int s_FinalArgc = 0;

// ---------------------------------------------------------------------------
// The gate between core 0 and the application core.
//
// The cores are started at the end of Initialize, because that is where a
// Circle world is finished. But the application must not begin until the
// shim's split is armed — until then its platform calls would run on the
// wrong core with no mailbox to carry them. So the application core waits
// here, and core 0 opens the gate once SDL2Circle_SplitInit has returned.
//
// The return travels back the same way. Core 0 cannot join a core, so the
// application core publishes the result and core 0 watches for it while
// yielding to the scheduler — which is what keeps the servo, the watchdog
// and every device alive for as long as the program runs.
// ---------------------------------------------------------------------------

static std::atomic<int> s_AppGate{0};      // core 0 -> application core
static std::atomic<int> s_AppDone{0};      // application core -> core 0
static int s_AppResult = -1;

static inline void PublishToOtherCores(void)
{
    asm volatile("dsb ish; sev" ::: "memory");
}

static void ParkCore(void)
{
    for (;;)
        asm volatile("wfe" ::: "memory");
}

void CSplitCores::Run(unsigned nCore)
{
    // Before this core runs anything of ours: every core here may reach code
    // that throws, and a throw reads this register first.
    SDL2Circle_ArmCoreRuntime();

    switch (nCore)
    {
    case 1:
        // The application core. Wait for the gate, run the program, publish
        // what it returned, then go quiet — this core has no other purpose
        // and must not fall through into anything.
        while (!s_AppGate.load(std::memory_order_acquire))
            asm volatile("wfe" ::: "memory");

        s_AppResult = rapi_scummvm_main(s_FinalArgc,
                                        const_cast<char **>(s_FinalArgv));

        s_AppDone.store(1, std::memory_order_release);
        PublishToOtherCores();
        ParkCore();
        break;

    case 2:
        // The elected presentation core. Never returns.
        SDL2Circle_SplitPresentCore();
        break;

    default:
        ParkCore();
        break;
    }
}

CKernel::CKernel(void)
    // Serial device 0 is the GPIO14/15 header UART on every board. Named
    // explicitly because Circle's RASPPI >= 5 default (SERIAL_DEVICE_DEFAULT
    // = 10) is the Pi 5's dedicated debug connector, not the header.
    : m_Serial(0, FALSE, 0),
      m_Timer(&m_Interrupt),
      m_Logger(m_Options.GetLogLevel(), &m_Timer),
      m_EMMC(&m_Interrupt, &m_Timer, &m_ActLED),
      m_Console(&m_Serial, &m_Serial),    // stdio over the UART
      m_USB(&m_Interrupt, &m_Timer, TRUE /* plug-and-play */)
{
    m_ActLED.Blink(3);
}

// Build-timestamp epoch (seconds since 1970-01-01 UTC) from __DATE__/__TIME__.
// Monotonic across releases, always a plausible "now".
static unsigned BuildEpoch(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   // "Mmm dd yyyy"
    const char *t = __TIME__;   // "hh:mm:ss"

    int mon = 1;
    for (int i = 0; i < 12; i++)
        if (d[0] == months[i*3] && d[1] == months[i*3+1] && d[2] == months[i*3+2])
            { mon = i + 1; break; }
    int day  = (d[4] == ' ' ? 0 : d[4] - '0') * 10 + (d[5] - '0');
    int year = (d[7]-'0')*1000 + (d[8]-'0')*100 + (d[9]-'0')*10 + (d[10]-'0');
    int hh = (t[0]-'0')*10 + (t[1]-'0');
    int mm = (t[3]-'0')*10 + (t[4]-'0');
    int ss = (t[6]-'0')*10 + (t[7]-'0');

    // days since 1970-01-01 (civil-to-days, treated as UTC)
    int y = year - (mon <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    unsigned doe = yoe*365 + yoe/4 - yoe/100 + doy;
    long days = (long)era*146097 + (long)doe - 719468;
    return (unsigned)(days * 86400L + hh*3600 + mm*60 + ss);
}

boolean CKernel::Initialize(void)
{
    boolean bOK = TRUE;
    if (bOK) bOK = m_Serial.Initialize(115200);
    if (bOK) bOK = m_Logger.Initialize(&m_Serial);
    if (bOK) bOK = m_Interrupt.Initialize();
    if (bOK) bOK = m_Timer.Initialize();
    // No battery RTC on a Pi: seed the wall clock with the build time —
    // like a device whose clock was set once at the factory — so time()
    // is plausible. ScummVM stamps save games with it.
    if (bOK) m_Timer.SetTime(BuildEpoch(), FALSE /* universal */);
    if (bOK) bOK = m_EMMC.Initialize();
    if (bOK) bOK = (f_mount(&m_FileSystem, "SD:", 1) == FR_OK);
    if (bOK) bOK = m_Console.Initialize();
    if (bOK) CGlueStdioInit(m_Console);

    // USB, here and not in the game's SDL_Init.
    //
    // Enumeration is slow, interrupt-driven and free to take as long as it
    // needs — right here, on core 0, with the whole machine to itself and
    // nothing yet depending on it answering. That is exactly what it is NOT
    // once the split is armed: from then on core 0's servo is the only thing
    // answering the other cores, and a long call inside it stops everything.
    //
    // Not fatal. A board with no working USB still runs the game, just with
    // no keyboard and no pad, and that is worth saying rather than dying for.
    if (bOK && !m_USB.Initialize())
        m_Logger.Write(From, LogWarning,
                       "USB did not come up — the game will run without a "
                       "keyboard or a game pad");

    // Core 0 runs application and library code like any other core, so it
    // arms itself too — before the secondary cores start, and before the
    // first thing that can throw.
    if (bOK) SDL2Circle_ArmCoreRuntime();

    // Start the secondary cores last: the world they are about to work in
    // has to be complete first — the card mounted, stdio wired — because
    // core 0 will be busy serving them from the moment they run. They park
    // in CSplitCores::Run until Run() below arms the split and opens the
    // gate.
    if (bOK) bOK = m_Cores.Initialize();
    return bOK;
}

TShutdownMode CKernel::Run(void)
{
    m_Logger.Write(From, LogNotice, "starting ScummVM");

    // Geometry evidence belongs on serial: what boot config handed us, read
    // next to the shim's framebuffer-grant line when the window is created.
    // This is the PHYSICAL request only — the mode asked of the firmware.
    // The card asks for no size, so this prints 0x0 and the panel keeps its
    // own mode. It never sets what the program is given: that is the
    // declaration below.
    m_Logger.Write(From, LogNotice, "boot config geometry: %ux%u",
                   m_Options.GetWidth(), m_Options.GetHeight());

    // The VIRTUAL display ScummVM is given, declared before anything asks
    // the library about the display. Every SDL answer ScummVM gets — the
    // current mode, the window, the renderer's output size — is this,
    // whatever the panel is really scanning, and the library carries each
    // frame from here to there in one pass on the presentation core.
    //
    // 640x480 is a SCUMM game's 320x200 raster with its aspect ratio
    // corrected to 320x240 and then doubled, which is what the baked
    // --scale-factor=2 asks ScummVM to produce. It is also exactly the size
    // the version 7 and 8 games draw at on their own. The two numbers are a
    // pair: change one and the other has to move with it, or the shim spends
    // a pass a frame rescaling what already fitted.
    //
    // The library has no default and no fallback: without this it refuses to
    // start.
    static const int VIRTUAL_WIDTH  = 640;
    static const int VIRTUAL_HEIGHT = 480;
    if (SDL2Circle_DeclareVirtualDevice(32, VIRTUAL_WIDTH, VIRTUAL_HEIGHT) != 0)
    {
        m_Logger.Write(From, LogError, "virtual display %dx%d refused: %s",
                       VIRTUAL_WIDTH, VIRTUAL_HEIGHT, SDL_GetError());
        return ShutdownHalt;
    }
    m_Logger.Write(From, LogNotice, "virtual display declared: %dx%d at 32bpp",
                   VIRTUAL_WIDTH, VIRTUAL_HEIGHT);

    // Render throughput lives and dies by the ARM and core clocks. The shim
    // owns the class that manages them, so the readings come from the shim;
    // this kernel never makes a CCPUThrottle of its own, because Circle
    // allows exactly one and a second stops the board. Above the socmaxtemp
    // limit in the card's cmdline.txt the clock is pulled back to idle — or,
    // where that file also names a fan pin with gpiofanpin=, the fan is
    // switched on instead and the clock is left alone.
    m_Logger.Write(From, LogNotice,
                   "SoC: %uC, arm %u MHz, core %u MHz, socmaxtemp %uC",
                   SDL2Circle_SoCTemperature(),
                   SDL2Circle_CPUClockRate() / 1000000,
                   CMachineInfo::Get()->GetClockRate(CLOCK_ID_CORE) / 1000000,
                   CKernelOptions::Get()->GetSoCMaxTemp());

    // Build the program's argument list before anything else looks at it:
    // the baked arguments, plus whatever a pre-boot writer stamped into the
    // image. Kernel switches are taken out here and never reach ScummVM.
    s_FinalArgc = DefaultsBuildArgv(ScummVMArgv,
                                    sizeof(ScummVMArgv) / sizeof(ScummVMArgv[0]),
                                    s_FinalArgv,
                                    sizeof(s_FinalArgv) / sizeof(s_FinalArgv[0]));

    // Move into ScummVM's own directory before it runs, so every relative
    // path it opens lands there and never in the card's root. One card
    // carries several games, and two of them writing a configuration file
    // into the root would each silently overwrite the other's.
    //
    // Done here, on core 0, before the application core is released: the
    // working directory is one global, so setting it here covers the
    // application core too.
    // A failure is worth saying out loud but is not fatal — ScummVM will
    // simply look where DATA_PATH and its own configuration point instead.
    if (chdir(RAPI_GAME_DIR) != 0)
        m_Logger.Write(From, LogWarning,
                       "could not enter " RAPI_GAME_DIR
                       " — relative paths will resolve at the card root");

    // Serial key injection, if the block asked for it.
    if (rapi_debug_uart)
    {
        m_Logger.Write(From, LogNotice,
                       "serial key injection armed (--rapi-debug-uart)");
    }

    // Performance reports — one serial line every N seconds, frame rate then
    // the cycle split — come from the library. Nothing to wire here: the
    // defaults block's `--rapi-perf=N` was consumed above, which is where
    // SDL2Circle_SetPerfInterval gets called (see defaults.cpp).

    int res;
    m_Logger.Write(From, LogNotice,
                   "core split: hardware core 0, application core 1, presentation core 2");

    // Arm the split before the application's first instruction: the
    // servo and watchdog on core 0, and the mailboxes every marshalled
    // call rides. Then open the gate.
    SDL2Circle_SplitInit();
    s_AppGate.store(1, std::memory_order_release);
    PublishToOtherCores();

    // Core 0's idle loop for the whole run. Yielding is not politeness
    // here: the servo task is what answers the application core, feeds
    // the sound device and pumps USB, and it only runs when this loop
    // gives it the core.
    while (!s_AppDone.load(std::memory_order_acquire))
        m_Scheduler.Yield();
    res = s_AppResult;

    // Park instead of rebooting. A reboot stops the clocks with the UART
    // FIFO still draining, so the exit line reaches the bench truncated —
    // and it destroys the machine state worth inspecting. The board sits
    // here until power-cycled.
    m_Logger.Write(From, LogNotice, "ScummVM exited with %d — parked", res);
    for (;;)
    {
        m_Timer.MsDelay(1000);
    }
}

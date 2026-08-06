//
// circle_stubs.cpp — the odd SDL2 entry points ScummVM references that
// circle-libsdl2 does not implement, and that are not about surfaces.
//
// Everything here either does the job properly or fails honestly — returns
// an error, returns null — so that nothing pretends to work. Where a
// function is a deliberate no-op it says why: on a bare-metal board with one
// fullscreen display and no desktop there is nothing for it to do.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the stub here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <SDL2/SDL.h>

extern "C" {

// ---------------------------------------------------------------------------
// Mutexes and condition variables
// ---------------------------------------------------------------------------
//
// ScummVM guards its mixer and its timer manager with these and reaches them
// on every audio callback. There are no threads on this board and nothing
// contends for them, but a lock that quietly did nothing would be the wrong
// thing to leave behind for the day something does, so the mutexes are real:
// a test-and-set over one word, which costs almost nothing.

struct SDL_mutex { volatile int held; };
struct SDL_cond  { volatile unsigned signalled; };

SDL_mutex *SDL_CreateMutex(void) {
	SDL_mutex *m = (SDL_mutex *)calloc(1, sizeof(SDL_mutex));
	if (m == nullptr)
		SDL_SetError("out of memory allocating mutex");
	return m;
}

void SDL_DestroyMutex(SDL_mutex *mutex) { free(mutex); }

int SDL_LockMutex(SDL_mutex *mutex) {
	if (mutex == nullptr)
		return -1;
	while (__atomic_exchange_n(&mutex->held, 1, __ATOMIC_ACQUIRE) != 0)
		asm volatile("yield" ::: "memory");
	return 0;
}

int SDL_UnlockMutex(SDL_mutex *mutex) {
	if (mutex == nullptr)
		return -1;
	__atomic_store_n(&mutex->held, 0, __ATOMIC_RELEASE);
	return 0;
}

int SDL_TryLockMutex(SDL_mutex *mutex) {
	if (mutex == nullptr)
		return -1;
	return __atomic_exchange_n(&mutex->held, 1, __ATOMIC_ACQUIRE) == 0 ? 0 : SDL_MUTEX_TIMEDOUT;
}

SDL_cond *SDL_CreateCond(void) {
	SDL_cond *c = (SDL_cond *)calloc(1, sizeof(SDL_cond));
	if (c == nullptr)
		SDL_SetError("out of memory allocating condition variable");
	return c;
}

void SDL_DestroyCond(SDL_cond *cond) { free(cond); }

int SDL_CondSignal(SDL_cond *cond) {
	if (cond == nullptr)
		return -1;
	__atomic_fetch_add(&cond->signalled, 1, __ATOMIC_RELEASE);
	return 0;
}

int SDL_CondBroadcast(SDL_cond *cond) { return SDL_CondSignal(cond); }

// A wait with nothing that can signal it is a hang, and a hang on a board
// with no console is indistinguishable from a crash. The only signaller
// would be a thread, and this port creates none, so an unsignalled wait
// returns an error instead of never returning at all.
int SDL_CondWait(SDL_cond *cond, SDL_mutex *mutex) {
	if (cond == nullptr || mutex == nullptr)
		return -1;
	if (__atomic_load_n(&cond->signalled, __ATOMIC_ACQUIRE) > 0) {
		__atomic_fetch_sub(&cond->signalled, 1, __ATOMIC_ACQUIRE);
		return 0;
	}
	SDL_SetError("SDL_CondWait: nothing can signal this condition variable");
	return -1;
}

// ---------------------------------------------------------------------------
// Window and renderer calls with nothing to do on this board
// ---------------------------------------------------------------------------
//
// The display is one fullscreen panel that the host kernel declared before
// the program started. Its size, its position and its scaling are settled
// before any of these can be called, so each answers success and changes
// nothing — which is the truth, not a pretence.

int  SDL_SetWindowFullscreen(SDL_Window *, Uint32) { return 0; }
void SDL_SetWindowSize(SDL_Window *, int, int) {}
void SDL_SetWindowMinimumSize(SDL_Window *, int, int) {}
void SDL_SetWindowMaximumSize(SDL_Window *, int, int) {}
void SDL_SetWindowPosition(SDL_Window *, int, int) {}
void SDL_SetWindowResizable(SDL_Window *, SDL_bool) {}
void SDL_SetWindowIcon(SDL_Window *, SDL_Surface *) {}
void SDL_MaximizeWindow(SDL_Window *) {}
void SDL_MinimizeWindow(SDL_Window *) {}
void SDL_RestoreWindow(SDL_Window *) {}
void SDL_SetWindowGrab(SDL_Window *, SDL_bool) {}
int  SDL_SetWindowMouseRect(SDL_Window *, const SDL_Rect *) { return 0; }
int  SDL_SetWindowDisplayMode(SDL_Window *, const SDL_DisplayMode *) { return 0; }
int  SDL_RenderSetLogicalSize(SDL_Renderer *, int, int) { return 0; }
int  SDL_RenderSetIntegerScale(SDL_Renderer *, SDL_bool) { return 0; }

// The window's position is the corner of the only screen there is.
void SDL_GetWindowPosition(SDL_Window *, int *x, int *y) {
	if (x != nullptr) *x = 0;
	if (y != nullptr) *y = 0;
}

// No window decoration, so no border to measure.
int SDL_GetWindowBordersSize(SDL_Window *, int *top, int *left,
                             int *bottom, int *right) {
	if (top    != nullptr) *top    = 0;
	if (left   != nullptr) *left   = 0;
	if (bottom != nullptr) *bottom = 0;
	if (right  != nullptr) *right  = 0;
	return 0;
}

// The panel's physical size is not something the firmware reports, so there
// is no honest number of dots per inch to give. Reporting failure is what
// makes ScummVM fall back to its own default scale rather than believe a
// figure this port invented.
int SDL_GetDisplayDPI(int, float *, float *, float *) {
	SDL_SetError("the display does not report its physical size");
	return -1;
}

// Screen savers belong to a desktop. There is none, and the display never
// blanks itself.
void SDL_DisableScreenSaver(void) {}
void SDL_EnableScreenSaver(void) {}

// Rotated and mirrored drawing. The shim draws upright, so the one case it
// can serve is served and every other one is refused rather than silently
// drawn the wrong way up. ScummVM only asks for a rotation when the user
// sets one, and this port declares no rotated modes.
int SDL_RenderCopyEx(SDL_Renderer *renderer, SDL_Texture *texture,
                     const SDL_Rect *srcrect, const SDL_Rect *dstrect,
                     const double angle, const SDL_Point *center,
                     const SDL_RendererFlip flip) {
	(void)center;
	if (angle != 0.0 || flip != SDL_FLIP_NONE) {
		SDL_SetError("rotated and mirrored drawing are not implemented");
		return -1;
	}
	return SDL_RenderCopy(renderer, texture, srcrect, dstrect);
}

// The window always holds the pointer and always has it confined to itself,
// because there is nowhere else on this machine for a pointer to be.
SDL_bool SDL_GetWindowGrab(SDL_Window *) { return SDL_TRUE; }

// The drawable is the window, at one pixel per pixel. There is no display
// scaling here and no high-density panel to account for.
void SDL_GL_GetDrawableSize(SDL_Window *window, int *w, int *h) {
	SDL_GetWindowSize(window, w, h);
}

// The window-manager handle. There is no window manager, so there is no
// handle, and saying so is what stops a caller acting on a structure this
// port never filled in.
SDL_bool SDL_GetWindowWMInfo(SDL_Window *, struct SDL_SysWMinfo *) {
	SDL_SetError("there is no window manager on this machine");
	return SDL_FALSE;
}

// No touch screen. ScummVM asks so it can offer touch controls.
int SDL_GetNumTouchDevices(void) { return 0; }

// ---------------------------------------------------------------------------
// The audio device, under SDL's older names
// ---------------------------------------------------------------------------
//
// ScummVM's mixer opens the sound device with SDL_OpenAudio, which is SDL
// 1.2's call and which SDL 2 keeps as a shorthand for opening the default
// device and calling it device 1. circle-libsdl2 implements the device form
// only, so these three are that shorthand, written out.

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained) {
	return SDL_OpenAudioDevice(nullptr, 0, desired, obtained, 0) == 1 ? 0 : -1;
}

void SDL_PauseAudio(int pause_on) { SDL_PauseAudioDevice(1, pause_on); }

void SDL_CloseAudio(void) { SDL_CloseAudioDevice(1); }

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

// Looking into the queue without taking anything out of it. ScummVM does
// this in one place: after a key press it looks ahead for the text event
// that key produced, so that a key and its character arrive together.
//
// The shim's queue cannot be looked into — it has one call, and that call
// removes what it returns. So this answers "nothing queued", which is
// exactly the answer a keyboard producing no text input would give, and
// ScummVM's own fallback is to build the character from the key code. The
// visible difference is on layouts where a character needs more than one
// key, which no keyboard reaching this board has.
int SDL_PeepEvents(SDL_Event *, int, SDL_eventaction, Uint32, Uint32) {
	return 0;
}

// ScummVM blocks here in a few dialogs. Polling and yielding keeps the servo
// on core 0 running, which is what delivers the event this is waiting for.
int SDL_WaitEvent(SDL_Event *event) {
	for (;;) {
		if (SDL_PollEvent(event))
			return 1;
		SDL_Delay(5);
	}
}

// ---------------------------------------------------------------------------
// The desktop this board does not have
// ---------------------------------------------------------------------------

// There is no clipboard, because there is nothing else running to share one
// with. Saying so plainly is what stops ScummVM offering a paste that could
// never produce anything.
SDL_bool SDL_HasClipboardText(void) { return SDL_FALSE; }

char *SDL_GetClipboardText(void) {
	char *empty = (char *)SDL_malloc(1);
	if (empty != nullptr)
		empty[0] = '\0';
	return empty;
}

int SDL_SetClipboardText(const char *) {
	SDL_SetError("there is no clipboard on this machine");
	return -1;
}

// No browser, no other program at all.
int SDL_OpenURL(const char *) {
	SDL_SetError("there is no browser on this machine");
	return -1;
}

// There is no window manager to put a dialog on top of, so the message goes
// where every other diagnostic goes: the serial console.
int SDL_ShowSimpleMessageBox(Uint32, const char *title, const char *message,
                             SDL_Window *) {
	printf("%s: %s\n", title != nullptr ? title : "message",
	       message != nullptr ? message : "");
	return 0;
}

// The machine has no locale and no user to have set one. An empty list is
// what SDL returns when it cannot tell, and ScummVM then uses its built-in
// default, which is English.
SDL_Locale *SDL_GetPreferredLocales(void) { return nullptr; }

// ---------------------------------------------------------------------------
// Processor features
// ---------------------------------------------------------------------------
//
// ScummVM asks these to decide whether to dispatch to a vector
// implementation. This build compiles none of them in (see svmgen/config.h),
// so the answer that matches the binary is no — whatever the silicon can
// actually do.

SDL_bool SDL_HasSSE2(void)    { return SDL_FALSE; }
SDL_bool SDL_HasSSE41(void)   { return SDL_FALSE; }
SDL_bool SDL_HasAVX2(void)    { return SDL_FALSE; }
SDL_bool SDL_HasAltiVec(void) { return SDL_FALSE; }
SDL_bool SDL_HasNEON(void)    { return SDL_FALSE; }

// ---------------------------------------------------------------------------
// Odds and ends
// ---------------------------------------------------------------------------

// SDL2 publishes the C library under its own names so that an application
// can call them on a platform with no C library at all. The shim provides
// the allocator half; there is a C library here, and it is the one SDL would
// forward to anyway, so these are one line each.
void *SDL_memcpy(void *dst, const void *src, size_t len) { return memcpy(dst, src, len); }
void *SDL_memset(void *dst, int c, size_t len) { return memset(dst, c, len); }

char *SDL_strdup(const char *str) {
	if (str == nullptr)
		str = "";
	const size_t n = strlen(str) + 1;
	char *p = (char *)SDL_malloc(n);
	if (p != nullptr)
		memcpy(p, str, n);
	return p;
}

char *SDL_getenv(const char *name) { return getenv(name); }

// Where the program keeps its own files. On a desktop these are per-user
// directories; here they are the card directory this game owns, which is
// also where its data lives. The caller frees what it gets, so this hands
// back a fresh copy every time.
static char *DupGameDir(void) {
	static const char path[] = RAPI_GAME_DIR "/";
	char *copy = (char *)SDL_malloc(sizeof(path));
	if (copy != nullptr)
		memcpy(copy, path, sizeof(path));
	return copy;
}

char *SDL_GetBasePath(void) { return DupGameDir(); }
char *SDL_GetPrefPath(const char *, const char *) { return DupGameDir(); }

} // extern "C"

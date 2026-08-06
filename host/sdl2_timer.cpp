//
// sdl2_timer.cpp — SDL's callback timers, on a machine with one thread.
//
// ScummVM asks SDL for a 10 ms repeating timer and drives its whole timer
// manager from it: music tempo, engine housekeeping, anything that has to
// happen on a clock rather than on a frame. On a desktop SDL serves that
// from a thread of its own.
//
// THERE IS NO THREAD HERE. circle-libsdl2 runs the application on one core
// with no scheduler over it, so a timer cannot interrupt the program — it
// can only be looked at when the program comes back round. So this file
// keeps the timers and fires the due ones at the two moments the program is
// guaranteed to reach, and both are moments where it is safe to be called:
//
//   SDL_PollEvent   every trip round any ScummVM loop, at least once a frame
//   SDL_Delay       every wait, which is where the program spends the time
//                   it is not drawing
//
// Both are reached through the linker's --wrap, so the shim's own versions
// still do their work and this file only adds the servicing around them.
//
// WHAT THAT COSTS. A callback runs late whenever the program is inside a
// single long operation with no delay and no event poll in it — loading a
// room, decompressing a sound. It is never dropped, only deferred, and the
// interval is measured from when it actually ran, so a late tick does not
// make the next one early. For a 10 ms timekeeping tick that is the
// difference between a steady beat and one that stumbles at a scene change,
// and no more than that.
//
// REENTRANCY is refused outright. A callback that calls SDL_Delay would
// otherwise be re-entered from inside itself, and ScummVM's timer manager
// takes a lock the second entry would deadlock on.
//
#include <SDL2/SDL.h>

namespace {

// Enough for every timer ScummVM creates and room to spare: it makes one,
// and the mixer's own scheduling is done from the audio callback instead.
const int MAX_TIMERS = 8;

struct Timer {
	SDL_TimerID       id;         // 0 when the slot is free
	SDL_TimerCallback callback;
	void             *param;
	Uint64            due;        // milliseconds, on SDL's own clock
};

Timer s_timers[MAX_TIMERS];
SDL_TimerID s_nextID = 1;
bool s_inCallback = false;

// Fire every timer whose deadline has passed, and reschedule it by the
// interval its callback asked for. A callback returning zero cancels the
// timer, which is SDL's own rule.
void ServiceTimers() {
	if (s_inCallback)
		return;

	const Uint64 now = SDL_GetTicks64();

	s_inCallback = true;
	for (int i = 0; i < MAX_TIMERS; i++) {
		Timer &t = s_timers[i];
		if (t.id == 0 || now < t.due)
			continue;

		const Uint32 next = t.callback(0, t.param);
		if (next == 0)
			t.id = 0;
		else
			t.due = SDL_GetTicks64() + next;
	}
	s_inCallback = false;
}

} // namespace

extern "C" {

int  __real_SDL_PollEvent(SDL_Event *event);
void   __real_SDL_Delay(Uint32 ms);

SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_TimerCallback callback, void *param) {
	if (callback == nullptr) {
		SDL_SetError("SDL_AddTimer: no callback");
		return 0;
	}

	for (int i = 0; i < MAX_TIMERS; i++) {
		if (s_timers[i].id != 0)
			continue;

		s_timers[i].id       = s_nextID++;
		s_timers[i].callback = callback;
		s_timers[i].param    = param;
		s_timers[i].due      = SDL_GetTicks64() + interval;
		return s_timers[i].id;
	}

	SDL_SetError("SDL_AddTimer: no free timer slot");
	return 0;
}

SDL_bool SDL_RemoveTimer(SDL_TimerID id) {
	for (int i = 0; i < MAX_TIMERS; i++) {
		if (s_timers[i].id == id) {
			s_timers[i].id = 0;
			return SDL_TRUE;
		}
	}
	return SDL_FALSE;
}

// The two servicing points. Each does its own job first or last as suits it:
// events are serviced before the queue is read, so a timer that pushes an
// event has it seen on this call rather than the next; a delay is serviced
// after waiting, so the callback sees the time that has actually passed.

int __wrap_SDL_PollEvent(SDL_Event *event) {
	ServiceTimers();
	return __real_SDL_PollEvent(event);
}

void __wrap_SDL_Delay(Uint32 ms) {
	__real_SDL_Delay(ms);
	ServiceTimers();
}

} // extern "C"

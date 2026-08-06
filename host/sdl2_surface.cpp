//
// sdl2_surface.cpp — the SDL2 surface and texture calls ScummVM makes that
// circle-libsdl2 does not implement.
//
// The shim renders from textures alone: its SDL_Surface is a 32-bit staging
// buffer, its textures are ARGB8888, and nothing in it blits, converts or
// fills a surface. ScummVM needs all of that, in three depths at once, so
// the missing middle lives here — written against the shim's public API and
// nothing else.
//
// WHAT SCUMMVM DRAWS WITH, and why each depth is here:
//
//   8-bit paletted   the game screen. A SCUMM game, like every game of its
//                    era, draws into a buffer of palette indices, and the
//                    palette changes constantly.
//   16-bit RGB565    the composed screen. ScummVM's SDL 2 graphics manager
//                    scales the game screen into an RGB565 surface, then
//                    uploads that as a streaming texture, and both the
//                    surface format and the texture format are fixed in
//                    upstream's own source.
//   32-bit ARGB8888  the mouse cursor and the on-screen messages, which
//                    carry alpha.
//
// THE TEXTURE HALF OF THIS FILE exists because of the RGB565 above. The
// shim's textures are ARGB8888 and a request for anything else is refused,
// so a texture ScummVM asks for in RGB565 is created here as ARGB8888 and
// remembered — with the format its owner believes it has — in a small
// registry. SDL_UpdateTexture converts on the way in, SDL_QueryTexture
// answers with the format that was asked for, and SDL_DestroyTexture forgets
// it. From ScummVM's side nothing has happened; from the shim's side it only
// ever sees the one format it supports.
//
// Six functions REPLACE a library function rather than adding one, reached
// through the linker's --wrap (see WRAPPED_SDL in the Makefile) so the
// library's own versions stay in place and still do their work. Everything
// else here is an addition.
//
// These are seams, not permanent furniture. When the shim implements one of
// these for real, the way to adopt it is to DELETE the code here: the
// archive is linked whole, so a leftover stub becomes a duplicate-symbol
// error at link time rather than a silent winner over the real thing.
//
#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" {

SDL_Surface *__real_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask);
void __real_SDL_FreeSurface(SDL_Surface *surface);

SDL_Texture *__real_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                                      int access, int w, int h);
int  __real_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                              const void *pixels, int pitch);
int  __real_SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access,
                             int *w, int *h);
void __real_SDL_DestroyTexture(SDL_Texture *texture);

} // extern "C"

namespace {

// ---------------------------------------------------------------------------
// Surfaces this file owns
// ---------------------------------------------------------------------------
//
// A surface made here carries allocations the library knows nothing about —
// a palette, a heap pixel format, usually the pixels — so freeing it is this
// file's job too. Rather than guess from the surface's contents, every one
// made here is recorded, and the free path looks it up: found means ours and
// freed our way, not found means the library's and handed back to it.
//
// The colour key and the per-surface alpha live in the record as well. SDL
// keeps them inside a private blit map, which is a structure this
// implementation does not have and does not need.

struct OwnedSurface {
	SDL_Surface     *surface;
	SDL_PixelFormat *format;       // always heap, always ours
	SDL_Palette     *palette;      // null for the direct-colour formats
	bool             owns_pixels;  // false when the caller supplied them
	bool             keyed;
	Uint32           key;
	Uint8            alpha;        // 255 unless SDL_SetSurfaceAlphaMod said so
	OwnedSurface    *next;
};

OwnedSurface *s_owned = nullptr;

OwnedSurface *Find(SDL_Surface *surface) {
	for (OwnedSurface *o = s_owned; o != nullptr; o = o->next)
		if (o->surface == surface)
			return o;
	return nullptr;
}

// Fill in a heap pixel format for one of the three layouts this port needs.
// Depth 8 gets a palette; 16 is RGB565; 32 is ARGB8888, which is
// byte-for-byte what the shim's streaming textures take.
SDL_PixelFormat *MakeFormat(int depth) {
	SDL_PixelFormat *fmt = (SDL_PixelFormat *)calloc(1, sizeof(SDL_PixelFormat));
	if (fmt == nullptr)
		return nullptr;

	fmt->BitsPerPixel  = (Uint8)depth;
	fmt->BytesPerPixel = (Uint8)(depth / 8);
	fmt->refcount      = 1;

	switch (depth) {
	case 8:
		fmt->format = SDL_PIXELFORMAT_INDEX8;
		break;

	case 16:
		fmt->format = SDL_PIXELFORMAT_RGB565;
		fmt->Rmask  = 0xF800; fmt->Rshift = 11; fmt->Rloss = 3;
		fmt->Gmask  = 0x07E0; fmt->Gshift = 5;  fmt->Gloss = 2;
		fmt->Bmask  = 0x001F; fmt->Bshift = 0;  fmt->Bloss = 3;
		fmt->Aloss  = 8;
		break;

	default:
		fmt->format = SDL_PIXELFORMAT_ARGB8888;
		fmt->Rmask  = 0x00FF0000; fmt->Rshift = 16;
		fmt->Gmask  = 0x0000FF00; fmt->Gshift = 8;
		fmt->Bmask  = 0x000000FF; fmt->Bshift = 0;
		fmt->Amask  = 0xFF000000; fmt->Ashift = 24;
		break;
	}
	return fmt;
}

SDL_Palette *MakePalette() {
	SDL_Palette *pal = (SDL_Palette *)calloc(1, sizeof(SDL_Palette));
	if (pal == nullptr)
		return nullptr;

	pal->colors = (SDL_Color *)calloc(256, sizeof(SDL_Color));
	if (pal->colors == nullptr) {
		free(pal);
		return nullptr;
	}
	pal->ncolors  = 256;
	pal->refcount = 1;
	// Opaque black until the game sets a real palette. A palette with zero
	// alpha throughout would convert to a fully transparent picture and read
	// as "the game drew nothing".
	for (int i = 0; i < 256; i++)
		pal->colors[i].a = 0xFF;
	return pal;
}

// The one place a surface is built. A null pixel pointer with a non-zero
// pitch is legitimate and means the caller will point the surface at memory
// it locks elsewhere.
SDL_Surface *NewOwnedSurface(int width, int height, int depth,
                             void *pixels, int pitch) {
	if (width <= 0 || height <= 0
	    || (depth != 8 && depth != 16 && depth != 32)) {
		SDL_SetError("unsupported surface: %dx%d at %d bits", width, height, depth);
		return nullptr;
	}

	const bool prealloc = (pixels != nullptr);

	SDL_Surface     *surface = (SDL_Surface *)calloc(1, sizeof(SDL_Surface));
	OwnedSurface    *rec     = (OwnedSurface *)calloc(1, sizeof(OwnedSurface));
	SDL_PixelFormat *fmt     = MakeFormat(depth);
	SDL_Palette     *pal     = (depth == 8) ? MakePalette() : nullptr;

	if (surface == nullptr || rec == nullptr || fmt == nullptr
	    || (depth == 8 && pal == nullptr)) {
		if (pal != nullptr) { free(pal->colors); free(pal); }
		free(fmt);
		free(rec);
		free(surface);
		SDL_SetError("out of memory allocating surface");
		return nullptr;
	}

	fmt->palette = pal;

	if (pitch == 0)
		pitch = width * fmt->BytesPerPixel;

	if (!prealloc) {
		pixels = calloc(1, (size_t)pitch * height);
		if (pixels == nullptr) {
			if (pal != nullptr) { free(pal->colors); free(pal); }
			free(fmt);
			free(rec);
			free(surface);
			SDL_SetError("out of memory allocating surface pixels");
			return nullptr;
		}
	}

	surface->flags     = prealloc ? SDL_PREALLOC : 0;
	surface->format    = fmt;
	surface->w         = width;
	surface->h         = height;
	surface->pitch     = pitch;
	surface->pixels    = pixels;
	surface->clip_rect = SDL_Rect{ 0, 0, width, height };
	surface->refcount  = 1;

	rec->surface     = surface;
	rec->format      = fmt;
	rec->palette     = pal;
	rec->owns_pixels = !prealloc;
	rec->alpha       = 0xFF;
	rec->next        = s_owned;
	s_owned          = rec;

	return surface;
}

// The rectangle actually touched, once the caller's has been clipped to the
// surface. False when nothing is left.
bool ClipToSurface(SDL_Surface *s, const SDL_Rect *in, SDL_Rect *out) {
	SDL_Rect r = (in != nullptr) ? *in : SDL_Rect{ 0, 0, s->w, s->h };
	if (r.x < 0) { r.w += r.x; r.x = 0; }
	if (r.y < 0) { r.h += r.y; r.y = 0; }
	if (r.x + r.w > s->w) r.w = s->w - r.x;
	if (r.y + r.h > s->h) r.h = s->h - r.y;
	if (r.w <= 0 || r.h <= 0)
		return false;
	*out = r;
	return true;
}

// One pixel, from whatever the source surface holds, to 32-bit ARGB. This is
// the single place the three depths meet, so it is also the single place a
// colour-depth mistake can be made.
inline Uint32 ToARGB(const SDL_PixelFormat *fmt, Uint32 v) {
	switch (fmt->BytesPerPixel) {
	case 1: {
		const SDL_Palette *pal = fmt->palette;
		if (pal == nullptr || (int)v >= pal->ncolors)
			return 0xFF000000u;
		const SDL_Color &c = pal->colors[v];
		return ((Uint32)c.a << 24) | ((Uint32)c.r << 16)
		     | ((Uint32)c.g << 8)  | (Uint32)c.b;
	}
	case 2: {
		// RGB565, with each channel's low bits filled from its own high
		// bits so that full-scale stays full-scale.
		const Uint32 r = (v >> 11) & 0x1F;
		const Uint32 g = (v >> 5)  & 0x3F;
		const Uint32 b =  v        & 0x1F;
		return 0xFF000000u
		     | (((r << 3) | (r >> 2)) << 16)
		     | (((g << 2) | (g >> 4)) << 8)
		     |  ((b << 3) | (b >> 2));
	}
	default:
		// A 32-bit format with no alpha mask has no alpha channel, and the
		// byte where one would be holds whatever the last writer left. The
		// shim's own surfaces are exactly that, so the byte is replaced
		// rather than read: without this every blit from one of them would
		// find alpha zero and draw nothing at all.
		return (fmt->Amask != 0) ? v : (0xFF000000u | v);
	}
}

inline Uint32 FromARGB(const SDL_PixelFormat *fmt, Uint32 argb) {
	if (fmt->BytesPerPixel == 2)
		return (((argb >> 19) & 0x1F) << 11)
		     | (((argb >> 10) & 0x3F) << 5)
		     |  ((argb >> 3)  & 0x1F);
	return argb;
}

inline Uint32 ReadPixel(const Uint8 *p, int bpp) {
	switch (bpp) {
	case 1:  return *p;
	case 2:  return *(const Uint16 *)p;
	default: return *(const Uint32 *)p;
	}
}

inline void WritePixel(Uint8 *p, int bpp, Uint32 v) {
	switch (bpp) {
	case 1:  *p = (Uint8)v; break;
	case 2:  *(Uint16 *)p = (Uint16)v; break;
	default: *(Uint32 *)p = v; break;
	}
}

// ---------------------------------------------------------------------------
// Textures this file shadows
// ---------------------------------------------------------------------------
//
// Only the format is remembered. The pixels stay where the shim put them:
// this file converts into them on the way through and keeps no copy, so a
// screen-sized texture costs nothing here beyond the record.

struct ShadowTexture {
	SDL_Texture   *texture;
	Uint32         declared;   // what the caller asked for, and is told back
	int            w;
	int            h;
	ShadowTexture *next;
};

ShadowTexture *s_textures = nullptr;

ShadowTexture *FindTexture(SDL_Texture *texture) {
	for (ShadowTexture *t = s_textures; t != nullptr; t = t->next)
		if (t->texture == texture)
			return t;
	return nullptr;
}

} // namespace

extern "C" {

// ---- surface creation ------------------------------------------------------

// The library makes 32-bit surfaces; this adds the paletted and RGB565 ones
// and leaves everything else to the library.
SDL_Surface *__wrap_SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask) {
	if (depth == 8 || depth == 16)
		return NewOwnedSurface(width, height, depth, nullptr, 0);

	return __real_SDL_CreateRGBSurface(flags, width, height, depth,
	                                   Rmask, Gmask, Bmask, Amask);
}

void __wrap_SDL_FreeSurface(SDL_Surface *surface) {
	if (surface == nullptr)
		return;

	OwnedSurface **link = &s_owned;
	for (OwnedSurface *o = s_owned; o != nullptr; link = &o->next, o = o->next) {
		if (o->surface != surface)
			continue;

		if (--surface->refcount > 0)
			return;

		*link = o->next;
		if (o->palette != nullptr) {
			free(o->palette->colors);
			free(o->palette);
		}
		if (o->owns_pixels)
			free(surface->pixels);
		free(o->format);
		free(o);
		free(surface);
		return;
	}

	__real_SDL_FreeSurface(surface);
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width, int height,
                                            int depth, Uint32 format) {
	if (depth == 0)
		depth = SDL_BITSPERPIXEL(format);
	if (depth == 32)
		return __real_SDL_CreateRGBSurface(flags, width, height, 32,
		                                   0x00FF0000, 0x0000FF00,
		                                   0x000000FF, 0xFF000000);
	return NewOwnedSurface(width, height, depth, nullptr, 0);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                      int depth, int pitch, Uint32 Rmask,
                                      Uint32 Gmask, Uint32 Bmask, Uint32 Amask) {
	(void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
	return NewOwnedSurface(width, height, depth, pixels,
	                       pitch != 0 ? pitch : width * (depth / 8));
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormatFrom(void *pixels, int width,
                                                int height, int depth,
                                                int pitch, Uint32 format) {
	if (depth == 0)
		depth = SDL_BITSPERPIXEL(format);
	return NewOwnedSurface(width, height, depth, pixels,
	                       pitch != 0 ? pitch : width * (depth / 8));
}

// ---- palettes and pixel values ---------------------------------------------

int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                         int firstcolor, int ncolors) {
	if (palette == nullptr || colors == nullptr) {
		SDL_SetError("SDL_SetPaletteColors: no palette");
		return -1;
	}
	if (firstcolor < 0 || ncolors < 0 || firstcolor + ncolors > palette->ncolors) {
		SDL_SetError("SDL_SetPaletteColors: range outside the palette");
		return -1;
	}

	for (int i = 0; i < ncolors; i++) {
		SDL_Color c = colors[i];
		// ScummVM fills only r, g and b. A zero alpha here would convert
		// the whole picture to transparent.
		c.a = 0xFF;
		palette->colors[firstcolor + i] = c;
	}
	palette->version++;
	return 0;
}

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette) {
	if (surface == nullptr || surface->format == nullptr) {
		SDL_SetError("SDL_SetSurfacePalette: no surface");
		return -1;
	}
	surface->format->palette = palette;
	return 0;
}

Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b) {
	if (format == nullptr)
		return 0;
	if (format->BytesPerPixel == 1) {
		// Nothing here builds an index from a colour: the caller wants a
		// value it can write, and on a paletted surface only the palette
		// knows what that is. Black is the honest answer, and it is what
		// the one caller — a fill before anything is drawn — wants.
		return 0;
	}
	return ((Uint32)r >> format->Rloss) << format->Rshift
	     | ((Uint32)g >> format->Gloss) << format->Gshift
	     | ((Uint32)b >> format->Bloss) << format->Bshift
	     | format->Amask;
}

void SDL_GetRGB(Uint32 pixel, const SDL_PixelFormat *format,
                Uint8 *r, Uint8 *g, Uint8 *b) {
	const Uint32 argb = ToARGB(format, pixel);
	if (r != nullptr) *r = (Uint8)(argb >> 16);
	if (g != nullptr) *g = (Uint8)(argb >> 8);
	if (b != nullptr) *b = (Uint8)argb;
}

void SDL_GetRGBA(Uint32 pixel, const SDL_PixelFormat *format,
                 Uint8 *r, Uint8 *g, Uint8 *b, Uint8 *a) {
	const Uint32 argb = ToARGB(format, pixel);
	SDL_GetRGB(pixel, format, r, g, b);
	if (a != nullptr) *a = (Uint8)(argb >> 24);
}

// ---- locking, keying and blend state ---------------------------------------
//
// Nothing here is hardware memory or run-length encoded, so a lock is a
// formality. The state setters keep what they are given in the surface's own
// record, because SDL keeps it in a private structure this implementation
// does not have.

int  SDL_LockSurface(SDL_Surface *surface) {
	if (surface == nullptr) {
		SDL_SetError("SDL_LockSurface: no surface");
		return -1;
	}
	surface->locked++;
	return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface) {
	if (surface != nullptr && surface->locked > 0)
		surface->locked--;
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key) {
	OwnedSurface *o = (surface != nullptr) ? Find(surface) : nullptr;
	if (o == nullptr) {
		SDL_SetError("SDL_SetColorKey: not a surface this port owns");
		return -1;
	}
	o->keyed = (flag != 0);
	o->key   = key;
	return 0;
}

int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key) {
	OwnedSurface *o = (surface != nullptr) ? Find(surface) : nullptr;
	if (o == nullptr || !o->keyed) {
		SDL_SetError("SDL_GetColorKey: surface has no colour key");
		return -1;
	}
	if (key != nullptr)
		*key = o->key;
	return 0;
}

int SDL_SetSurfaceAlphaMod(SDL_Surface *surface, Uint8 alpha) {
	OwnedSurface *o = (surface != nullptr) ? Find(surface) : nullptr;
	if (o == nullptr) {
		SDL_SetError("SDL_SetSurfaceAlphaMod: not a surface this port owns");
		return -1;
	}
	o->alpha = alpha;
	return 0;
}

// Run-length encoding is an SDL optimisation for keyed blits, not a
// behaviour. This implementation does not encode, so the answer is that the
// request was received and the picture is unchanged, which is true.
int SDL_SetSurfaceRLE(SDL_Surface *, int) { return 0; }

// Every blit here is a source-over blend where the source has alpha and a
// straight copy where it does not, which is what ScummVM asks for in both
// modes it sets. There is no third behaviour to select.
int SDL_SetSurfaceBlendMode(SDL_Surface *, SDL_BlendMode) { return 0; }

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect) {
	if (surface == nullptr)
		return SDL_FALSE;
	SDL_Rect r;
	if (!ClipToSurface(surface, rect, &r)) {
		surface->clip_rect = SDL_Rect{ 0, 0, 0, 0 };
		return SDL_FALSE;
	}
	surface->clip_rect = r;
	return SDL_TRUE;
}

void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect) {
	if (surface != nullptr && rect != nullptr)
		*rect = surface->clip_rect;
}

// ---- filling and copying ---------------------------------------------------

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color) {
	if (dst == nullptr || dst->pixels == nullptr) {
		SDL_SetError("SDL_FillRect: no destination");
		return -1;
	}

	SDL_Rect r;
	if (!ClipToSurface(dst, rect, &r))
		return 0;

	const int bpp = dst->format->BytesPerPixel;
	for (int y = 0; y < r.h; y++) {
		Uint8 *row = (Uint8 *)dst->pixels + (size_t)(r.y + y) * dst->pitch
		             + (size_t)r.x * bpp;
		if (bpp == 1) {
			memset(row, (int)(color & 0xFF), (size_t)r.w);
		} else {
			for (int x = 0; x < r.w; x++)
				WritePixel(row + (size_t)x * bpp, bpp, color);
		}
	}
	return 0;
}

int SDL_FillRects(SDL_Surface *dst, const SDL_Rect *rects, int count,
                  Uint32 color) {
	for (int i = 0; i < count; i++)
		if (SDL_FillRect(dst, &rects[i], color) != 0)
			return -1;
	return 0;
}

// The blit. Every combination of the three depths is handled through one
// path: read a pixel, widen it to ARGB, narrow it to the destination. A
// same-format unkeyed opaque blit takes the row-memcpy shortcut, which is
// the case that happens every frame.
int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect) {
	if (src == nullptr || dst == nullptr
	    || src->pixels == nullptr || dst->pixels == nullptr) {
		SDL_SetError("SDL_UpperBlit: no source or no destination");
		return -1;
	}

	SDL_Rect s;
	if (!ClipToSurface(src, srcrect, &s))
		return 0;

	SDL_Rect d = { dstrect ? dstrect->x : 0, dstrect ? dstrect->y : 0, s.w, s.h };
	// Clipping the destination shrinks the source region by the same amount,
	// from the same edges, so the two stay in step.
	if (d.x < 0) { s.x -= d.x; s.w += d.x; d.w += d.x; d.x = 0; }
	if (d.y < 0) { s.y -= d.y; s.h += d.y; d.h += d.y; d.y = 0; }
	if (d.x + d.w > dst->w) { d.w = dst->w - d.x; s.w = d.w; }
	if (d.y + d.h > dst->h) { d.h = dst->h - d.y; s.h = d.h; }
	if (d.w <= 0 || d.h <= 0)
		return 0;

	const OwnedSurface *o = Find(src);
	const bool   keyed = (o != nullptr) && o->keyed;
	const Uint32 key   = (o != nullptr) ? o->key : 0;
	const Uint8  alpha = (o != nullptr) ? o->alpha : 0xFF;

	const int sbpp = src->format->BytesPerPixel;
	const int dbpp = dst->format->BytesPerPixel;

	// The row-copy shortcut, for a blit with nothing to decide per pixel:
	// same format, no colour key, fully opaque, and no alpha channel in the
	// source to blend with. This is the case that happens every frame.
	if (sbpp == dbpp && !keyed && alpha == 0xFF
	    && (sbpp != 4 || src->format->Amask == 0)) {
		for (int y = 0; y < d.h; y++) {
			const Uint8 *sp = (const Uint8 *)src->pixels
			                  + (size_t)(s.y + y) * src->pitch
			                  + (size_t)s.x * sbpp;
			Uint8 *dp = (Uint8 *)dst->pixels + (size_t)(d.y + y) * dst->pitch
			            + (size_t)d.x * dbpp;
			memcpy(dp, sp, (size_t)d.w * sbpp);
		}
		if (dstrect != nullptr)
			*dstrect = d;
		return 0;
	}

	for (int y = 0; y < d.h; y++) {
		const Uint8 *sp = (const Uint8 *)src->pixels
		                  + (size_t)(s.y + y) * src->pitch
		                  + (size_t)s.x * sbpp;
		Uint8 *dp = (Uint8 *)dst->pixels + (size_t)(d.y + y) * dst->pitch
		            + (size_t)d.x * dbpp;

		for (int x = 0; x < d.w; x++) {
			const Uint32 raw = ReadPixel(sp + (size_t)x * sbpp, sbpp);
			if (keyed && raw == key)
				continue;

			Uint32 argb = ToARGB(src->format, raw);
			Uint32 a    = (argb >> 24) * alpha / 255;
			if (a == 0)
				continue;

			if (a < 255) {
				// Source over destination, one channel at a time. Only the
				// cursor and the on-screen messages reach this.
				const Uint32 under = ToARGB(dst->format,
				                            ReadPixel(dp + (size_t)x * dbpp, dbpp));
				const Uint32 inv = 255 - a;
				const Uint32 r = (((argb >> 16) & 0xFF) * a + ((under >> 16) & 0xFF) * inv) / 255;
				const Uint32 g = (((argb >> 8)  & 0xFF) * a + ((under >> 8)  & 0xFF) * inv) / 255;
				const Uint32 b = ((argb & 0xFF) * a + (under & 0xFF) * inv) / 255;
				argb = 0xFF000000u | (r << 16) | (g << 8) | b;
			}

			WritePixel(dp + (size_t)x * dbpp, dbpp, FromARGB(dst->format, argb));
		}
	}

	if (dstrect != nullptr)
		*dstrect = d;
	return 0;
}

// SDL's contract for the lower blit is that the caller has already clipped,
// which is exactly what the upper blit does before it draws. The two are the
// same function here.
int SDL_LowerBlit(SDL_Surface *src, SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect) {
	return SDL_UpperBlit(src, srcrect, dst, dstrect);
}

// ---- textures --------------------------------------------------------------

SDL_Texture *__wrap_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                                      int access, int w, int h) {
	// The shim has one texture format. Anything else is created as that one
	// and remembered under the name its owner gave it.
	SDL_Texture *texture = __real_SDL_CreateTexture(renderer,
	                                                SDL_PIXELFORMAT_ARGB8888,
	                                                access, w, h);
	if (texture == nullptr)
		return nullptr;

	ShadowTexture *t = (ShadowTexture *)calloc(1, sizeof(ShadowTexture));
	if (t == nullptr) {
		__real_SDL_DestroyTexture(texture);
		SDL_SetError("out of memory recording texture format");
		return nullptr;
	}
	t->texture  = texture;
	t->declared = format;
	t->w        = w;
	t->h        = h;
	t->next     = s_textures;
	s_textures  = t;

	return texture;
}

int __wrap_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                             const void *pixels, int pitch) {
	const ShadowTexture *t = FindTexture(texture);
	if (t == nullptr || t->declared == SDL_PIXELFORMAT_ARGB8888)
		return __real_SDL_UpdateTexture(texture, rect, pixels, pitch);

	if (t->declared != SDL_PIXELFORMAT_RGB565) {
		SDL_SetError("texture format 0x%08x cannot be converted", t->declared);
		return -1;
	}

	const int x = (rect != nullptr) ? rect->x : 0;
	const int y = (rect != nullptr) ? rect->y : 0;
	const int w = (rect != nullptr) ? rect->w : t->w;
	const int h = (rect != nullptr) ? rect->h : t->h;
	if (w <= 0 || h <= 0)
		return 0;

	// One row of the picture at a time, so the widened copy is a row long
	// rather than a screen long. A whole 640x480 screen would be 1.2 MB of
	// scratch on a board where that is a real amount of memory.
	Uint32 *row = (Uint32 *)malloc((size_t)w * sizeof(Uint32));
	if (row == nullptr) {
		SDL_SetError("out of memory converting texture rows");
		return -1;
	}

	int rc = 0;
	for (int j = 0; j < h && rc == 0; j++) {
		const Uint16 *src = (const Uint16 *)((const Uint8 *)pixels
		                                     + (size_t)j * pitch);
		for (int i = 0; i < w; i++) {
			const Uint32 v = src[i];
			const Uint32 r = (v >> 11) & 0x1F;
			const Uint32 g = (v >> 5)  & 0x3F;
			const Uint32 b =  v        & 0x1F;
			row[i] = 0xFF000000u
			       | (((r << 3) | (r >> 2)) << 16)
			       | (((g << 2) | (g >> 4)) << 8)
			       |  ((b << 3) | (b >> 2));
		}
		const SDL_Rect line = { x, y + j, w, 1 };
		rc = __real_SDL_UpdateTexture(texture, &line, row, w * 4);
	}

	free(row);
	return rc;
}

int __wrap_SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access,
                            int *w, int *h) {
	const int rc = __real_SDL_QueryTexture(texture, format, access, w, h);
	const ShadowTexture *t = FindTexture(texture);
	if (rc == 0 && t != nullptr && format != nullptr)
		*format = t->declared;
	return rc;
}

void __wrap_SDL_DestroyTexture(SDL_Texture *texture) {
	ShadowTexture **link = &s_textures;
	for (ShadowTexture *t = s_textures; t != nullptr; link = &t->next, t = t->next) {
		if (t->texture == texture) {
			*link = t->next;
			free(t);
			break;
		}
	}
	__real_SDL_DestroyTexture(texture);
}

} // extern "C"

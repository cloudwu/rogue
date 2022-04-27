#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <string.h>

#include "SDL.h"
#include "vga8x16.h"
#include "charset_cp936.h"

#define FRAMESEC 1000
#define PIXELWIDTH 8
#define PIXELHEIGHT 16
#define TABSIZE 8
#define UNICACHE 1024

struct slot {
	uint16_t background;	// 565 RGB
	uint16_t color;		// 565 RGB
	uint32_t code:23;	// for unicode
	uint32_t rightpart:1;
	uint32_t layer:8;
};

struct sprite {
	struct sprite *prev;
	struct sprite *next;
	unsigned w;
	unsigned h;
	int x;
	int y;
	struct slot s[1];
};

struct unicode_cache {
	int unicode[UNICACHE];
	uint16_t index[UNICACHE];
};

struct context {
	SDL_Renderer *renderer;
	SDL_Window *window;
	SDL_Surface *surface;
	uint64_t tick;
	int frame;
	int fps;
	int x;
	int y;
	int width;
	int height;
	int w;
	int h;
	struct slot *s;
	struct sprite *spr;
	uint8_t layer[256];
	struct unicode_cache u;
};

static inline int
inthash(int p) {
	int h = (2654435761 * p) % UNICACHE;
	return h;
}

static int
search_cp437(int unicode) {
	static const uint16_t cp437_unicode[] = {
		0x00a0,0x00a1,0x00a2,0x00a3,0x00a5,0x00aa,0x00ab,0x00ac,0x00b0,0x00b1,0x00b2,0x00b5,0x00b7,0x00ba,0x00bb,0x00bc,
		0x00bd,0x00bf,0x00c4,0x00c5,0x00c6,0x00c7,0x00c9,0x00d1,0x00d6,0x00dc,0x00df,0x00e0,0x00e1,0x00e2,0x00e4,0x00e5,
		0x00e6,0x00e7,0x00e8,0x00e9,0x00ea,0x00eb,0x00ec,0x00ed,0x00ee,0x00ef,0x00f1,0x00f2,0x00f3,0x00f4,0x00f6,0x00f7,
		0x00f9,0x00fa,0x00fb,0x00fc,0x00ff,0x0192,0x0393,0x0398,0x03a3,0x03a6,0x03a9,0x03b1,0x03b4,0x03b5,0x03c0,0x03c3,
		0x03c4,0x03c6,0x207f,0x20a7,0x2219,0x221a,0x221e,0x2229,0x2248,0x2261,0x2264,0x2265,0x2310,0x2320,0x2321,0x2500,
		0x2502,0x250c,0x2510,0x2514,0x2518,0x251c,0x2524,0x252c,0x2534,0x253c,0x2550,0x2551,0x2552,0x2553,0x2554,0x2555,
		0x2556,0x2557,0x2558,0x2559,0x255a,0x255b,0x255c,0x255d,0x255e,0x255f,0x2560,0x2561,0x2562,0x2563,0x2564,0x2565,
		0x2566,0x2567,0x2568,0x2569,0x256a,0x256b,0x256c,0x2580,0x2584,0x2588,0x258c,0x2590,0x2591,0x2592,0x2593,0x25a0,
	};
	static const uint8_t cp437_index[] = {
		255,173,155,156,157,166,174,170,248,241,253,230,250,167,175,172,
		171,168,142,143,146,128,144,165,153,154,225,133,160,131,132,134,
		145,135,138,130,136,137,141,161,140,139,164,149,162,147,148,246,
		151,163,150,129,152,159,226,233,228,232,234,224,235,238,227,229,
		231,237,252,158,249,251,236,239,247,240,243,242,169,244,245,196,
		179,218,191,192,217,195,180,194,193,197,205,186,213,214,201,184,
		183,187,212,211,200,190,189,188,198,199,204,181,182,185,209,210,
		203,207,208,202,216,215,206,223,220,219,221,222,176,177,178,254,
	};
	int begin = 0;
	int end = sizeof(cp437_unicode) / sizeof(cp437_unicode[0]);
	while (begin < end) {
		int mid = (begin + end) / 2;
		uint16_t code = cp437_unicode[mid];
		if (code == unicode) {
			return cp437_index[mid];
		}
		else if (code < unicode)
			begin = mid + 1;
		else
			end = mid;
	}
	return -1;
}

static int
search_cp936(int unicode) {
	int begin = 0;
	int end = sizeof(unimap_cp936) / sizeof(unimap_cp936[0]);
	while (begin < end) {
		int mid = (begin + end) / 2;
		uint16_t code = unimap_cp936[mid];
		if (code == unicode)
			return mid;
		else if (code < unicode)
			begin = mid + 1;
		else
			end = mid;
	}
	return -1;
}

static int
unicode_index(struct context *ctx, int unicode) {
	if (unicode <= 127)
		return unicode;
	int slot = inthash(unicode);
	if (ctx->u.unicode[slot] != unicode) {
		ctx->u.unicode[slot] = unicode;
		ctx->u.index[slot] = 255;
		int code = search_cp437(unicode);
		if (code >= 0) {
			ctx->u.index[slot] = code;
		} else {
			code = search_cp936(unicode);
			if (code >= 0) {
				ctx->u.index[slot] = code + 256;
			}
		}
	}
	return ctx->u.index[slot];
}

static struct context *
getCtx(lua_State *L) {
	struct context * ctx = (struct context *)lua_touserdata(L, lua_upvalueindex(1));
	return ctx;
}

static int
get_int(lua_State *L, int idx, const char * name) {
	if (lua_getfield(L, idx, name) != LUA_TNUMBER) {
		luaL_error(L, "Can't get %s as number", name);
	}
	int isnum;
	int r = lua_tointegerx(L, -1, &isnum);
	if (!isnum)
		luaL_error(L, "Can't get %s as integer", name);
	lua_pop(L, 1);
	return r;
}

static uint32_t
is_enable(lua_State *L, int idx, const char * name) {
	lua_getfield(L, idx, name);
	int enable = lua_toboolean(L, -1);
	lua_pop(L, 1);
	return enable ? 0xffffffff : 0;
}

static void
init_surface(lua_State *L, struct context *ctx) {
	ctx->surface = SDL_CreateRGBSurface(0, ctx->width * PIXELWIDTH, ctx->height * PIXELHEIGHT, 24, 0, 0, 0, 0);
	if (ctx->surface == NULL) {
		luaL_error(L, "Create surface failed : %s", SDL_GetError());
	}
}

static void
init_slotbuffer(lua_State *L, struct context *ctx) {
	size_t sz =  ctx->width * ctx->height * sizeof(struct slot);
	struct slot * s = (struct slot *)lua_newuserdatauv(L, sz, 0);
	memset(s, 0, sz);
	ctx->s = s;
	lua_setiuservalue(L, lua_upvalueindex(1), 1);
	s->color = 0xffff;
}

static int
linit(lua_State *L) {
	struct context * ctx = getCtx(L);
	if (ctx->window != NULL)
		return luaL_error(L, "Already init");
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		return luaL_error(L, "Couldn't initialize SDL: %s\n", SDL_GetError());

	luaL_checktype(L, 1, LUA_TTABLE);

	int width = get_int(L, 1, "width");
	int height = get_int(L, 1, "height");

	SDL_Window *wnd = NULL;
	SDL_Renderer *r = NULL;

	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

	uint32_t flags = 0;

	flags |= is_enable(L, 1, "borderless") & SDL_WINDOW_BORDERLESS;
	flags |= is_enable(L, 1, "resizeable") & SDL_WINDOW_RESIZABLE;
	flags |= is_enable(L, 1, "fullscreen") & SDL_WINDOW_FULLSCREEN;

	if (SDL_CreateWindowAndRenderer(width * PIXELWIDTH, height * PIXELHEIGHT, flags, &wnd, &r)) {
        return luaL_error(L, "Couldn't create window and renderer: %s", SDL_GetError());
    }

	if (lua_getfield(L, 1, "title") == LUA_TSTRING) {
		const char * s = lua_tostring(L, -1);
		SDL_SetWindowTitle(wnd, s);
		lua_pop(L, 1);
	}

	ctx->renderer = r;
	ctx->window = wnd;
	ctx->tick = SDL_GetTicks64();
	ctx->frame = 0;
	ctx->fps = get_int(L, 1, "fps");
	ctx->width = width;
	ctx->height = height;
	ctx->surface = NULL;
	ctx->w = width * PIXELWIDTH;
	ctx->h = height * PIXELHEIGHT;

	init_surface(L, ctx);
	init_slotbuffer(L, ctx);

	return 0;
}

static inline void
color16to24(uint16_t c16, uint8_t c[3]) {
	c[2] = c16 >> 11;
	c[2] = (c[2] << 3) | (c[2] & 7);
	c[1] = c16 >> 5;
	c[1] = (c[1] << 2) | (c[1] & 3);
	c[0] = (c16 << 3) | (c16 & 3);
}

static inline void
draw_slot(uint8_t *p, struct slot *s, int pitch) {
	const uint8_t *g;
	int g_pitch;
	if (s->code <= 255) {
		g = &vga8x16[s->code * 16];
		g_pitch = 1;
	} else {
		int code = s->code - 256;
		g = &uni16x16_cp936[code * 32];
		if (s->rightpart)
			++g;
		g_pitch = 2;
	}
	uint8_t b[3], c[3];
	color16to24(s->background, b);
	color16to24(s->color, c);
	int i,j;
	for (i=0;i<16;i++) {
		uint8_t m = *g;
		for (j=0;j<8;j++) {
			uint8_t *color = (m & 0x80) ? c : b;
			p[j*3+0] = color[0];
			p[j*3+1] = color[1];
			p[j*3+2] = color[2];
			m <<= 1;
		}
		p+=pitch;
		g+=g_pitch;
	}
}

static void
flush_slotbuffer(uint8_t *p, struct slot *s, int w, int h ) {
	int i,j;
	int pitch = w * PIXELWIDTH * 3;
	for (i=0;i<h;i++) {
		for (j=0;j<w;j++) {
			draw_slot(p + j * PIXELWIDTH * 3, &s[j], pitch);
		}
		p += pitch * PIXELHEIGHT;
		s += w;
	}
}

static inline void
draw_sprite(struct context *ctx, struct sprite *spr) {
	int src_x = 0;
	int src_y = 0;
	int des_x = spr->x - ctx->x;
	int des_y = spr->y - ctx->y;
	int w = spr->w;
	int h = spr->h;
	if (des_x < 0) {
		src_x -= des_x;
		if (src_x >= w)
			return;
		des_x = 0;
		w -= src_x;
	} else if (des_x >= ctx->width) {
		return;
	}
	if (des_x + w > ctx->width) {
		w =  ctx->width - des_x;
	}
	if (des_y < 0) {
		src_y -= des_y;
		if (src_y >= h)
			return;
		des_y = 0;
		h -= src_y;
	} else if (des_y >= ctx->height) {
		return;
	}
	if (des_y + h > ctx->height) {
		h = ctx->height - des_y;
	}
	int i,j;
	struct slot *src_slot = &spr->s[src_y * spr->w + src_x];
	struct slot *des_slot = &ctx->s[des_y * ctx->width + des_x];
	for (i=0;i<h;i++) {
		for (j=0;j<w;j++) {
			if (ctx->layer[src_slot[j].layer] == 0 && src_slot[j].code && src_slot[j].layer >= des_slot[j].layer) {
				des_slot[j] = src_slot[j];
			}
		}
		src_slot += spr->w;
		des_slot += ctx->width;
	}
}

static void
draw_sprites(struct context *ctx) {
	struct sprite * spr = ctx->spr;
	if (spr == NULL)
		return;
	do {
		draw_sprite(ctx, spr);
		spr = spr->next;
	} while (spr != ctx->spr);
}

static void
flip_surface(struct context *ctx) {
	SDL_Surface *ws = SDL_GetWindowSurface(ctx->window);
	int w = ctx->width * PIXELWIDTH;
	int h = ctx->height * PIXELHEIGHT;

	draw_sprites(ctx);

	SDL_LockSurface(ctx->surface);
	flush_slotbuffer(ctx->surface->pixels, ctx->s, ctx->width, ctx->height);
	SDL_UnlockSurface(ctx->surface);
	memset(ctx->s, 0, sizeof(struct slot) * ctx->width * ctx->height);

	if (ws->w == w && ws->h == h) {
		SDL_BlitSurface(ctx->surface, NULL, ws, NULL);
	} else {
		SDL_BlitScaled(ctx->surface, NULL, ws, NULL);
	}
	SDL_UpdateWindowSurface(ctx->window);
}

static int
lframe(lua_State *L) {
	struct context * ctx = getCtx(L);
	if (ctx->surface == NULL)
		return luaL_error(L, "Init first");
	ctx->x = luaL_optinteger(L, 1, 0);
	ctx->y = luaL_optinteger(L, 2, 0);
	flip_surface(ctx);
	uint64_t c = SDL_GetTicks64();
	int lastframe = ctx->frame;
	int frame = lastframe + 1;
	if (frame > ctx->fps) {
		lastframe = 0;
		frame = 1;
	}
	int delta = FRAMESEC * frame / ctx->fps - FRAMESEC * lastframe / ctx->fps;
	ctx->frame = frame;
	ctx->tick += delta;
	if (c < ctx->tick)
		SDL_Delay(ctx->tick - c);
	if (c - ctx->tick > FRAMESEC) {
		// reset frame count if the error is too large
		ctx->tick = c;
		ctx->frame = 0;
	}

	return 0;
}

static void
resize_window(struct context *ctx, int width, int height) {
	int ow = ctx->width * PIXELWIDTH;
	int oh = ctx->height * PIXELHEIGHT;
	if (ow * height == oh * width) {
		ctx->w = width;
		ctx->h = height;
		return;
	}
	if (width > ctx->w) {
		// enlarge width
		height = oh * width / ow;
	} else if (height > ctx->h) {
		// enlarge height
		width = ow * height / oh;
	} else if (width < ctx->w) {
		// shrink width
		height = oh * width / ow;
	} else {
		// shrink height
		width = ow * height / oh;
	}

	ctx->w = width;
	ctx->h = height;

	SDL_SetWindowSize(ctx->window, width, height);
}

static void
winevent(lua_State *L, SDL_Event *ev) {
	struct context * ctx = getCtx(L);
	switch (ev->window.event) {
		case SDL_WINDOWEVENT_SIZE_CHANGED :
			resize_window(ctx, ev->window.data1, ev->window.data2);
			break;
	}
}

static int
keyevent(lua_State *L, SDL_Event *ev) {
//	if (ev->key.repeat)
//		return 0;
	lua_pushstring(L, "KEY");
	lua_pushstring(L, SDL_GetKeyName(ev->key.keysym.sym));
	lua_pushboolean(L, ev->key.type == SDL_KEYDOWN);
	return 3;
}

static int
levent(lua_State *L) {
	SDL_Event event;

	int r;

	while (SDL_PollEvent(&event)) {
		switch (event.type)	{
			case SDL_QUIT:
				lua_pushstring(L, "QUIT");
				return 1;
			case SDL_WINDOWEVENT:
				winevent(L, &event);
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:
				if ((r = keyevent(L, &event)) > 0)
					return r;
				break;
			default:
				break;
		}
	}
	return 0;
}

static struct sprite *
getSpr(lua_State *L) {
	struct sprite *spr = lua_touserdata(L, 1);
	if (spr == NULL)
		luaL_error(L, "Need sprite");
	return spr;
}


static int
lsetpos(lua_State *L) {
	struct sprite *spr = getSpr(L);
	spr->x = luaL_checkinteger(L, 2);
	spr->y = luaL_checkinteger(L, 3);
	return 0;
}

static void
link_sprite(struct context *ctx, struct sprite *spr) {
	struct sprite * node = ctx->spr;
	if (node == NULL) {
		spr->prev = spr->next = spr;
	} else {
		// insert before node
		spr->next = node;
		spr->prev = node->prev;

		node->prev = spr;
		spr->prev->next = spr;
	}
	ctx->spr = spr;
}

static void
unlink_sprite(struct context *ctx, struct sprite *spr) {
	if (ctx->spr == spr) {
		if (spr->prev == spr->next) {
			ctx->spr = NULL;
		} else {
			ctx->spr = spr->next;
		}
	}
	struct sprite *prev = spr->prev;
	struct sprite *next = spr->next;
	prev->next = next;
	next->prev = prev;
	spr->prev = NULL;
	spr->next = NULL;
}

static int
lvisible(lua_State *L) {
	struct sprite *spr = getSpr(L);
	int visible = lua_toboolean(L, 2);
	if (visible) {
		if (spr->prev)
			return 0;	// already visible
		struct context *ctx = getCtx(L);
		link_sprite(ctx, spr);
	} else {
		if (spr->prev == NULL)
			return 0;	// already invisible
		struct context *ctx = getCtx(L);
		unlink_sprite(ctx, spr);
	}
	return 0;
}

static int
lspriteinfo(lua_State *L) {
	struct sprite *spr = lua_touserdata(L, 1);
	lua_pushfstring(L, "[sprite %dx%d %d %d]", spr->w, spr->h, spr->x, spr->y);

	return 1;
}

struct sprite_attribs {
	int transparency;
	uint16_t background;
	uint16_t color;
	uint8_t layer;
};

/*
**    From lua 5.4 lutf8lib.c
** Decode one UTF-8 sequence, returning NULL if byte sequence is
** invalid.  The array 'limits' stores the minimum value for each
** sequence length, to check for overlong representations. Its first
** entry forces an error for non-ascii bytes with no continuation
** bytes (count == 0).
*/
static const char *
utf8_decode(const char *s, int *val) {
  static const int limits[] =
        {~(int)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u};
  unsigned int c = (unsigned char)s[0];
  int res = 0;  /* final result */
  if (c < 0x80)  /* ascii? */
    res = c;
  else {
    int count = 0;  /* to count number of continuation bytes */
    for (; c & 0x40; c <<= 1) {  /* while it needs continuation bytes... */
      unsigned int cc = (unsigned char)s[++count];  /* read next byte */
      if ((cc & 0xC0) != 0x80)  /* not a continuation byte? */
        return NULL;  /* invalid byte sequence */
      res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
    }
    res |= ((int)(c & 0x7F) << (count * 5));  /* add first byte */
    if (count > 5 || res < limits[count])
      return NULL;  /* invalid byte sequence */
    s += count;  /* skip continuation bytes read */
  }
  if (val) *val = res;
  return s + 1;  /* +1 to include first byte */
}

static void
sprite_graph(lua_State *L, int idx, struct context *ctx, struct sprite *spr, struct sprite_attribs *attrib) {
	int i,j;
	struct slot *s = spr->s;
	for (i=0;i<spr->h;i++) {
		lua_geti(L, idx, i+1);
		const char * str = lua_tostring(L, -1);
		lua_pop(L, 1);
		for (j=0;j<spr->w;j++) {
			s[j].background = attrib->background;
			s[j].color = attrib->color;
			s[j].layer = attrib->layer;
			int unicode;
			if (!(str = utf8_decode(str, &unicode)))
				luaL_error(L, "Invalid utf8 text");
			int c = unicode_index(ctx, unicode);
			if (c == attrib->transparency)
				c = 0;
			s[j].code = c;
			s[j].rightpart = 0;
			if (c > 256) {
				s[j+1] = s[j];
				s[j+1].rightpart = 1;
				++j;
			}
		}
		s += spr->w;
	}
}

static inline uint16_t
color24to16(uint32_t c) {
	int r = (c >> 16) & 0xff;
	int g = (c >> 8) & 0xff;
	int b = c & 0xff;
	return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
}

static uint16_t
get_color(lua_State *L, int idx, const char *name, uint16_t def) {
	if (lua_getfield(L, idx, name) == LUA_TNIL) {
		lua_pop(L, 1);
		return def;
	}
	int isnum;
	uint32_t c = lua_tointegerx(L, -1, &isnum);
	if (!isnum)
		luaL_error(L, "Color should be uint32");
	lua_pop(L, 1);
	return color24to16(c);
}

static inline size_t
sprite_size(int w, int h) {
	return sizeof(struct sprite) + sizeof(struct slot) * (w * h -1);
}

static int
lclone(lua_State *L) {
	struct sprite *spr = getSpr(L);
	size_t sz = sprite_size(spr->w,spr->h);
	struct sprite *clone = (struct sprite *)lua_newuserdatauv(L, sz, 0);
	memcpy(clone, spr, sz);
	if (clone->prev) {
		clone->prev = NULL;
		clone->next = NULL;
		link_sprite(getCtx(L), clone);
	}
	luaL_getmetatable(L, "RSPRITE");
	lua_setmetatable(L, -2);
	return 1;
}

static void
reset_color(struct sprite *spr, uint16_t c) {
	int i,j;
	struct slot *s = spr->s;
	for (i=0;i<spr->h;i++) {
		for (j=0;j<spr->w;j++) {
			s[j].color = c;
		}
		s += spr->w;
	}
}

#define COLOR_UNINIT -2
#define COLOR_UNDEF -1

static int
lsetcolor(lua_State *L) {
	struct sprite *spr = getSpr(L);
	if (lua_type(L, 2) == LUA_TNUMBER) {
		int isnum;
		uint32_t c = lua_tointegerx(L, -1, &isnum);
		if (!isnum)
			return luaL_error(L, "Color should be uint32");
		uint16_t c16 = color24to16(c);
		reset_color(spr, c16);
		return 0;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	int pal[256];
	int i,j;
	for (i=0;i<256;i++) {
		pal[i] = COLOR_UNINIT;
	}
	struct slot *s = spr->s;
	for (i=0;i<spr->h;i++) {
		if (lua_geti(L, 2, i+1) != LUA_TSTRING) {
			return luaL_error(L, "Invalid colormap");
		}
		const char * str = lua_tostring(L, -1);
		lua_pop(L, 1);
		for (j=0;j<spr->w;j++) {
			int index = (uint8_t)str[j];
			if (index > 127 || index == 0)
				return luaL_error(L, "Invalid colormap, ascii only");
			if (pal[index] >= 0) {
				s[j].color = pal[index];
			} else if (pal[index] == COLOR_UNINIT) {
				char key[2] = { index, 0 };
				if (lua_getfield(L, 2, key) == LUA_TNIL) {
					pal[index] = COLOR_UNDEF;
				} else {
					int isnum;
					uint32_t c = lua_tointegerx(L, -1, &isnum);
					if (!isnum) {
						return luaL_error(L, "Pal .%c should be integer", index);
					}
					pal[index] = color24to16(c);
					s[j].color = pal[index];
				}
				lua_pop(L, 1);
			}
		}
		s += spr->w;
	}
	return 0;
}

static void
clear_sprite_text(struct sprite *spr) {
	int n = spr->w * spr->h;
	int i;
	for (i=0;i<n;i++) {
		spr->s[i].code = ' ';
		spr->s[i].rightpart = 0;
	}
}

static int
lsettext(lua_State *L) {
	struct context *ctx = getCtx(L);
	struct sprite * spr = getSpr(L);
	const char *text = luaL_checkstring(L, 2);
	int x=0;
	int y=0;
	struct slot *s = spr->s;
	int unicode = 0;
	clear_sprite_text(spr);
	while (y < spr->h && ((text = utf8_decode(text, &unicode)), unicode)) {
		if (text == NULL)
			return luaL_error(L, "Invalid UTF-8 string %s", lua_tostring(L, 2));
		int c = unicode_index(ctx, unicode);
		if (c == '\n') {
			x = spr->w;
		} else if (c == '\t') {
			x = (x / TABSIZE + 1) * TABSIZE;
		} else if (c <= 255) {
			s[x].code = c;
			++x;
		} else {
			if (x + 1 >= spr->w) {
				if (spr->w < 2)
					return luaL_error(L, "Invalid sprite width %d", spr->w);
				x = 0;
				++y;
				if (y <= spr->h)
					break;
				s += spr->w;
			}
			s[x].code = c;
			++x;
			s[x].code = c;
			s[x].rightpart = 1;
			++x;
		}
		if (x >= spr->w) {
			x = 0;
			++y;
			s += spr->w;
		}
	}
	return 0;
}

static int
get_sprite_width(lua_State *L, struct context *ctx, int idx, int line) {
	if (lua_geti(L, idx, line) != LUA_TSTRING) {
		luaL_error(L, "Invalid sprite");
	}
	const char * s = lua_tostring(L, -1);
	int len = 0;
	int code = 0;
	while ((s = utf8_decode(s, &code)) && code) {
		int c = unicode_index(ctx, code);
		if (c > 255)
			len += 2;
		else
			++len;
	}
	lua_pop(L, 1);
	return len;
}

static void
check_sprite_size(lua_State *L, struct context *ctx, int idx, int *w, int *h) {
	int lines = (int)lua_rawlen(L, idx);
	if (lines <= 0)
		luaL_error(L, "sprite height 0");
	*h = lines;
	int width = get_sprite_width(L, ctx, idx, 1);
	if (width <= 0)
		luaL_error(L, "sprite width 0");
	*w = width;
	int i;
	for (i=2;i<=lines;i++) {
		if (get_sprite_width(L, ctx, idx, i) != width) {
			luaL_error(L, "sprite is not a rect");
		}
	}
}

static int
lsprite(lua_State *L) {
	struct context *ctx = getCtx(L);
	luaL_checktype(L, 1, LUA_TTABLE);
	int w,h;
	check_sprite_size(L, ctx, 1, &w, &h);
	size_t sz = sprite_size(w,h);
	struct sprite *spr = (struct sprite *)lua_newuserdatauv(L, sz, 0);
	spr->w = w;
	spr->h = h;
	spr->x = 0;
	spr->y = 0;
	spr->prev = NULL;
	spr->next = NULL;
	struct sprite_attribs a;
	a.transparency = 0;
	if (lua_getfield(L, 1, "transparency") == LUA_TSTRING) {
		const char * t = lua_tostring(L, -1);
		a.transparency = (unsigned)*t;
		if (a.transparency > 127 || a.transparency == 0) {
			return luaL_error(L, "transparency is ascii only");
		}
	}
	lua_pop(L, 1);
	a.color = get_color(L, 1, "color", 0xffff);
	a.background = get_color(L, 1, "background", 0);
	a.layer = 0;
	if (lua_getfield(L, 1, "layer") == LUA_TNUMBER) {
		int layer = lua_tointeger(L, -1);
		if (layer < 0)
			layer = 0;
		else if (layer > 255)
			layer = 255;
		a.layer = layer;
	}
	lua_pop(L, 1);

	sprite_graph(L, 1, ctx, spr, &a);
	if (luaL_newmetatable(L, "RSPRITE")) {
		luaL_Reg l[] = {
			{ "setpos", lsetpos },
			{ "setcolor", lsetcolor },
			{ "clone", NULL },
			{ "visible", NULL },
			{ "__tostring", lspriteinfo },
			{ "__gc", NULL },
			{ "__index", NULL },
			{ NULL, NULL },
		};
		luaL_setfuncs(L, l, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");

		luaL_Reg l2[] = {
			{ "clone", lclone },
			{ "visible", lvisible },
			{ "text", lsettext },
			{ "__gc", lvisible },
			{ NULL, NULL },
		};

		lua_pushvalue(L, lua_upvalueindex(1));
		luaL_setfuncs(L, l2, 1);
	}
	lua_setmetatable(L, -2);
	link_sprite(ctx, spr);
	return 1;
}

static int
llayer(lua_State *L) {
	struct context * ctx = getCtx(L);
	luaL_checktype(L, 1, LUA_TTABLE);
	lua_pushnil(L);
	while (lua_next(L, 1) != 0) {
		int isnum;
		int layer = lua_tointegerx(L, -2, &isnum);
		if (isnum && layer >=0 && layer <=255) {
			int hide = lua_toboolean(L, -1);
			ctx->layer[layer] = !hide;
		}
		lua_pop(L, 1);
	}
	return 0;
}

LUAMOD_API int
luaopen_rogue_core(lua_State *L) {
	luaL_checkversion(L);
	luaL_Reg l[] = {
		{ "init", linit },
		{ "frame", lframe },
		{ "event", levent },
		{ "sprite", lsprite },
		{ "layer", llayer },
		{ NULL, NULL },
	};
	luaL_newlibtable(L, l);
	struct context *ctx = (struct context *)lua_newuserdatauv(L, sizeof(struct context), 1);
	memset(ctx, 0, sizeof(*ctx));
	luaL_setfuncs(L,l,1);
	return 1;
}

#define LUA_LIB

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <string.h>

#include "SDL.h"
#include "vga8x16.h"

#define FRAMESEC 1000
#define PIXELWIDTH 8
#define PIXELHEIGHT 16
#define TABSIZE 8

struct slot {
	uint16_t background;	// 565 RGB
	uint16_t color;		// 565 RGB
	uint32_t code:24;	// for unicode
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
};

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
	if (s->code > 255)
		return;
	const uint8_t *g = &vga8x16[s->code * 16];
	uint8_t b[3], c[3];
	color16to24(s->background, b);
	color16to24(s->color, c);
	int i,j;
	for (i=0;i<16;i++) {
		uint8_t m = g[i];
		for (j=0;j<8;j++) {
			uint8_t *color = (m & 0x80) ? c : b;
			p[j*3+0] = color[0];
			p[j*3+1] = color[1];
			p[j*3+2] = color[2];
			m <<= 1;
		}
		p+=pitch;
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

static int
get_sprite_width(lua_State *L, int idx, int line) {
	if (lua_geti(L, idx, line) != LUA_TSTRING) {
		luaL_error(L, "Invalid sprite");
	}
	size_t sz;
	lua_tolstring(L, -1, &sz);
	lua_pop(L, 1);
	return (int)sz;
}

static void
check_sprite_size(lua_State *L, int idx, int *w, int *h) {
	int lines = (int)lua_rawlen(L, idx);
	if (lines <= 0)
		luaL_error(L, "sprite height 0");
	*h = lines;
	int width = get_sprite_width(L, idx, 1);
	if (width <= 0)
		luaL_error(L, "sprite width 0");
	*w = width;
	int i;
	for (i=2;i<=lines;i++) {
		if (get_sprite_width(L, idx, i) != width) {
			luaL_error(L, "sprite is not a rect");
		}
	}
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

static void
sprite_graph(lua_State *L, int idx, struct sprite *spr, struct sprite_attribs *attrib) {
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
			int c = (uint8_t)str[j];
			if (c == attrib->transparency)
				c = 0;
			s[j].code = c;
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
	int w,h;
	check_sprite_size(L, 2, &w, &h);
	if (w != spr->w || h != spr->h) {
		return luaL_error(L, "Sprite (%dx%d) size mismatch : (%dx%d)", spr->w, spr->h, w, h);
	}
	int pal[256];
	int i,j;
	for (i=0;i<256;i++) {
		pal[i] = COLOR_UNINIT;
	}
	struct slot *s = spr->s;
	for (i=0;i<spr->h;i++) {
		lua_geti(L, 2, i+1);
		const char * str = lua_tostring(L, -1);
		lua_pop(L, 1);
		for (j=0;j<spr->w;j++) {
			int index = (uint8_t)str[j];
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

static int
lsettext(lua_State *L) {
	struct sprite * spr = getSpr(L);
	const char *text = luaL_checkstring(L, 2);
	int x=0;
	int y=0;
	int i,j;
	struct slot *s = spr->s;
	for (i=0;text[i] && y < spr->h;i++) {
		char c = text[i];
		int nextline = 0;
		if (c == '\n') {
			nextline = 1;
		} else if (c == '\t') {
			x = (x / TABSIZE + 1) * TABSIZE;
			if (x >= spr->w) {
				nextline = 1;
			}
		} else {
			s[x].code = text[i];
			++x;
			if (x >= spr->w) {
				nextline = 1;
				x = 0;
			}
		}
		if (nextline) {
			for (j=x;j<spr->w;j++) {
				s[j].code = ' ';
			}
			++y;
			x = 0;
			s += spr->w;
		}
	}
	int n = spr->w * (spr->h - i);
	for (i=0;i<n;i++) {
		s[i].code = ' ';
	}
	return 0;
}

static int
lsprite(lua_State *L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int w,h;
	check_sprite_size(L, 1, &w, &h);
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

	sprite_graph(L, 1, spr, &a);
	if (luaL_newmetatable(L, "RSPRITE")) {
		luaL_Reg l[] = {
			{"setpos", lsetpos },
			{"setcolor", lsetcolor },
			{"clone", NULL },
			{"text", lsettext },
			{"visible", NULL },
			{"__tostring", lspriteinfo },
			{"__gc", NULL },
			{"__index", NULL },
			{ NULL, NULL },
		};
		luaL_setfuncs(L, l, 0);
		lua_pushvalue(L, -1);
		lua_setfield(L, -2, "__index");

		luaL_Reg l2[] = {
			{ "clone", lclone },
			{ "visible", lvisible },
			{ "__gc", lvisible },
			{ NULL, NULL },
		};

		lua_pushvalue(L, lua_upvalueindex(1));
		luaL_setfuncs(L, l2, 1);
	}
	lua_setmetatable(L, -2);
	link_sprite(getCtx(L), spr);
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

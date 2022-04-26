Lua Rogue
=========

A small lua library for roguelike games.

Build
=====

Compile rogue.c and link with lua54.dll and SDL2.dll.

You can download SDL2.dll from https://github.com/libsdl-org/SDL/releases, or build by yourself.

How to Use
==========

See test.lua

Init SDL first,

```lua
local c = require "rogue.core"

c.init {
	title = "测试",
	width = 80,
	height = 25,
	fps = 25,
	resizeable = true,
}
```

The mainloop could be

```lua
local function dispatch(name, ...)
	if name then
		return not EVENT[name](...)
	else
		return true
	end
end

while dispatch(c.event()) do
	-- do something
	c.frame()
end

```

The event can be "QUIT" and "KEY" .

About Sprite
============

The sprite is an ascii art rectangle. You can create a sprite by

```lua
local s = c.sprite {
	".-----.",
	"| ^ ^ |",
	"|  -  |",
	".-----.",
	color = 0xff0000,
	background = 0,
	transparency = '.',
	layer = 1,
}
```

The layer is from 0 to 255, the larger will cover the lower. If two sprites intersected with the same layer, the behaviour is undefined.

* sprite:clone()   Clone a sprite
* sprite:setpos(x,y) Move the sprite to (x,y)
* sprite:color(color) Change the color
* sprite:text(string) Replace the sprite with text.
* sprite:visible(true/false) Show/Hide the sprite

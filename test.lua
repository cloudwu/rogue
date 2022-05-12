assert(package.loadlib(assert(package.searchpath("SDL2", package.cpath)), "*"))

local c = require "rogue.core"

c.init {
	title = "测试",
	width = 80,
	height = 25,
	fps = 25,
	resizeable = true,
	scale = 1,
	software = true,
	vsync = true,
}

local background = c.sprite {
	[[..........................................]],
	[[..........................................]],
	[[.......................... ☺☻♥♦♣♠•◘○◙♂♀♪♫☼]],
	[[..........................►◄↕‼¶§▬↨↑↓→←∟↔▲▼]],
	[[.......................... !"#$%&'()*+,-./]],
	[[..........................0123456789:;<=>?]],
	[[..........................@ABCDEFGHIJKLMNO]],
	[[..........................PQRSTUVWXYZ[\]^_]],
	[[..........................`abcdefghijklmno]],
	[[..........................pqrstuvwxyz{|}~⌂]],
	[[..........................ÇüéâäàåçêëèïîìÄÅ]],
	[[..........................ÉæÆôöòûùÿÖÜ¢£¥₧ƒ]],
	[[..........................áíóúñÑªº¿⌐¬½¼¡«»]],
	[[..........................░▒▓│┤╡╢╖╕╣║╗╝╜╛┐]],
	[[..........................└┴┬├─┼╞╟╚╔╩╦╠═╬╧]],
	[[..........................╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀]],
	[[..........................αßΓπΣσµτΦΘΩδ∞φε∩]],
	[[..........................≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ]],
	[[................................你好，世界]],
	color = 0x404040,
	layer = 1,
}

local s = c.sprite {
	".-----.",
	"| ^ ^ |",
	"|  -  |",
	".-----.",
	color = 0xff0000,
	transparency = '.',
	layer = 2,
	kx = 3,
	ky = 3,
}

local title = c.sprite {
	"       ",
	"       ",
	color = 0x80ff,
	layer = 3,
	kx = 3,
	ky = -1,
}

local s2 = s:clone(true)
s2:setcolor(0xff00)
s2:setcolor {
	Y = 0xffff00,
	W = 0xffffff,
	".-----.",
	"| Y Y |",
	"|  W  |",
	".-----.",
}

s2:setpos(5,5)

local mouse_cursor = c.sprite {
	" ",
	background = 0x808080,
	layer = 0,
}


local x = 3
local y = 3

local camera_x = 0
local camera_y = 0

local EVENT = {}

function EVENT.QUIT()
	return true
end

function EVENT.KEY(name, press)
	if press then
		if name == "Left" then
			x = x - 1
		elseif name == "Right" then
			x = x + 1
		elseif name == "Up" then
			y = y - 1
		elseif name == "Down" then
			y = y + 1
		elseif name == "Q" then
			title:visible(false)
		elseif name == "Z" then
			c.layer { [1] = false }
		elseif name == "A" then
			camera_x = camera_x - 1
		elseif name == "D" then
			camera_x = camera_x + 1
		elseif name == "W" then
			camera_y = camera_y - 1
		elseif name == "S" then
			camera_y = camera_y + 1
		end
	else
		if name == "Q" then
			title:visible(true)
		elseif name == "Z" then
			c.layer { [1] = true }
		end
	end
end

function EVENT.MOTION(x, y)
	mouse_cursor:setpos(x,y)
end

function EVENT.BUTTON(x, y, button, pressed, click)
	mouse_cursor:setpos(x,y)
	mouse_cursor:visible(not pressed)
end

local function dispatch(name, ...)
	if name then
		return not EVENT[name](...)
	else
		return true
	end
end

while dispatch(c.event()) do
	s:setpos(x,y)
	title:setpos(x, y)
	title:text(string.format("x = %d\ny = %d", x, y))
	c.frame(camera_x, camera_y)
end

LUA_INC=-I/usr/local/include
LUA_LIB=-L/usr/local/bin -llua54
SDL_INC=-ISDL/include
SDL_LIB=-L. -lSDL2

all : rogue.dll

rogue.dll : rogue.c
	gcc -Wall -O2 --shared -o $@ $^ $(LUA_INC) $(LUA_LIB) $(SDL_INC) $(SDL_LIB)

clean :
	rm -f rogue.dll

all:
	gcc -ggdb `pkg-config --libs --cflags xcb freetype2 harfbuzz cairo cairo-xcb` -o ct src/ct.c src/xwin.c src/tbuf.c

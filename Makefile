all:
	gcc -ggdb `pkg-config --libs --cflags xcb freetype2 harfbuzz cairo cairo-xcb x11-xcb` -lutil -o ct src/ct.c src/xwin.c src/tbuf.c src/wstr.c

all:
	gcc `pkg-config --libs --cflags xcb freetype2` -o ct src/ct.c src/xwin.c

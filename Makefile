all:
	gcc `pkg-config --libs --cflags xcb freetype2 harfbuzz` -o ct src/ct.c src/xwin.c

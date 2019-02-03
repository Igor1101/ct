all:
	gcc `pkg-config --libs --cflags xcb freetype2 harfbuzz xcb-render` -o ct src/ct.c src/xwin.c

CFLAGS += -DCT_FONT_PATH="\"./usr/font.ttf\""
all:
	gcc $(CFLAGS) -ggdb `pkg-config --libs --cflags xcb freetype2 harfbuzz cairo cairo-xcb x11-xcb` -o ct src/ct.c src/xwin.c src/tbuf.c src/wstr.c
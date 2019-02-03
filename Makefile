all:
	gcc `pkg-config --libs --cflags xcb` -o ct src/ct.c src/xwin.c

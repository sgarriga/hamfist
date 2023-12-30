# CMAKE+=-DMAX_SYMBOLS=16

CC=gcc
CFLAGS=-I ../portaudio/include
LDFLAGS=-L ../portaudio/lib/.libs
LDLIBS=-l:libportaudio.a -lm -lpthread

all: fist

install: fist
	sudo cp fist /usr/local/bin/fist
	if [ ! -d "/usr/share/fist" ]; then sudo mkdir /usr/share/fist ; fi
	sudo cp char-map-* /usr/share/fist/
	sudo ln -sf /usr/share/fist/char-map-us /usr/share/fist/char-map

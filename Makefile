
CC=gcc

# Uncomment for 8 dit/dahs per 'letter'
#CPPFLAGS=-DMAX_SYMBOLS=16

# Uncomment for 16 dit/dahs per 'letter'
#CPPFLAGS=-DMAX_SYMBOLS=32

# Update thse if you already have PortAudio installed
CFLAGS=-I ./portaudio/include
LDFLAGS=-L ./portaudio/lib/.libs

# libportaudio is statically linked to avoid installing
LDLIBS=-l:libportaudio.a -lm -lasound -lpthread

.PHONY: all
all: alsa portaudio fist

.PHONY: alsa
alsa:
	if [ ! -f "/usr/include/asoundlib.h" ]; then sudo apt install libasound-dev; fi

.PHONY: portaudio
portaudio: 
	if [ ! -d "./portaudio" ]; then git clone https://github.com/PortAudio/portaudio.git; fi 
	cd portaudio; if [ ! -f "Makefile" ]; then ./configure; make clean; fi ; make

.PHONY: clean
clean: 
	rm -rf fist portaudio

.PHONY: install
install: fist
	sudo cp fist /usr/local/bin/fist
	if [ ! -d "/usr/share/fist" ]; then sudo mkdir /usr/share/fist ; fi
	sudo cp char-map-* /usr/share/fist/
	sudo ln -sf /usr/share/fist/char-map-us /usr/share/fist/char-map

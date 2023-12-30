#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include "portaudio.h"

static bool debug = false;
static bool echo = false;
static uint8_t wpm = 20; // Words Per Minute
static uint16_t hz = 650; // dit/dah tone sine wave frequency in Hz

#define SYMBOL_BITS 2
#ifndef MAX_SYMBOLS
#define MAX_SYMBOLS 8
#endif
#if MAX_SYMBOLS == 8
#define SYMBOL_SET uint16_t
#elif MAX_SYMBOLS == 16
#define SYMBOL_SET uint32_t
#elif MAX_SYMBOLS == 32
#define SYMBOL_SET uint64_t
#else
#error MAX_SYMBOLS must be 8, 16 or 32
#endif

typedef struct { 
	uint8_t key;
	SYMBOL_SET symbol_map;
} map_t;
static map_t mapping[256];
static uint8_t letters = 0;

typedef struct { 
	uint8_t key;
	char *sequence;
} sequence_t;

static sequence_t *specials = NULL;
static uint8_t specials_count = 0;

#define SAMPLE_RATE         44100
#define TABLE_SIZE            200
#define FRAMES_PER_BUFFER    1024
static float sine[TABLE_SIZE]; /* sine wavetable */

static PaStream *stream;

static void fix_nl(char *s) {
	char *p = strrchr(s, '\n');
	if (p)
		*p = '\0';
}

static void pa_error(PaError err) {
	fprintf(stderr, "An error occurred while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	// Print more information about the error.
	if (err == paUnanticipatedHostError)
	{
		const PaHostErrorInfo *hostErrorInfo = Pa_GetLastHostErrorInfo();
		fprintf(stderr, "Host API error = #%ld, hostApiType = %d\n", hostErrorInfo->errorCode, hostErrorInfo->hostApiType);
		fprintf(stderr, "Host API error = %s\n", hostErrorInfo->errorText);
	}
	Pa_Terminate();
	exit(1);
}

static void build_sinewave() {
	for (int i=0; i<TABLE_SIZE; i++)
	{
		sine[i] = (float) sin(((double)i/(double)TABLE_SIZE) * M_PI * 2.);
	}
}

static void setup_sound() {
	PaStreamParameters outputParameters;
	PaError err;

	build_sinewave();

	err = Pa_Initialize();
	if (err != paNoError) 
		pa_error(err);

	outputParameters.device = Pa_GetDefaultOutputDevice(); /* default output device */
	if (outputParameters.device == paNoDevice) {
		fprintf(stderr,"Error: No default output device.\n");
		exit(1);
	}

	outputParameters.channelCount = 2;       /* stereo output */
	outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
	outputParameters.suggestedLatency = 0.050; // Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;

	err = Pa_OpenStream(
			&stream,
			NULL, /* no input */
			&outputParameters,
			SAMPLE_RATE,
			FRAMES_PER_BUFFER,
			paClipOff,      /* we won't output out of range samples so don't bother clipping them */
			NULL, /* no callback, use blocking API */
			NULL); /* no callback, so no callback userData */
	if (err != paNoError) {
		fprintf(stderr,"Error: Cannot open stream.\n");
		pa_error(err);
	}
}

#define u2d(a) (a * (60 / (50 * wpm)))
static void tone(uint8_t units) {
	int duration_ms = 1000 * u2d(units);
	PaError err;
	int left_phase = 0;
	int right_phase = 0;
	float buffer[FRAMES_PER_BUFFER][2]; /* stereo output buffer */
	int bufferCount;

	err = Pa_StartStream(stream);
	if (err != paNoError) 
		pa_error(err);

	bufferCount = ((duration_ms * SAMPLE_RATE) / FRAMES_PER_BUFFER) / 1000;

	for (int i=0; i < bufferCount; i++)
	{
		for (int j=0; j < FRAMES_PER_BUFFER; j++)
		{
			buffer[j][0] = sine[left_phase];  /* left */
			buffer[j][1] = sine[right_phase];  /* right */
			left_phase++;
			if (left_phase >= TABLE_SIZE) 
				left_phase -= TABLE_SIZE;
			right_phase++;
			if (right_phase >= TABLE_SIZE)
				right_phase -= TABLE_SIZE;
		}

		err = Pa_WriteStream(stream, buffer, FRAMES_PER_BUFFER);
		if (err != paNoError) 
			pa_error(err);
	}

	err = Pa_StopStream(stream);
	if (err != paNoError) 
		pa_error(err);
}

static void silence(uint8_t units) {
	int duration_ms = 1000 * u2d(units);
	Pa_Sleep(duration_ms);
}

static char *charmap = "/usr/share/fist/char-map";
static void load_alphabet() {
	FILE *cm;
	int cc = 0;
	char buffer[128];
	char morse[20];
	char symbols[(SYMBOL_BITS*MAX_SYMBOLS)+2+1];
	char binary_str[(SYMBOL_BITS*MAX_SYMBOLS)+1];

	// read our keymap
	cm = fopen(charmap, "r");
	if (cm == NULL) {
		cc = errno;
		printf("Error opening character map\n");
		exit(cc);
	}

	memset(mapping, 0, sizeof(mapping));

	while (fgets(buffer, sizeof(buffer), cm) != NULL) {
		fix_nl(buffer);

		if ((buffer[0] == '#' && buffer[1] == '#') || buffer[1] != ' ')
			continue;

		if (buffer[2] != '^') {
			mapping[letters].key = buffer[0];
			symbols[0] = '\0';
			for (char *c=&buffer[2]; *c && strlen(symbols) < (SYMBOL_BITS*MAX_SYMBOLS); c++) {
				switch(*c) {
					case '\n' :
					case ' ' : break;

					case '.' : strcat(symbols, "01");
						   break;
					case '-' : strcat(symbols, "10");
						   break;
					case '/' : strcat(symbols, "1101");
						   break;
					case '_' : strcat(symbols, "1110");
						   break;
					default: printf("Invalid char-map entry \"%s\"\n", buffer);
				}
			}
			while (strlen(symbols) < (SYMBOL_BITS*MAX_SYMBOLS)) {
				strcat(symbols, "00");
			}

			sprintf(binary_str, "%*.*s", (SYMBOL_BITS*MAX_SYMBOLS), (SYMBOL_BITS*MAX_SYMBOLS), symbols);
			mapping[letters].symbol_map = (SYMBOL_SET) strtol(binary_str, NULL, 2);

			letters++;
		}
		else {
			sequence_t *p;
			char *s;

			p = realloc(specials, sizeof(sequence_t) * (specials_count + 1));
			if (p == NULL) {
				fprintf(stderr, "Out of memory!\n");
				exit(-1);
			}
			specials = p;

			s = malloc(strlen(&buffer[3]+1));
			if (s == NULL) {
				fprintf(stderr, "Out of memory!\n");
				exit(-1);
			}
			strcpy(s, &buffer[3]);
			specials[specials_count].key = buffer[0];
			specials[specials_count].sequence = s;

			specials_count++;
		}
	}
}

static void dump_symbol(SYMBOL_SET symbol) {
	bool shift = false;
	for (int k = (SYMBOL_BITS*MAX_SYMBOLS)-SYMBOL_BITS; k > -1; k-=SYMBOL_BITS) {
		switch((symbol>> k) & 0b11)  {
			case 0b00: k=-1;
				   break;

			case 0b01: printf(shift ? "/" : ".");
				   shift = false;
				   break;

			case 0b10: printf(shift ? "_" : "-");
				   shift = false;
				   break;

			case 0b11: shift = true;

		}
	}
}

static void play_symbol(SYMBOL_SET symbol) {
	bool shift = false;
	for (int k = (SYMBOL_BITS*MAX_SYMBOLS)-SYMBOL_BITS; k > -1; k-=SYMBOL_BITS) {
		switch((symbol>> k) & 0b11)  {
			case 0b00: k=-1;
				   break;

			case 0b01: if (shift) {
					   // note this is an embedded inter-character gap (rare)
					   shift = false;
					   silence(3);
				   }
				   else {
					   // dit + intra-char space
					   tone(1);
					   silence(1);
				   }
				   break;

			case 0b10: if (shift) {
					   // note this is a word space (a typed SPACE)
					   shift = false;
					   silence(7);
				   }
				   else {
					   // dit + intra-char space
					   tone(3);
					   silence(1);
				   }
				   break;

			case 0b11: shift = true;

		}
	}
}

static void dump_alphabet1() {
	for (int i = 0; i < letters; i++) {
		printf("%c ", mapping[i].key);
		dump_symbol(mapping[i].symbol_map);
		printf("\n");
	}

}

static void dump_alphabet2() {
	for (int i = 0; i < letters; i++)
		printf("%c %4x\n", mapping[i].key, mapping[i].symbol_map);

}

static void dump_specials() {
	if (specials != NULL)
		for (int i = 0; i < specials_count; i++) 
			printf("%c => %s\n", specials[i].key, specials[i].sequence);
}

static void send_morse(char c) {
	int i;

	for (i = 0; i < letters; i++) {
		if (mapping[i].key == c) {
			play_symbol(mapping[i].symbol_map);
			if (echo) {
				dump_symbol(mapping[i].symbol_map);
				printf(" ");
			}
			break;
		}
	}

	if (i == letters)
		printf("\n%c unknown", c);
}

static void play_string(char *s);
static bool special(char c) {
	bool handled = false;
	for (int i = 0; i < specials_count; i++) {
		if (specials[i].key == c) {
			play_string(specials[i].sequence);
			handled = true;
		}
	}
	return handled;
}

static void play_string(char *s) {
	for (; *s; s++) {
		if (!special(*s)) {
			if (*s >= 'a' && *s <= 'z')
				*s = toupper(*s);
			send_morse(*s); 
		}
	}
}

int main(int argc, char *argv[]) {
	char buffer[265];
	PaError err;

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && (i+1) < argc) {
			charmap = argv[++i];
		}
		if (!strcmp(argv[i], "-w") && (i+1) < argc) {
			int w = atoi(argv[++i]);
			if (w > 0 && w <256)
				wpm = w;
		}
		if (!strcmp(argv[i], "-t") && (i+1) < argc) {
			int t = atoi(argv[++i]);
			if (t > 249 && t < 1001) {
				hz = t;
			}
		}
		if (!strcmp(argv[i], "-v")) {
			echo = true;
		}
		if (!strcmp(argv[i], "-h")) {
			printf("Usage:\n%s {options}\n", argv[0]);
			printf("Options:\n");
			printf("  -h        show this information\n");
			printf("  -v        verbose (show dit/dah encoding)\n");
			printf("  -m keymap use specified file as a keymap\n");
			printf("  -t tone   specify tone frequency between 1000Hz and 250Hz (default 650Hz)\n");
			printf("  -w WPM    specify words-per-minute 1..255 (default 50)\n");
			exit(0);
		}

	}

	setup_sound();

	load_alphabet();
	if (debug) {
		dump_alphabet1();
		dump_alphabet2();
		dump_specials();
	}

	while(true) {
		fgets(buffer, sizeof(buffer), stdin);
		fix_nl(buffer);
		play_string(buffer);
		printf("\n");
	}

	err = Pa_CloseStream(stream);
	if (err != paNoError) 
		pa_error(err);

	Pa_Terminate();


}

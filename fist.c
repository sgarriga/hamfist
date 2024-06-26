#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>
#include <math.h>
#include <alsa/error.h>
#include <unistd.h>
#include <signal.h>
#include "portaudio.h"

#define BUFF_MAX 4096
// limit to number of chars on a command line

// CTRL-C handling
static volatile sig_atomic_t die = false;
void interruptHdl(int dummy) {
	die = true;
}

static bool debug = false;
static bool piped = false;
static bool echo = false;

// Morse handling stuff
static uint8_t wpm = 20; // Words Per Minute
static uint16_t dit_ms = 0;    // calculated later
static uint16_t hz = 650; // dit/dah tone sine wave frequency in Hz
static char *message = NULL;

#define SYMBOL_BITS 2
#ifndef MAX_SYMBOLS
#define MAX_SYMBOLS 8
#endif

#ifndef FIST840
static char *charmap = "/usr/share/fist/char-map";
#if MAX_SYMBOLS == 8
#define SYMBOL_SET uint16_t
#elif MAX_SYMBOLS == 16
#define SYMBOL_SET uint32_t
#elif MAX_SYMBOLS == 32
#define SYMBOL_SET uint64_t
#else
#error MAX_SYMBOLS must be 8, 16 or 32
#endif
#else
#define SYMBOL_SET uint16_t
#endif

typedef struct { 
	uint8_t key;
	SYMBOL_SET symbol_map;
} map_t;

typedef struct { 
	uint8_t key;
	char *sequence;
} sequence_t;

#ifndef FIST840
static map_t mapping[UINT8_MAX];
static uint8_t letters = 0;

static sequence_t *specials = NULL;
static uint8_t specials_count = 0;
#else
static const map_t mapping[] = {{'A', 0x6000}, {'B', 0x9500},
                                {'C', 0x9900}, {'D', 0x9400},
                                {'E', 0x4000}, {'F', 0x5900},
                                {'G', 0xa400}, {'H', 0x5500},
                                {'I', 0x5000}, {'J', 0x6a00},
                                {'K', 0x9800}, {'L', 0x6500},
                                {'M', 0xa000}, {'N', 0x9000},
                                {'O', 0xa800}, {'P', 0x6900},
                                {'Q', 0xa600}, {'R', 0x6400},
                                {'S', 0x5400}, {'T', 0x8000},
                                {'U', 0x5800}, {'V', 0x5600},
                                {'W', 0x6800}, {'X', 0x9600},
                                {'Y', 0x9a00}, {'Z', 0xa500},
                                {'0', 0xaa80}, {'1', 0x6a80},
                                {'2', 0x5a80}, {'3', 0x5680},
                                {'4', 0x5580}, {'5', 0x5540},
                                {'6', 0x9540}, {'7', 0xa540},
                                {'8', 0xa940}, {'9', 0xaa40},
                                {'"', 0xe000}, {'&', 0x6540},
                                {'\'', 0x6a90}, {'@', 0x6990},
                                {')', 0x9a60}, {'(', 0x9a40},
                                {':', 0xa950}, {',', 0xa5a0},
                                {'=', 0x9580}, {'!', 0x99a0},
                                {'.', 0x6660}, {'-', 0x9560},
                                {'*', 0x9600}, {'+', 0x6640},
                                {'"', 0x6590}, {'?', 0x5a50},
                                {'/', 0x9640}, {'\\', 0x6580}};
static uint8_t letters = 56;

static sequence_t specials[] = {{'%', "0/0"},
                                {'~', "TILDE"},
                                {'#', "HASH"},
                                {'$', "USD"},
                                {'^', "CARET"},
                                {'_', "UNDERSCORE"},
                                {'{', "B(_"},
                                {'}', "_)B"},
                                {'|', "BAR"},
                                {'<', "LT"},
                                {'>', "GT"},
                                {'`', "'"},
                                {'[', "S(_"},
                                {']', "_)S"},
                                {';', ".,"}};
static uint8_t specials_count = 15;
#endif

// PortAudio stuff
static uint32_t sample_rate = 44100;
#define TABLE_SIZE            UINT8_MAX
#define FRAMES_PER_BUFFER     64
typedef struct
{
	float   sine[TABLE_SIZE];
	uint8_t phase;
} paTestData;

static paTestData data;

static PaStream *stream;
static PaStreamParameters outputParameters;

// ALSA error handler - to supress ALSA noise on stderr
void alsa_err(const char *file, int line, const char *function, int err, const char *fmt,...) { }

// Jack also causes noise on stderr, so we will have to suppress all errors for a while
static int stderr_fd;
static fpos_t stderr_pos;

static void mute_stderr(void)
{
	fflush(stderr);
	fgetpos(stderr, &stderr_pos);
	stderr_fd = dup(fileno(stderr));
	freopen("/dev/null", "w", stderr);
}

static void unmute_stderr(void)
{
	fflush(stderr);
	dup2(stderr_fd, fileno(stderr));
	close(stderr_fd);
	clearerr(stderr);
	fsetpos(stderr, &stderr_pos);
}

// strip trailing newline from text string
static void fix_nl(char *s) {
	char *p = strrchr(s, '\n');
	if (p)
		*p = '\0';
}

// print out the stuff the PortAudio folks do in their examples
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

// simplified version of example
static int paCallback( const void *inputBuffer, void *outputBuffer,
		unsigned long framesPerBuffer,
		const PaStreamCallbackTimeInfo* timeInfo,
		PaStreamCallbackFlags statusFlags,
		void *userData )
{
	paTestData *data = (paTestData*)userData;
	float *out = (float*)outputBuffer;
	unsigned long i;

	(void) timeInfo; /* Prevent unused variable warnings. */
	(void) statusFlags;
	(void) inputBuffer;
	for( i=0; i<framesPerBuffer; i++ )
	{
		*out++ = data->sine[data->phase];
		*out++ = data->sine[data->phase];
		data->phase++;
	}

	return paContinue;
}

// simplified version of example
static void build_sinewave() {
	for (int i=0; i<TABLE_SIZE; i++)
	{
		data.sine[i] = (float) sin(((double)i/(double)TABLE_SIZE) * M_PI * 2.0);
	}
	data.phase = 0;
}

static void StreamFinished( void* userData )
{
	;
}

// again, based on example
static void setup_sound() {
	PaError err;
	int true_se, null_se;

	snd_lib_error_set_handler(&alsa_err); // suppress ALSA warnings
	mute_stderr(); // to stop Jack errors - this is a bit extreme!

	build_sinewave();

	err = Pa_Initialize();
	if (err != paNoError) 
		pa_error(err);

	outputParameters.device = Pa_GetDefaultOutputDevice();
	if (outputParameters.device == paNoDevice) {
		unmute_stderr();
		fprintf(stderr,"Error: No default output device.\n");
		exit(1);
	}

	outputParameters.channelCount = 2;       /* stereo output */
	outputParameters.sampleFormat = paFloat32; /* 32 bit floating point output */
	outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
	outputParameters.hostApiSpecificStreamInfo = NULL;

	err = Pa_OpenStream(
			&stream,
			NULL, /* no input */
			&outputParameters,
			sample_rate,
			FRAMES_PER_BUFFER,
			paClipOff,
			paCallback,
			&data);
	unmute_stderr();
	if (err != paNoError) {
		fprintf(stderr,"Error: Cannot open stream.\n");
		pa_error(err);
	}

	snd_lib_error_set_handler(NULL); // resume ALSA warnings
}

#define silence(a) pa_sustain(a)

// allow current note or silence to play for the alloted number of time units
static void pa_sustain(uint8_t units) {
	uint32_t duration_ms = (uint32_t) (units * dit_ms);
	Pa_Sleep(duration_ms);
}

static void tone(uint8_t units) {
	PaError err;

	err = Pa_StartStream(stream);
	if (err != paNoError) 
		pa_error(err);

	pa_sustain(units);

	err = Pa_StopStream(stream);
	if (err != paNoError) 
		pa_error(err);
}

#ifndef FIST840
static void load_charmap() {
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
#endif

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

static void dump_charmap1() {
	for (int i = 0; i < letters; i++) {
		printf("%c ", mapping[i].key);
		dump_symbol(mapping[i].symbol_map);
		printf("\n");
	}

}

static void dump_charmap2() {
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
	char buffer[BUFF_MAX];
	PaError err;

	signal(SIGINT, interruptHdl);

	for (int i = 0; i < argc; i++) {
#ifndef FIST840
		if (!strcmp(argv[i], "-m") && (i+1) < argc) {
			charmap = argv[++i];
		}
#endif
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
		if (!strcmp(argv[i], "-s")) {
			message=argv[++i];
		}
		if (!strcmp(argv[i], "-p")) {
			piped = true;
		}
		if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			printf("Usage:\n%s {options}\n", argv[0]);
			printf("Options:\n");
			printf("  -h/--help  show this information\n");
#ifndef FIST840
			printf("  -m keymap  use specified file as a keymap\n");
#endif
			printf("  -p         output piped input rather than prompting for message\n");
			printf("  -s string  output string rather than prompting for message (overrides -p)\n");
			printf("  -t tone    specify tone frequency (Hz) between 250 and 1000 (default 650)\n");
			printf("  -v         verbose (show dit/dah encoding)\n");
			printf("  -w WPM     specify words-per-minute 1..255 (default 20)\n");
			exit(0);
		}

	}

	dit_ms = (uint16_t) (1000.0 * 60.0 / (50.0 * wpm));
	sample_rate = (uint32_t) ((float) TABLE_SIZE * (float) hz);
	setup_sound();

#ifndef FIST840
	load_charmap();
#endif
	if (debug) {
		dump_charmap1();
		dump_charmap2();
		dump_specials();
	}

	if (!message) {
		if (!piped) {
			printf("Dit = %d ms, Rate = %d WPM, Frequency = %d Hz\n", dit_ms, wpm, hz);

			printf("Type your message and hit Enter to convert.\n");
			printf("(Ctrl+C to quit)\n");
		}
		while(!die || piped) {
			fgets(buffer, sizeof(buffer), stdin);
			fix_nl(buffer);
			play_string(buffer);
			if (piped)
				break;
			printf("\n");
		}
		if (!piped) {
			printf("Exiting\n");
		}
	}
	else {
		play_string(message);
		printf("\n");
	}

	err = Pa_CloseStream(stream);
	if (err != paNoError) 
		pa_error(err);

	Pa_Terminate();
	exit(0);
}

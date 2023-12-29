#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdint.h>

static bool debug = false;
static bool echo = true;
static uint8_t wpm = 20; // Words Per Minute

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

static void fix_nl(char *s) {
	char *p = strrchr(s, '\n');
	if (p)
		*p = '\0';
}

static void setup_sound() {
}

#define u2d(a) (a * (60 / (50 * wpm)))
static void tone(uint8_t units) {
	int duration = u2d(units);
}

static void silence(uint8_t units) {
	int duration = u2d(units);
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

	for (int i = 0; i < argc; i++) {
		if (!strcmp(argv[i], "-m") && (i+1) < argc) {
			charmap = argv[++i];
		}
		if (!strcmp(argv[i], "-w") && (i+1) < argc) {
			int w = atoi(argv[++i]);
			if (w > 0 && w <256)
				wpm = 0;
		}
		if (!strcmp(argv[i], "-t") && (i+1) < argc) {
			int t = atoi(argv[++i]);
			if (t > 0 && t < 256) { // to do
				int hz = 0;
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
			printf("  -t tone   specify tone frequency (default ?)\n");
			exit(0);
		}

	}

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
}

# hamfist
Generate Morse Code (CW) tones from US keyboard input under Linux (i.e., a Raspberry Pi). It might work on WSL2 if you have managed to get PulseAudio to work - but I have not.

This application uses PortAudio (https://github.com/PortAudio/portaudio) to generate sounds. If you already have it installed, you <i>might</i> want to tweak the `Makefile`.

## Building
Building is straightforward, the `Makefile` pulls down the required Github repository for PortAudio and the ALSA library if required (it will prompt for the `sudo` password if it needs to).

`git clone https://github.com/sgarriga/hamfist.git`<br>
`cd hamfist`<br>
`make`<br>
Enter sudo password if prompted<br>
`make install`

## Usage
`Usage:`<br>
`fist {options}`<br>
`Options:`<br>
`  -h/--help  show this information`<br>
`  -m keymap  use specified file as a keymap`<br>
`  -p         output piped input rather than prompting for message`<br>
`  -s string  output string rather than prompting for message (overrides -p)`<br>
`  -t tone    specify tone frequency (Hz) between 250 and 1000 (default 650)`<br>
`  -v         verbose (show dit/dah encoding)`<br>
`  -w WPM     specify words-per-minute 1..255 (default 20)`<br>
<br>

## Character Maps
Unless an alternate is specified with the `-m` option, the `fist` program uses the character map file `/usr/share/fist/char-map`. By default a standard US keyboard is assumed (and the file is actually a link to `/usr/share/fist/char-map-us`).

The mapping file format is:
- lines starting `##` are comments
- data lines are of the format `<char><space><morse sequence>` or `<char><space>^<letter sequence>`
- the `<char>` value should be an UPPERCASE character - Morse Code does not have a lower case!
- morse sequences are specified using the following symbols:
1. `.` = dit
2. `-` = dah
3. `/` = letter-space
4. `_` = word-space (normally only used to map the space character)
- letter sequences are used to replace a character with a text string e.g. `#` can be converted to `HASH` by `# ^HASH` (note the caret).

It is possible to use non-US keyboards using an alternate character map provided:
1. the key generates a single-byte (8-bit) ASCII value.
2. the Morse code sequence consists of eight (8) or fewer dits and dahs. If more are required, specify -DMAX_SYMBOLS=16 or =32 when you build

An alternate `fist-us` executable is also built - this uses a hard-coded mapping of US keys.

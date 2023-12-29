# hamfist
Generate Morse Code (CW) tones from US keyboard input under Linux (i.e. a Raspberry Pi).

At least that's the plan...<br>
Right now characters are converted to a visual representation while I figure out the audio

## Usage
`
Usage:
./fist {options}
Options:
  -h        show this information
  -v        verbose (show dit/dah encoding)
  -m keymap use specified file as a keymap
  -t tone   specify tone frequency (default ?)
`
Ctrl-C to exit.

## Character Maps
The program uses a default character map file `/usr/share/fist/char-map`. By default a standard US keyboard is assumed (the file is a link to `/usr/share/fist/char-map-us`), but an alternate default may be specified by modifying the link or by specifying an alternate mapping file using the `-m` option.

The mapping file format is:
- lines starting `##` are comments
- data lines are of the format `<char><space><morse sequence>` or `<char><space>^<letter sequence>`
- the `<char>` value should be an UPPERCASE character - Morse Code does not have a lower case!
- morse sequences are specified using the following symbols:
1. `.` = dit
2. `-` = dah
3. `/` = letter-space
4. `_` = word-space (normally only used to map the space character)
- letter sequences are used to replace a character with a text string e.g. `#` can be converted to `HASH` by `# ^HASH` (note the caret)..

It is possible to use non-US keyboards using an alternate character map provided 
1. the key generates a single-byte ASCII value.
2. the Morse code sequence consists of eight (8) or fewer dits and dahs. If more are required specify -DMAX_SYMBOLS=16 or =32


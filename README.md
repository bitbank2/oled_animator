*** OLED Animator ***

OLED SSD1306 animation player using the I2C interface<br>
Copyright (c) 2018 BitBank Software, Inc.<br>
Written by Larry Bank (bitbank@pobox.com)<br>
Project started 5/21/2018<br>
<br>
This player can display compressed animation data on a SSD1306 OLED
connected via I2C. The original purpose of the player was to make it easy
to run interesting animations on Arduino AVR microcontrollers. Since AVRs
don't have much memory (either flash or RAM), the goal of the data
compression is to reduce the amount of memory needed for full screen
arbitrary animations and reduce the amount of data written to the display
so that slower, I2C connections could support good framerates.
The compression scheme is my own invention. I'm
not claiming that it can't be improved, but it satisfies the goals of
being byte-oriented, simple to decompress and doesn't require any RAM
on the player side. The data is compressed in 3 ways:<br>
1) Skip - the bytes are identical to those of the previous frame<br>
2) Copy - the bytes are different and are copied as-is<br>
3) Repeat - a repeating byte sequence<br>
In order to not keep a copy of the display memory on the player, the
skip operations just move the write address (cursor position) on the
OLED display. The bytes are packed such that the highest 2 bits of each
command byte determine the treatment. They are:<br>
00SSSCCC - skip+copy (in that order). The 3 bit lengths of each of the
    skip and copy represent 0-7<br>
00000000 - special case (long skip). The next byte is the len (1-256)<br>
01CCCSSS - copy+skip (in that order). Same as above<br>
01000000 - special case (long copy). The next byte is the len (1-256)<br>
1RRRRRRR - Repeat the next byte 1-128 times.<br>
<br>
With those simple operations, typical animated GIF's get compressed between
3 and 6 to 1 (each 1024 byte frame becomes 170 to 341 bytes of compressed
data)<br>
<br>
*** Note: ***
The compressor uses my closed-source imaging library to decode animated GIFs. I need to find a solution to this, so in the mean time, the source code is here (minus the imaging library) and I have included pre-built binaries for Debian Linux and MacOS. I'll resolve this soon as well as provide the Arduino version.
 

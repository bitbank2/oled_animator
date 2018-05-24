//
// OLED SSD1306 animation player using the I2C interface
// Written by Larry Bank (bitbank@pobox.com)
// Project started 5/21/2018
//
// This player can display compressed animation data on a SSD1306 OLED
// connected via I2C. The original purpose of the player was to make it easy
// to run interesting animations on Arduino AVR microcontrollers. Since AVRs
// don't have much memory (either flash or RAM), the goal of the data
// compression is to reduce the amount of memory needed for full screen
// arbitrary animations and reduce the amount of data written to the display
// so that slower, I2C connections could support good framerates.
// The compression scheme is my own invention. I'm
// not claiming that it can't be improved, but it satisfies the goals of
// being byte-oriented, simple to decompress and doesn't require any RAM
// on the player side. The data is compressed in 3 ways:
// 1) Skip - the bytes are identical to those of the previous frame
// 2) Copy - the bytes are different and are copied as-is
// 3) Repeat - a repeating byte sequence
// In order to not keep a copy of the display memory on the player, the
// skip operations just move the write address (cursor position) on the
// OLED display. The bytes are packed such that the highest 2 bits of each
// command byte determine the treatment. They are:
// 00SSSCCC - skip+copy (in that order). The 3 bit lengths of each of the
//    skip and copy represent 0-7
// 00000000 - special case (long skip). The next byte is the len (1-256)
// 01CCCSSS - copy+skip (in that order). Same as above
// 01000000 - special case (long copy). The next byte is the len (1-256)
// 1RRRRRRR - Repeat the next byte 1-128 times.
//
// With those simple operations, typical animated GIF's get compressed between
// 3 and 6 to 1 (each 1024 byte frame becomes 170 to 341 bytes of compressed
// data)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include "oled96.h"

// Masks defining the upper 2 bit "commands" for the compressed data
#define OP_MASK 0xc0
#define OP_SKIPCOPY 0x00
#define OP_COPYSKIP 0x40
#define OP_REPEAT1 0x80
#define OP_REPEAT2 0xc0

static int file_i2c = 0;
static int iOffset;
static int bBadDisplay = 0;
static int bLoop = 0;
static char szIn[512];
static int iChannel = 1; // default I2C channel
static int iAddress = 0x3c; // default I2C address
static int iFrameRate = 15; // 15 FPS
static int iDelay; // based on framerate

static void oledWriteCommand(unsigned char);
//
// Opens a file system handle to the I2C device
// Initializes the OLED controller into "page mode"
// Prepares the font data for the orientation of the display
// Returns 0 for success, 1 for failure
//
int oledInit(int iChannel, int iAddr, int bFlip, int bInvert)
{
const unsigned char initbuf[]={0x00,0xae,0xa8,0x3f,0xd3,0x00,0x40,0xa1,0xc8,
			0xda,0x12,0x81,0xff,0xa4,0xa6,0xd5,0x80,0x8d,0x14,
			0xaf,0x20,0x00};
char filename[32];
int rc;
unsigned char uc[4];

	sprintf(filename, "/dev/i2c-%d", iChannel);
	if ((file_i2c = open(filename, O_RDWR)) < 0)
	{
		fprintf(stderr, "Failed to open the i2c bus\n");
		file_i2c = 0;
		return 1;
	}

	if (ioctl(file_i2c, I2C_SLAVE, iAddr) < 0)
	{
		fprintf(stderr, "Failed to acquire bus access or talk to slave\n");
		file_i2c = 0;
		return 1;
	}

	rc = write(file_i2c, initbuf, sizeof(initbuf));
	if (rc != sizeof(initbuf))
		return 1;
	if (bInvert)
	{
		uc[0] = 0; // command
		uc[1] = 0xa7; // invert command
		rc = write(file_i2c, uc, 2);
	}
	if (bFlip) // rotate display 180
	{
		uc[0] = 0; // command
		uc[1] = 0xa0;
		rc = write(file_i2c, uc, 2);
		uc[1] = 0xc0;
		rc = write(file_i2c, uc, 2);
	}
	return 0;
} /* oledInit() */

// Sends a command to turn off the OLED display
// Closes the I2C file handle
void oledShutdown()
{
	if (file_i2c != 0)
	{
		oledWriteCommand(0xaE); // turn off OLED
		close(file_i2c);
		file_i2c = 0;
	}
}

// Send a single byte command to the OLED controller
static void oledWriteCommand(unsigned char c)
{
unsigned char buf[2];
int rc;

	buf[0] = 0x00; // command introducer
	buf[1] = c;
	rc = write(file_i2c, buf, 2);
	if (rc) {} // suppress warning
} /* oledWriteCommand() */

static void oledWriteCommand2(unsigned char c, unsigned char d)
{
unsigned char buf[3];
int rc;

	buf[0] = 0x00;
	buf[1] = c;
	buf[2] = d;
	rc = write(file_i2c, buf, 3);
	if (rc) {} // suppress warning
} /* oledWriteCommand2() */

int oledSetContrast(unsigned char ucContrast)
{
        if (file_i2c == 0)
                return -1;

	oledWriteCommand2(0x81, ucContrast);
	return 0;
} /* oledSetContrast() */

// Send commands to position the "cursor" to the given
// row and column
static void oledSetPosition(int x, int y)
{
	oledWriteCommand(0xb0 | y); // go to page Y
	oledWriteCommand(0x00 | (x & 0xf)); // // lower col addr
	oledWriteCommand(0x10 | ((x >> 4) & 0xf)); // upper col addr
	iOffset = (y<<7)+x;
}

// Write a block of pixel data to the OLED
// Length can be anything from 1 to 1024 (whole display)
static void oledWriteDataBlock(unsigned char *ucBuf, int iLen)
{
unsigned char ucTemp[1028];
int rc;

	ucTemp[0] = 0x40; // data command
//
// Badly behaving horizontal addressing mode
// basically behaves the same as page mode (needs to be explicitly sent to
// the next page instead of auto-incrementing)
//
	if (bBadDisplay)
	{
	int j, i = 0;
		while (((iOffset & 0x7f) + iLen) >= 128) // if it will hit the page end
		{
			j = 128 - (iOffset & 0x7f); // amount we can write
			memcpy(&ucTemp[1], &ucBuf[i], j);
			rc = write(file_i2c, ucTemp, j+1);	
			i += j; iLen -= j;
			iOffset = (iOffset + j) & 0x3ff;
			oledSetPosition(iOffset & 0x7f, (iOffset >> 7));
		} // while it needs help
		if (iLen)
		{
			memcpy(&ucTemp[1], &ucBuf[i], iLen);
			rc = write(file_i2c, ucTemp, iLen+1);
			iOffset += iLen;
		}
	}
	else // can write in one shot
	{
		memcpy(&ucTemp[1], ucBuf, iLen);
		rc = write(file_i2c, ucTemp, iLen+1);
		iOffset += iLen;
		iOffset &= 0x3ff;
	}
	if (rc) {} // suppress warning
}

// Fill the frame buffer with a byte pattern
// e.g. all off (0x00) or all on (0xff)
int oledFill(unsigned char ucData)
{
int y;
unsigned char temp[128];

	if (file_i2c == 0) return -1; // not initialized

	memset(temp, ucData, 128);
	for (y=0; y<8; y++)
	{
		oledSetPosition(0,y); // set to (0,Y)
		oledWriteDataBlock(temp, 128); // fill with data byte
	} // for y
	return 0;
} /* oledFill() */

void PlayAnimation(unsigned char *pData, int iSize)
{
unsigned char *s, *pEnd;
int j, i;
unsigned char b, bCode;
unsigned char ucTemp[256];

do {
   s = pData;
   pEnd = &s[iSize];
   while (s < pEnd)
   {
    i = 0;
    oledSetPosition(0,0);
    while (i < 1024) // try one frame
     {
        bCode = *s++;
        switch (bCode & OP_MASK) // different compression types
        {
            case OP_SKIPCOPY: // skip/copy
            if (bCode == OP_SKIPCOPY) // big skip
            {
               b = *s++;
               i += b + 1;
               oledSetPosition(i & 0x7f, (i >> 7));
            }
            else // skip/copy
            {
               if (bCode & 0x38)
               {
                  i += ((bCode & 0x38) >> 3); // skip amount
                  oledSetPosition(i & 0x7f, (i >> 7));
               }
               if (bCode & 7)
               {
                   oledWriteDataBlock(s, bCode & 7);
                   s += (bCode & 7);
                   i += bCode & 7;
               }
           }
           break;
          case OP_COPYSKIP: // copy/skip
          if (bCode == OP_COPYSKIP) // big copy
          {
             b = *s++;
             j = b + 1;
	     oledWriteDataBlock(s, j);
             s += j;
             i += j;
          }
	  else
	  {
             j = ((bCode & 0x38) >> 3);
             if (j)
             {
                oledWriteDataBlock(s, j);
                s += j;
                i += j;
             }
             if (bCode & 7)
             {
                 i += (bCode & 7); // skip
                 oledSetPosition(i & 0x7f, (i >> 7));
             }
           }
	break;
      case OP_REPEAT1: // repeat
      case OP_REPEAT2:
          j = (bCode & 0x7f) + 1;
          b = *s++;
	  memset(ucTemp, b, j);
          oledWriteDataBlock(ucTemp, j);
          i += j;
          break;  
        } // switch on code type
     } // while rendering frame
     usleep(iDelay);
    } // while playing frames
  } while (bLoop);
} /* PlayAnimation() */

static void parse_opts(int argc, char *argv[])
{
// set default options
int i = 1;
    
    while (i < argc)
    {   
        /* if it isn't a cmdline option, we're done */
        if (0 != strncmp("--", argv[i], 2))
            break;
        /* GNU-style separator to support files with -- prefix
         * example for a file named "--baz": ./foo --bar -- --baz
         */
        if (0 == strcmp("--", argv[i]))
        {   
            i += 1;
            break;
        }
        /* test for each specific flag */ 
        if (0 == strcmp("--in", argv[i])) {
            strcpy(szIn, argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--addr", argv[i])) {
            iAddress = strtol(argv[i+1], NULL, 16);;
            i += 2;
	} else if (0 == strcmp("--rate", argv[i])) {
	    iFrameRate = atoi(argv[i+1]);
	    i += 2;
	} else if (0 == strcmp("--bad", argv[i])) {
	    bBadDisplay = 1;
	    i++;
	} else if (0 == strcmp("--loop", argv[i])) {
	    bLoop = 1;
	    i++;
        } else if (0 == strcmp("--chan", argv[i])) {
            iChannel = atoi(argv[i+1]);
            i += 2;
        }  else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }
    }
} /* parse_opts() */

int main(int argc, char *argv[])
{
FILE *pf;
int iSize, i;
unsigned char *pData;

	if (argc < 2)
	{
		printf("oledplay - SSD1306 animation player\n");
		printf("written by Larry Bank 5/21/18\n");
		printf("usage:\n\n");
		printf("./oledplay <options>\n\n");
		printf("--in    input file\n");
		printf("--chan  optional I2C channel; defaults to 1\n");
		printf("--addr  optional hex I2C addess; defaults to 0x3c\n");
		printf("--rate  optional framerate; defaults to 15FPS\n");
		printf("--loop  loops animation until CTRL-C is pressed\n");
		printf("--bad 	indicates the display doesn't support horizontal address mode\n");
		return -1;
	}
	parse_opts(argc, argv);
	iDelay = 1000000 / iFrameRate;
	i = oledInit(iChannel, iAddress, 0, 0);
	if (i)
	{
		printf("Error initializing OLED; are you running as sudo?\n");
		return -1;
	}
	pf = fopen(szIn, "rb");
	if (pf == NULL)
	{
		printf("Error opening %s\n", szIn);
		oledShutdown();
		return -1;
	}
	fseek(pf, 0L, SEEK_END);
	iSize = (int)ftell(pf);
	fseek(pf, 0L, SEEK_SET);
	pData = malloc(iSize);
	fread(pData, iSize, 1, pf);
	fclose(pf);
	PlayAnimation(pData, iSize);
	oledShutdown();
	return 0;
} /* main() */

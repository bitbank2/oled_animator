//
// Tiny Compressor (tcomp)
//
// Project started 5/13/2018
// Compress bitonal animated GIF into a compact format
// suitable for micontroller playback
//

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "pil.h"
#include "pil_io.h"

#define MAX_PATH 260
static char szIn[MAX_PATH];
static char szOut[MAX_PATH];
static int iTop = -1;
static int iLeft = -1;
static int bC = 0; // write C code instead of binary data to output file
static int bInvert = 0; // invert the bitmap colors
//#define DEBUG_LOG
//#define SAVE_INPUT_FRAMES
//#define SAVE_OUTPUT_FRAMES
//
// ShowHelp
//
// Display the help info when incorrect or no command line parameters are passed
//
void ShowHelp(void)
{
    printf(
	"tiny_compress - compress bitonal animated GIF\n\n"
	"usage: ./tcomp <options>\n"
	"valid options:\n\n"
        " --in <infile>       Input file\n"
	" --out <outfile>     Output file\n"
	" --c                 Write C code to output file\n"
	" --invert            Invert bitmap colors\n"
	" --top N             Top of cropped area\n"
	" --left N            Left of cropped area\n"
    );
}

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
        } else if (0 == strcmp("--out", argv[i])) {
            strcpy(szOut, argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--left", argv[i])) {
            iLeft = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--top", argv[i])) {
            iTop = atoi(argv[i+1]);
            i += 2;
        } else if (0 == strcmp("--c", argv[i])) {
            bC = 1;
            i++;
	} else if (0 == strcmp("--invert", argv[i])) {
            bInvert = 1;
            i++;
	}  else {
            fprintf(stderr, "Unknown parameter '%s'\n", argv[i]);
            exit(1);
        }
    }
} /* parse_opts() */
//
// Gather a vertical byte from horizontal pixels
//
unsigned char GetByte(unsigned char *pBuf, int x, int y)
{
int i;
unsigned char *s, b = 0;
unsigned char ucMask = 0x80 >> (x & 7);

   s = pBuf + (y * 16) + (x >> 3);
   for (i=0; i<8; i++)
   {
      if (s[0] & ucMask)
         b |= (1 << i);
      s += 16; // next line
   }
return b;
} /* GetByte() */
//
// Find repeats in a length of "different" bytes
//
int TryRepeat(int *iDiffCount, unsigned char *pTemp, unsigned char *pDest, int *iLen)
{
int i, j, x;
int iCount, iStart;
int iRepeat;
unsigned char ucMatch;

#ifdef DEBUG_LOG
printf("Entering TryRepeat(), iDiffCount = %d\n", *iDiffCount);
#endif

   i = *iLen; // output offset
   iCount = *iDiffCount;
   iStart = 0;
   iRepeat = 1;
   ucMatch = pTemp[0];
   for (x=1; x<iCount; x++)
   {
      if (ucMatch == pTemp[x]) // count repeats
      {
          iRepeat++;
      }
      else // start a new search, take care of old
      {
         if (iRepeat >= 3)
         {
             j = (x - iStart) - iRepeat; // non-repeats before this section
             if (j != 0) // non-repeats before the repeating bytes
             {
                 if (j > 7) // big copy
                 {
                    while (j >= 256)
                    {
#ifdef DEBUG_LOG
printf("big copy 256\n");
#endif
                        pDest[i++] = 0x40; // big copy
                        pDest[i++] = 0xff; // max length 256
                        memcpy(&pDest[i], &pTemp[iStart], 256);
                        i += 256;
                        j -= 256;
                        iStart += 256;
                    }
                    if (j > 7)
                    {
#ifdef DEBUG_LOG
printf("big copy %d\n", j);
#endif
                        pDest[i++] = 0x40;
                        pDest[i++] = (unsigned char)(j - 1);
                        memcpy(&pDest[i], &pTemp[iStart], j);
                        i += j;
                        iStart += j;
                        j = 0;
                    }
                 }
                 if (j > 0 && j <= 7) // short diff
                 {
#ifdef DEBUG_LOG
printf("short copy %d\n", j);
#endif
                     pDest[i++] = 0x40 + (j<<3);
                     memcpy(&pDest[i], &pTemp[iStart], j);
                     i += j;
                     iStart += j;
                 }
             }
             // now encode the repeat
             while (iRepeat >= 128)
             {
#ifdef DEBUG_LOG
printf("repeat 128, 0x%02x\n", ucMatch);
#endif
                 pDest[i++] = 0xff; // repeat of 128
                 pDest[i++] = ucMatch;
                 iRepeat -= 128;
                 iStart += 128;
             }
             if (iRepeat)
             {
#ifdef DEBUG_LOG
printf("repeat %d, 0x%02x\n", iRepeat, ucMatch);
#endif
                 pDest[i++] = 0x80 + (iRepeat - 1);
                 pDest[i++] = ucMatch;
                 iStart += iRepeat;
             }
         } // repeat >= 3
         ucMatch = pTemp[x];
         iRepeat = 1;
      }
   } // for x
// Pack the non-repeats and repeats as needed
   if (iRepeat >= 3) // pack the last set of repeats
   {
             j = (x - iStart) - iRepeat; // non-repeats before this section
             if (j != 0) // non-repeats before the repeating bytes
             {
                 if (j > 7) // big diff
                 {
                    while (j >= 256)
                    {
#ifdef DEBUG_LOG
printf("big copy 256\n");
#endif
                        pDest[i++] = 0x40; // big copy
                        pDest[i++] =0xff; // max length 256
                        memcpy(&pDest[i], &pTemp[iStart], 256);
                        i += 256;
                        j -= 256;
                        iStart += 256;
                    }
                    if (j > 7)
                    {
#ifdef DEBUG_LOG
printf("big copy %d\n", j);
#endif
                        pDest[i++] = 0x40;
                        pDest[i++] = (unsigned char)(j - 1);
                        memcpy(&pDest[i], &pTemp[iStart], j);
			i += j;
                        iStart += j;
                        j = 0;
                    }
                 }
                 if (j > 0 && j <= 7) // short diff
                 {
#ifdef DEBUG_LOG
printf("short copy %d\n", j);
#endif
                     pDest[i++] = 0x40 + (j<<3);
                     memcpy(&pDest[i], &pTemp[iStart], j);
                     i += j;
                     iStart += j;
                 }
             }
             // now encode the repeat
             while (iRepeat >= 128)
             {
#ifdef DEBUG_LOG
printf("repeat 128, 0x%02x\n", ucMatch);
#endif
                 pDest[i++] = 0xff; // repeat of 128
                 pDest[i++] = ucMatch;
                 iRepeat -= 128;
                 iStart += 128;
             }
             if (iRepeat)
             {
#ifdef DEBUG_LOG
printf("repeat %d, 0x%02x\n", iRepeat, ucMatch);
#endif
                 pDest[i++] = 0x80 + (iRepeat - 1);
                 pDest[i++] = ucMatch;
                 iStart += iRepeat;
             }
   } // pack last set of repeats
   // we could have lingering non-repeats that we couldn't pack
   *iDiffCount -= iStart;
   *iLen = i;
   return iStart;
} /* TryRepeat() */
//
// Handle a specific set of data to compress
// When entering this function, either there will be both a skip and diff
// count, or it will be the last set of data at the end of the frame
//
void CompressIt(unsigned char *pDest, int *iLen, int *iSkipCount, int *iDiffCount, unsigned char *pTemp, int bFinal)
{
int i = *iLen; // local copy of length

#ifdef DEBUG_LOG
printf("Entering CompressIt, size = %d\n", i);
#endif

   if (*iSkipCount & 0x8000) // skipped bytes are first
   {
      *iSkipCount &= 0x7fff;
      if (*iSkipCount > 7) // big skip
      {
         while(*iSkipCount >= 256)
         {
#ifdef DEBUG_LOG
printf("BigSkip 256\n");
#endif
            pDest[i++] = 0x00; // big skip
            pDest[i++] = 0xff; // max skip = 256 at a time
            *iSkipCount -= 256; 
         }
         if (*iSkipCount > 7) // another big skip
         {
#ifdef DEBUG_LOG
printf("BigSkip %d\n", *iSkipCount);
#endif
            pDest[i++] = 0x00; // big skip
            pDest[i++] = (unsigned char)(*iSkipCount - 1);
            *iSkipCount = 0;
            if (!bFinal && *iDiffCount > 0 && *iDiffCount <= 7) // diff count becomes 'first'
            {
               *iDiffCount |= 0x8000; // diff is now first
               *iLen = i;
               return;
            }
         }
      } // big skip
      if (*iSkipCount <= 7 && *iDiffCount > 0 && *iDiffCount <= 7) // 2 short
      { // skip/copy (both short)
#ifdef DEBUG_LOG
printf("skip/copy %d,%d\n", *iSkipCount, *iDiffCount);
#endif
         pDest[i++] = (unsigned char)((*iSkipCount << 3) | (*iDiffCount));
         memcpy(&pDest[i], pTemp, *iDiffCount);
         i += *iDiffCount;
         *iSkipCount = *iDiffCount = 0; // all handled
         *iLen = i;
         return;
      }
      if (*iSkipCount <=7 && *iDiffCount > 7) // need to do a short skip, long diff
      {
         if (*iSkipCount != 0) // store just the skip by itself
         {
#ifdef DEBUG_LOG
printf( "skip/copy %d,0\n", *iSkipCount);
#endif
            pDest[i++] = (unsigned char)(*iSkipCount << 3);
            *iSkipCount = 0;
         }
         pTemp += TryRepeat(iDiffCount, pTemp, pDest, &i);
         while (*iDiffCount >= 256) // store long diffs
         {
#ifdef DEBUG_LOG
printf("big copy 256\n");
#endif
            pDest[i++] = 0x40; // big copy
            pDest[i++] = 0xff; // max length = 256
            memcpy(&pDest[i], pTemp, 256);
            i += 256; pTemp += 256;
            *iDiffCount -= 256;
         } // while >= 256 diff
         if (*iDiffCount > 7) // wrap up the rest as a long diff
         {
#ifdef DEBUG_LOG
printf("big copy %d\n", *iDiffCount);
#endif 
            pDest[i++] = 0x40; // long copy
            pDest[i++] = (unsigned char)(*iDiffCount -1);
            memcpy(&pDest[i], pTemp, *iDiffCount);
            i += *iDiffCount;
            *iDiffCount = 0;
         }
         if (*iDiffCount > 0)
         {
            pDest[i++] = 0x40 | (*iDiffCount << 3); // short copy
            memcpy(&pDest[i], pTemp, *iDiffCount);
            i += *iDiffCount;
            *iDiffCount = 0;
         }
         *iLen = i; // return new length
         return;
      } // short skip, long diff
      if (*iSkipCount && *iDiffCount == 0 && bFinal) // write final skip
      {
#ifdef DEBUG_LOG
printf("final skip %d\n", *iSkipCount);
#endif
         pDest[i++] = *iSkipCount << 3;
         *iLen = i;
         return;
      }
      if (bFinal) // must write final diffs
      {
         while (*iDiffCount >= 256)
         {
#ifdef DEBUG_LOG
printf("big copy final 256\n");
#endif
            pDest[i++] = 0x40; // big copy
            pDest[i++] = 0xff; // max length = 256
            memcpy(&pDest[i], pTemp, 256);
            i += 256; pTemp += 256;
            *iDiffCount -= 256;
         }
         if (*iDiffCount > 7) // wrap up the rest as a long diff
         {
#ifdef DEBUG_LOG
printf("big copy final %d\n", *iDiffCount);
#endif
            pDest[i++] = 0x40; // long copy
            pDest[i++] = (unsigned char)(*iDiffCount -1);
            memcpy(&pDest[i], pTemp, *iDiffCount);
            i += *iDiffCount;
            *iDiffCount = 0;
         }
         if (*iDiffCount > 0)
         {
            pDest[i++] = 0x40 | (*iDiffCount << 3); // short copy
            memcpy(&pDest[i], pTemp, *iDiffCount);
            i += *iDiffCount;
            *iDiffCount = 0;
         }
      }
      if (*iSkipCount == 0 && *iDiffCount > 0) // now diff is first
         *iDiffCount |= 0x8000;
   } // skip first
   else // copy bytes are first
   {
      *iDiffCount &= 0x7fff;
      if (*iDiffCount > 7) // big diff first
      {
         pTemp += TryRepeat(iDiffCount, pTemp, pDest, &i);
         while (*iDiffCount >= 256) // long ones
         {
#ifdef DEBUG_LOG
printf("big copy 256\n");
#endif
            pDest[i++] = 0x40; // long copy
            pDest[i++] = 0xff; // max count = 256
            memcpy(&pDest[i], pTemp, 256);
            i += 256; pTemp += 256;
            *iDiffCount -= 256;
         }
         if (*iDiffCount > 7) // last long count
         {
#ifdef DEBUG_LOG
printf("big copy %d\n", *iDiffCount);
#endif
            pDest[i++] = 0x40;
            pDest[i++] = (unsigned char)(*iDiffCount - 1);
            memcpy(&pDest[i], pTemp, *iDiffCount);
            pTemp += *iDiffCount;
            i += *iDiffCount;
            *iDiffCount = 0;
         }
      } // big diff
      if (*iDiffCount) // small diff left over?
      {
         if (*iSkipCount <= 7) // small small
         {
#ifdef DEBUG_LOG
printf("copy/skip %d,%d\n", *iDiffCount, *iSkipCount);
#endif
            pDest[i++] = 0x40 | (unsigned char)((*iDiffCount << 3) | *iSkipCount);
            memcpy(&pDest[i], pTemp, *iDiffCount);
            pTemp += *iDiffCount;
            i += *iDiffCount;
            *iDiffCount = *iSkipCount = 0;
         }
         else // short diff, long skip
         {
#ifdef DEBUG_LOG
printf("copy/skip %d,0\n", *iDiffCount);
#endif
            pDest[i++] = 0x40 | (*iDiffCount << 3);
            memcpy(&pDest[i], pTemp, *iDiffCount);
            pTemp += *iDiffCount;
            i += *iDiffCount;
            *iDiffCount = 0;
            while (*iSkipCount >= 256)
            {
#ifdef DEBUG_LOG
printf("big skip 256\n");
#endif
               pDest[i++] = 0x00;
               pDest[i++] = 0xff; // skip 256
               *iSkipCount -= 256;
            } // while skip >= 256
            if (*iSkipCount > 7) // last big skip
            {
#ifdef DEBUG_LOG
printf("big skip %d\n", *iSkipCount);
#endif
               pDest[i++] = 0x00;
               pDest[i++] = *iSkipCount - 1;
               *iSkipCount = 0;
            }
         }
      }
      if (bFinal)
      {
            while (*iSkipCount >= 256)
            {
#ifdef DEBUG_LOG
printf("big skip final 256\n");
#endif
               pDest[i++] = 0x00;
               pDest[i++] = 0xff; // skip 256
               *iSkipCount -= 256;
            } // while skip >= 256
            if (*iSkipCount > 0) // last big skip
            {
#ifdef DEBUG_LOG
printf("big skip final %d\n", *iSkipCount);
#endif
               pDest[i++] = 0x00;
               pDest[i++] = *iSkipCount - 1;
               *iSkipCount = 0;
            }
      }
      if (*iDiffCount == 0 && *iSkipCount != 0) // left over skip, now it's first
         *iSkipCount |= 0x8000;
   } // copy first
   *iLen = i;
} /* CompressIt() */
//
// Convert the current GIF frame into 1-bpp by simple thresholding
//
void Make1Bit(unsigned char *pFrame, PIL_PAGE *pp)
{
int y, x, r, g, b;
unsigned char *s, *d;
unsigned char ucMask;

   memset(pFrame, 0, 128*8);
   // grab the current frame in "normal" bit/byte order
   // simple threshold to 1-bpp
   if (pp->cBitsperpixel == 16) // need to co
   {
      for (y=0; y<64; y++)
      {
         if (iTop != -1 && iLeft != -1)
            s = pp->pData + (iLeft*2) + (y+iTop)*pp->iPitch;
         else
            s = pp->pData + pp->iPitch * y;
         d = &pFrame[y*16];
         ucMask = 0x80;
         for (x=0; x<128; x++)
         {
            r = s[1] & 0xf8;
            g = (s[0] & 0xe0) >> 3;
            g |= ((s[1] & 7) << 5);
            b = ((s[0] & 0x1f) << 3);
            if ((r + g + b) > 384) // it's white
               d[0] |= ucMask;
            s += 2;
            ucMask >>= 1;
            if (ucMask == 0)
            {
               d++;
               ucMask = 0x80;
            }
         } // for x
      } // for y
   } // 16-bpp
// Invert if requested
   if (bInvert)
   {
     for (x=0; x<1024; x++)
     {
        pFrame[x] = ~pFrame[x];
     }
   }
} /* Make1Bit() */
//
// Compress the current frame against the previous
//
void AddFrame(PIL_PAGE *pp, unsigned char *pPrev, unsigned char *pData, int *iSize, int bFirst)
{
int iLen = *iSize;
unsigned char *pFrame = PILIOAlloc(128*8); // temporary frame
unsigned char ucTemp[1024];
int iDiffCount, iSkipCount;
int x, y;

   Make1Bit(pFrame, pp);

//
// Compress the data using the pixel layout of the SSD1306
// vertical bytes with the LSB at the top
// 128 bytes per row, 8 rows total
//
   if (bFirst) // First frame only has intra coding, not inter
   {
      iDiffCount = iSkipCount = 0;
      for (y = 0; y<64; y+=8)
      {
         for (x=0; x<128; x++)
         {
            ucTemp[iDiffCount++] = GetByte(pFrame, x, y);
         } // for x
      } // for y
      iDiffCount |= 0x8000; // mark it as 'first'
      CompressIt(pData, &iLen, &iSkipCount, &iDiffCount, ucTemp, 1); // do it in one shot
   }
   else
   { // find differences between the current and previous frame
   iSkipCount = 0;
   iDiffCount = 0;
   x = y = 0;
   while (y < 64)
   {
      while (y < 64 && GetByte(pFrame, x, y) == GetByte(pPrev, x, y)) // unchanged bytes from previous frame
      {
         if (iDiffCount == 0 && iSkipCount == 0)
            iSkipCount = 0x8000; // mark this as being first
         iSkipCount++;
         x++;
         if (x == 128) // next line
         {
            x = 0;
            y += 8;
         }
      } // while counting "skip" bytes 
      if ((iSkipCount & 0x7fff) && (iDiffCount & 0x7fff)) // if have both, store them
         CompressIt(pData, &iLen, &iSkipCount, &iDiffCount, ucTemp, 0);
      while (y < 64 && GetByte(pFrame, x, y) != GetByte(pPrev, x, y)) // changed
      {
         if (iDiffCount == 0 && iSkipCount == 0)
            iDiffCount = 0x8000; // mark this as being first
         ucTemp[(iDiffCount & 0x7fff)] = GetByte(pFrame, x, y);
         iDiffCount++;
         x++;
         if (x == 128) // next line
         {
            x = 0;
            y += 8;
         }
      } // while counting "copy" bytes
      if ((iSkipCount & 0x7fff) && (iDiffCount & 0x7fff)) // if have both, store them
         CompressIt(pData, &iLen, &iSkipCount, &iDiffCount, ucTemp, 0);
   } // while compressing frame
   CompressIt(pData, &iLen, &iSkipCount, &iDiffCount, ucTemp, 1); // compress last part
   } // not the first frame
   memcpy(pPrev, pFrame, 128*8); // old becomes the current
   PILIOFree(pFrame);
   *iSize = iLen;
} /* AddFrame() */
//
// Play the frames back into destination image to test
//
void PlayBack(unsigned char *pData, int iLen)
{
int x, y;
int iFrame, i, j, iOff;
unsigned char b, bCode;
unsigned char ucScreen[2024]; // destination bitmap
unsigned char ucBMP[1024]; // for generating output BMP

   iFrame = iOff = 0;
   while (iOff < iLen) // process all compressed data
   {
      i = 0; // graphics offset on SSD1306
      while (i < 1024) // while decompressing the current frame
      {
      bCode = pData[iOff++];
      switch (bCode & 0xc0) // different compression types
      {
         case 0x00: // skip/copy
            if (bCode == 0) // big skip
            {
               b = pData[iOff++];
               i += b + 1;
            }
            else // skip/copy
            {
               i += ((bCode & 0x38) >> 3); // skip amount
               memcpy(&ucScreen[i], &pData[iOff], bCode & 7);
               iOff += (bCode & 7);
               i += bCode & 7;
            }
            break;
         case 0x40: // copy/skip
            if (bCode == 0x40) // big copy
            {
               b = pData[iOff++];
               j = b + 1;
               memcpy(&ucScreen[i], &pData[iOff], j); // copy
               iOff += j;
               i += j;
            }
            else
            {
               j = ((bCode & 0x38) >> 3);
               memcpy(&ucScreen[i], &pData[iOff], j); // copy
               iOff += j;
               i += j;
               i += (bCode & 7); // skip
            }
            break;
         case 0x80: // repeat
         case 0xc0:
            j = (bCode & 0x7f) + 1;
            b = pData[iOff++];
            memset(&ucScreen[i], b, j);
            i += j;
            break;  
      } // switch on code type
      } // while decompressing the current frame
// Convert SSD1306 style bytes into "normal" byte order
      memset(ucBMP, 0, 1024);
      i = 0;
      for (y=0; y<64; y+=8)
      {
         for (x=0; x<128; x++)
         {
            bCode = 0x80 >> (x & 7);
            b = ucScreen[i];
            for (j=0; j<8; j++)
            {
               if (b & 1) // LSB first
                  ucBMP[(x>>3) + ((y+j)*16)] |= bCode;
               b >>= 1;
            }
            i++; // next byte column
         }
      } // for y
#ifdef SAVE_OUTPUT_FRAMES
      // Write it to a file
      {
         PIL_FILE pf;
         PIL_PAGE pp;
         char szName[32];
            memset(&pp, 0, sizeof(pp));
            pp.iWidth = 128;
            pp.iHeight = 64;
            pp.iPitch = 16;
            pp.cBitsperpixel = 1;
            pp.iDataSize = 1024;
            pp.cCompression = PIL_COMP_NONE;
            pp.pData = ucBMP;
            pp.cFlags = PIL_PAGEFLAGS_TOPDOWN;
            sprintf(szName, "out%d.bmp", iFrame);
            PILCreate(szName, &pf, 0, PIL_FILE_WINBMP);
            PILWrite(&pf, &pp, 0);
            PILClose(&pf);
      }
#endif // SAVE_OUTPUT_FRAMES
      iFrame++;
   } // while processing compressed data
} /* PlayBack() */

//
// Write the binary data as C statements
// ready to drop into an Arduino project
//
void MakeCode(void *ohandle, unsigned char *pData, int iLen)
{
int i;
char szLine[256], szTemp[16];

	PILIOWrite(ohandle, "const byte bAnimation[] PROGMEM = {\n", 36); 
	for (i=0; i<iLen; i++)
	{
		if ((i & 15) == 0)
			sprintf(szLine,"  ");
		if (i == iLen-1)
			sprintf(szTemp, "0x%02x", pData[i]);
		else
			sprintf(szTemp, "0x%02x,", pData[i]);
		strcat(szLine, szTemp);
		if (i == iLen-1 || (i & 15) == 15)
		{
			strcat(szLine, "\n");
			PILIOWrite(ohandle, szLine, strlen(szLine));
		}
	}
	PILIOWrite(ohandle, "};\n", 3);
} /* MakeCode() */

int main( int argc, char *argv[ ], char *envp[ ] )
{
PIL_FILE pf;
PIL_PAGE pp1, pp2;
int err;
int i, iLen;
unsigned char *pCompressed, *pPrevious;

   if (argc < 3)
      {
      ShowHelp();
      return 0;
      }
   parse_opts(argc, argv);
   pCompressed = NULL;
   iLen = 0;
	err = PILOpen(szIn, &pf, 0, "BitBank", 0x35c4);
	if (err == 0)
	{
		pCompressed = PILIOAlloc(0x40000); // try 256k
		pPrevious = PILIOAlloc(128*8); // previous frame for compare
		memset(pPrevious, 0, 128*8);
		iLen = 0;
		printf("size: %dx%d, bpp=%d, frames=%d\n", pf.iX, pf.iY, pf.cBpp, pf.iPageTotal);
		// Read each frame one at a time
		memset(&pp2, 0, sizeof(pp2));
		pp2.iWidth = pf.iX;
		pp2.iHeight = pf.iY;
		pp2.cBitsperpixel = 16; // has to be
		pp2.pData = PILIOAlloc(512*512*2);
		pp2.iPitch = 1024;
		pp2.iDataSize = pp2.iPitch * pp2.iHeight;
		pp2.cFlags = PIL_PAGEFLAGS_TOPDOWN;
		pp2.cCompression = PIL_COMP_NONE;
		pp2.pPalette = PILIOAlloc(2048);
		for (i=0; i<pf.iPageTotal; i++)
		{
		PIL_PAGE ppSrc;

	                err = PILRead(&pf, &pp1, i, 0);
        	        if (err)
                	{       
                        	printf("PILRead returned %d\n", err);
                        	return -1;
                	}

			memset(&ppSrc, 0, sizeof(ppSrc));
			ppSrc.cCompression = PIL_COMP_NONE;
			err = PILConvert(&pp1, &ppSrc, 0, NULL, NULL);
			if (err)
			{
				printf("PILConvert returned %d\n", err);
				return -1;
			}
			if (i == 0) // get global color table from first frame
			{
				memcpy(pp2.pPalette, ppSrc.pPalette, 768);
			}
			err = PILAnimateGIF(&pp2, &ppSrc);
			PILFree(&ppSrc);
			PILFree(&pp1);
			if (err == 0)
			{
#ifdef DEBUG_LOG
printf("About to enter AddFrame() for frame %d\n", i);
#endif
				AddFrame(&pp2, pPrevious, pCompressed, &iLen, i == 0);
#ifdef SAVE_INPUT_FRAMES
				{
				PIL_FILE pf2;
                                char szName[32];
					sprintf(szName, "in%d.bmp", i);
					PILCreate(szName,&pf2, 0, PIL_FILE_WINBMP);
					PILWrite(&pf2, &pp2, 0);
					PILClose(&pf2);
				}
#endif // SAVE_INPUT_FRAMES
			}
			else
			{
				printf("Frame: %d, PILAnimate returned %d\n", i, err);
			}
		} // for i
		if (iLen)
		{
		void *ohandle;
		ohandle = PILIOCreate(szOut);
			printf("Generated %d bytes of compressed output\n", iLen);
			if (ohandle != (void *)-1)
			{
				if (bC) // write C code
				{
					MakeCode(ohandle, pCompressed, iLen);
				}
				else // write binary data
				{
					PILIOWrite(ohandle, pCompressed, iLen);
				}
				PILIOClose(ohandle);
			}
		}
		PILClose(&pf);
	} // if file loaded successfully
   PlayBack(pCompressed, iLen);
   return 0;
}

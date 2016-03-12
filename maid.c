/*
maid
Copyright (c) 2016 Ben "GreaseMonkey" Russell

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

//define DO_MIDDLE_BITS

int img_width, img_height;
uint8_t palette[256][3];
uint8_t raw_input[50][160][3];
uint8_t has_been_touched[50][160];
int palette_len;
int img_bpp;

int lc_array[50][256];
int lc_array_membership[50][256];
int lc_array_count[50];

int8_t typical_association[256];
int gc_array[160];
int gc_array_membership[256];
int gc_array_count;

int convert_char(int v)
{
	int r = 0;
	if(v & 0x01) r |= 0x80;
	if(v & 0x02) r |= 0x40;
	if(v & 0x04) r |= 0x20;
	if(v & 0x08) r |= 0x04;
	if(v & 0x10) r |= 0x10;
	if(v & 0x20) r |= 0x02;
	if(v & 0x40) r |= 0x08;
	if(v & 0x80) r |= 0x01;
	return r;
}

void xuniputchar(int c)
{
	c -= 33;
	c %= 94;
	c += 33;
	putchar(c);
}

void get_ocpal_direct(int idx, int *r, int *g, int *b)
{
	if(idx < 16) {
		*r = *g = *b = ((idx+1)*255)/17;
		//*r = *g = *b = 0;

		/*
		*r = palette[idx][0];
		*g = palette[idx][1];
		*b = palette[idx][2];
		*/

	} else {
		// preferred, but asie wasn't briefed on this
		//*g = (idx-16)%8;
		//*r = ((idx-16)/8)%6;
		//*b = ((idx-16)/8)/6;

		// what CTIF uses
		idx -= 16;
		*b = (idx)%5;
		*g = (idx/5)%8;
		*r = (idx/5)/8;

		*r = (*r*255)/5;
		*g = (*g*255)/7;
		*b = (*b*255)/4;
	}
}

void analyse(const char *out_fname)
{
	FILE *fp;

	gc_array_count = 0;
	for(int i = 0; i < 256; i++) {
		gc_array_membership[i] = 0;
		typical_association[i] = -1;
	}

#if 1
	// if bg == fg, force bg mask
	for(int y = 0; y < img_height; y++)
	for(int x = 1; x < 160; x++)
	if(raw_input[y][x][0] == raw_input[y][x][1])
	{
		raw_input[y][x][2] = 0x00;
	}

	// ensure bg < fg beforehand
	/*
	for(int y = 0; y < img_height; y++)
	for(int x = 1; x < 160; x++)
	if(raw_input[y][x][0] > raw_input[y][x][1])
	{
		int t = raw_input[y][x][0];
		raw_input[y][x][0] = raw_input[y][x][1];
		raw_input[y][x][1] = t;
		raw_input[y][x][2] ^= 0xFF;
	}
	*/

	// main reversal + masking procedure
	// FIXME: typical association shit is broken
	for(int y = 0; y < img_height; y++)
	{
		int has_set_fg = 0;
		int has_set_bg = 0;
		int blank_fg_x = 0;
		int blank_bg_x = 0;

		if(0
			|| (raw_input[y][0][2] != 0xFF
				&& typical_association[raw_input[y][0][0]] == 0)
			|| (raw_input[y][0][2] != 0x00
				&& typical_association[raw_input[y][0][1]] == 1)
			|| (typical_association[raw_input[y][0][0]] == 1
				&& typical_association[raw_input[y][0][1]] != 1)
			|| (typical_association[raw_input[y][0][0]] == 0
				&& typical_association[raw_input[y][0][1]] != 0)
				|| 0) {

			int t = raw_input[y][0][0];
			raw_input[y][0][0] = raw_input[y][0][1];
			raw_input[y][0][1] = t;
			raw_input[y][0][2] ^= 0xFF;
		}

		if(raw_input[y][0][2] != 0xFF) {
			has_set_bg = 1;
			if(typical_association[raw_input[y][0][0]] == -1) {
				typical_association[raw_input[y][0][0]] = 0;
			}
		}

		if(raw_input[y][0][2] != 0x00) {
			has_set_fg = 1;
			if(typical_association[raw_input[y][0][0]] == -1) {
				typical_association[raw_input[y][0][0]] = 1;
			}
		}

		for(int x = 1; x < 160; x++)
		{
			if(0
				|| raw_input[y][x][0] == raw_input[y][x-1][1]
				|| raw_input[y][x][1] == raw_input[y][x-1][0]
				/*
				|| (raw_input[y][0][2] != 0xFF
					&& typical_association[raw_input[y][0][0]] == 0)
				|| (raw_input[y][0][2] != 0x00
					&& typical_association[raw_input[y][0][1]] == 1)
				|| (typical_association[raw_input[y][0][0]] == 1
					&& typical_association[raw_input[y][0][1]] != 1)
				|| (typical_association[raw_input[y][0][0]] == 0
					&& typical_association[raw_input[y][0][1]] != 0)
				*/
					|| 0) {

				int t = raw_input[y][x][0];
				raw_input[y][x][0] = raw_input[y][x][1];
				raw_input[y][x][1] = t;
				raw_input[y][x][2] ^= 0xFF;
			}

			if(raw_input[y][x][2] == 0xFF) {
				raw_input[y][x][0] = raw_input[y][x-1][0];
				blank_bg_x = x;
				has_set_bg = 0;
			} else {
				/*
				if(typical_association[raw_input[y][x][0]] == -1) {
					typical_association[raw_input[y][x][0]] = 0;
				}
				*/
				if(!has_set_bg) {
					has_set_bg = 1;
					for(int sx = blank_bg_x; sx < x; sx++) {
						raw_input[y][sx][0] = raw_input[y][x][0];
					}
				}
			}

			if(raw_input[y][x][2] == 0x00) {
				raw_input[y][x][1] = raw_input[y][x-1][1];
				blank_fg_x = x;
				has_set_fg = 0;
			} else {
				/*
				if(typical_association[raw_input[y][x][0]] == -1) {
					typical_association[raw_input[y][x][0]] = 1;
				}
				*/
				if(!has_set_fg) {
					has_set_fg = 1;
					for(int sx = blank_fg_x; sx < x; sx++) {
						raw_input[y][sx][1] = raw_input[y][x][1];
					}
				}
			}
		}
	}

	// ensure consistent ordering within two pairs
	memset(has_been_touched, 0, sizeof(has_been_touched));
	for(int sy = 0; sy < img_height; sy++)
	for(int sx = 0; sx < img_width;  sx++)
	{
		if(!has_been_touched[sy][sx]) {
			int bg = raw_input[sy][sx][0];
			int fg = raw_input[sy][sx][1];
			for(int dy = 0; dy < img_height; dy++)
			for(int dx = 0; dx < img_width;  dx++)
			{
				int dbg = raw_input[dy][dx][0];
				int dfg = raw_input[dy][dx][1];

				if(dbg == fg && dfg == bg) {
					raw_input[dy][dx][0] = dfg;
					raw_input[dy][dx][1] = dbg;
					raw_input[dy][dx][2] ^= 0xFF;
				}

				if(dbg == bg && dfg == fg) {
					has_been_touched[dy][dx] = 1;
				}
			}
		}
	}
#endif

	// calculate statistics
	for(int y = 0; y < img_height; y++)
	{
		lc_array_count[y] = 0;

		for(int i = 0; i < 256; i++) {
			lc_array_membership[y][i] = 0;
		}

		for(int x = 0; x < 160; x++) {
			for(int j = 0; j < 2; j++) {
				if(raw_input[y][x][2] == (j == 0 ? 0xFF : 0x00)) {
					continue;
				}

				int c = raw_input[y][x][j];

				if(!lc_array_membership[y][c]) {
					lc_array[y][lc_array_count[y]++] = c;
					lc_array_membership[y][c] = lc_array_count[y];
				}

				if(!gc_array_membership[c]) {
					gc_array[gc_array_count++] = c;
					gc_array_membership[c] = gc_array_count;
				}
			}
		}

		printf("%3d: %3d colours\n", y, lc_array_count[y]);
	}

	printf("\ntotal: %3d colours\n", gc_array_count);

	// print stuff
	for(int y = 0; y < img_height; y++)
	{
		for(int j = 0; j < 2; j++)
		{
			for(int x = 0; x < 160; x++) {
				if(1 && raw_input[y][x][2] == (j == 0 ? 0xFF : 0x00)) {
					xuniputchar(' ');
					continue;
				}

				//putchar(' '+lc_array_membership[y][raw_input[y][x][j]]);
				xuniputchar(' '+gc_array_membership[raw_input[y][x][j]]);
			}
			printf("\n");
		}
		printf("\n");
	}

	// output image
	fp = fopen(out_fname, "wb");
	fwrite("OCGPU", 5, 1, fp);
	fputc(img_bpp, fp);
	fputc(img_width, fp);
	fputc(img_height, fp);
	fputc(palette_len, fp);
	fwrite(palette, 3, palette_len, fp);

	int fg = -1;
	int bg = -1;
	int run_x = 1;
	int run_y = 1;

	int run_len = 0;
	uint8_t run_data[160];
	int run_mask = 0;

	memset(has_been_touched, 0, sizeof(has_been_touched));
	int touches_to_do = img_width * img_height;
	int can_change_two_things = 1;

	// TODO: better algorithm
	while(touches_to_do > 0) {
		int touched_a_colour = 0;

		for(int y = 0; y < img_height; y++)
		for(int x = 0; x <= img_width; x++) {
			// perform run
			if(x != img_width && (!touched_a_colour) && !has_been_touched[y][x]) {
				if(((raw_input[y][x][0] == fg && raw_input[y][x][2] != 0x00)
				|| (raw_input[y][x][1] == bg && raw_input[y][x][2] != 0xFF)) && fg != bg) {
					int t = raw_input[y][x][0];
					raw_input[y][x][0] = raw_input[y][x][1];
					raw_input[y][x][1] = t;
					raw_input[y][x][2] ^= 0xFF;
				}
			}

			if(x != img_width && !has_been_touched[y][x]) {
				if((raw_input[y][x][0] == fg || raw_input[y][x][2] == 0x00)
				&& (raw_input[y][x][1] == bg || raw_input[y][x][2] == 0xFF) && fg != bg) {
					int t = raw_input[y][x][0];
					raw_input[y][x][0] = raw_input[y][x][1];
					raw_input[y][x][1] = t;
					raw_input[y][x][2] ^= 0xFF;
				}
			}

			if(x == img_width || x == 0 ||
				(raw_input[y][x][2] != 0xFF && ((int)raw_input[y][x][0]) != bg
#ifdef DO_MIDDLE_BITS
					&& has_been_touched[y][x]
#endif
					) ||
				(raw_input[y][x][2] != 0x00 && ((int)raw_input[y][x][1]) != fg
#ifdef DO_MIDDLE_BITS
					&& has_been_touched[y][x]
#endif
					) ||
				((!has_been_touched[y][x]) && !touched_a_colour) || 
				(run_mask == 0 && (!has_been_touched[y][x])) || 
					0) {

				// output run
				if((run_mask & 0x04) && run_len != 0) {
					fputc(run_mask, fp);
					if(run_mask & 0x01) {
						fputc(bg, fp);
					}
					if(run_mask & 0x02) {
						fputc(fg, fp);
					}
					if(run_mask & 0x04) {
						fputc(run_x, fp);
						fputc(run_y, fp);

						// TODO: RLE
						fputc(0x01, fp);
						fputc(run_len, fp);
						fwrite(run_data, run_len, 1, fp);
					}
				}

				// check if we need to kill this run
				if(x == img_width) { 
					run_mask = 0;
					run_len = 0;
					break;
				}

				// start new run
				run_x = x+1;
				run_y = y+1;
				run_len = 0;
				run_mask = 0;

				if(touched_a_colour
					? 1
					&& (raw_input[y][x][2] == 0xFF || (int)raw_input[y][x][0] == bg)
					&& (raw_input[y][x][2] == 0x00 || (int)raw_input[y][x][1] == fg)
					: (!has_been_touched[y][x]) && (can_change_two_things
					? 1
					: 0
					|| (raw_input[y][x][2] == 0xFF || (int)raw_input[y][x][0] == bg)
					|| (raw_input[y][x][2] == 0x00 || (int)raw_input[y][x][1] == fg))
					) {

					run_mask = 0x04;

					//if((x == 0 || raw_input[y][x][2] != 0xFF) && ((int)raw_input[y][x][0]) != bg) {
					if(raw_input[y][x][2] != 0xFF && ((int)raw_input[y][x][0]) != bg) {
						bg = raw_input[y][x][0];
						run_mask |= 0x01;
					}
					
					//if((x == 0 || raw_input[y][x][2] != 0x00) && ((int)raw_input[y][x][1]) != fg) {
					if(raw_input[y][x][2] != 0x00 && ((int)raw_input[y][x][1]) != fg) {
						fg = raw_input[y][x][1];
						run_mask |= 0x02;
					}

					touched_a_colour = 1;
				}
			}

			// end run if at end
			if(x == img_width) {
				break;
			}

			// add char
			// TODO: RLE
			if(run_mask & 0x04) {
				assert(run_len < 160);
				run_data[run_len++] = convert_char(raw_input[y][x][2]);
				if(!has_been_touched[y][x]) {
					if((raw_input[y][x][0] == bg || raw_input[y][x][2] == 0xFF)
					&& (raw_input[y][x][1] == fg || raw_input[y][x][2] == 0x00)) {
						has_been_touched[y][x] = 1;
						touches_to_do--;
					}
				}
			}
		}

		assert(can_change_two_things ? touched_a_colour : 1);
		can_change_two_things = (!touched_a_colour);
		//printf("touch rem = %d %d %d\n", touches_to_do, touched_a_colour, can_change_two_things);
	}

	fclose(fp);
}

int main(int argc, char *argv[])
{
	FILE *fp;
	char magic[4];

	// load CTIF file
	fp = fopen(argv[1], "rb");

	magic[3] = 0; fread(magic, 4, 1, fp);
	if(memcmp(magic, "CTIF", 4)) {
		fprintf(stderr, "ERR: not a CTIF file\n");
		fflush(stderr);
		fclose(fp);
		return 1;
	}

	magic[3] = 0xFF; fread(magic, 4, 1, fp);
	if(memcmp(magic, "\x01\x00\x01\x00", 4)) {
		fprintf(stderr, "ERR: unsupported CTIF file (need CTIF v1, OC var0)\n");
		fflush(stderr);
		fclose(fp);
		return 1;
	}

	img_width  = fgetc(fp); img_width  |= fgetc(fp)<<8;
	img_height = fgetc(fp); img_height |= fgetc(fp)<<8;

	if(img_width > 160 || img_height > 50 || img_width < 1 || img_height < 1) {
		fprintf(stderr, "ERR: unsupported CTIF file (max size 160x50 chars)\n");
		fflush(stderr);
		fclose(fp);
		return 1;
	}

	magic[1] = 0xFF; fread(magic, 2, 1, fp);
	if(memcmp(magic, "\x02\x04", 2)) {
		fprintf(stderr, "ERR: unsupported CTIF file (need 2x4 chars)\n");
		fflush(stderr);
		fclose(fp);
		return 1;
	}

	img_bpp = fgetc(fp);
	if(fgetc(fp) != 3 || (img_bpp != 8 && img_bpp != 4)) {
		fprintf(stderr, "ERR: unsupported CTIF file (need 3-byte palette entries, 4 or 8 bpp)\n");
		fflush(stderr);
		fclose(fp);
		return 1;
	}

	palette_len = fgetc(fp);
	palette_len |= fgetc(fp)<<8;
	if(palette_len < 0 || palette_len > 16) {
		fprintf(stderr, "ERR: unsupported CTIF file (palette size unsupported)\n");
		fflush(stderr);
		fclose(fp);
		return 1;

	}

	for(int i = 0; i < 256; i++)
	{
		int r, g, b;

		if(i < palette_len) {
			r = fgetc(fp);
			g = fgetc(fp);
			b = fgetc(fp);

		} else {
			get_ocpal_direct(i, &r, &g, &b);
		}

		palette[i][0] = r;
		palette[i][1] = g;
		palette[i][2] = b;
	}

	for(int y = 0; y < img_height; y++)
	for(int x = 0; x < img_width;  x++)
	{
		int fg;
		int bg = fgetc(fp);

		if(img_bpp <= 4) {
			fg = bg&((1<<img_bpp)-1);
			bg >>= img_bpp;
		} else {
			fg = fgetc(fp);
		}

		int cmask = fgetc(fp);

		assert(fg >= 0 && fg < (1<<img_bpp));
		assert(bg >= 0 && bg < (1<<img_bpp));

		raw_input[y][x][0] = bg;
		raw_input[y][x][1] = fg;
		raw_input[y][x][2] = cmask;
	}

	fclose(fp);

	analyse(argv[2]);

	return 0;
}

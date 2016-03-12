/*
requires 320x200 top-left-origin 32bpp uncompressed TGA files (yes, it's hella picky)

how to make these in gimp:
1. Image -> Mode -> RGB
2. right-click background layer in the layer list, click Add Alpha Channel (unless greyed out)
5. Image -> Scale Image, ensure width is >= 320 and height is >= 200 and aspect locked
6. Image -> Canvas Size, set size to 320 x 200 and pan appropriately
7. Layer -> Layer to Image Size
8. File -> Export -> export as a .tga image
9. disable RLE, use Top-Left origin

for actual tier 2 support you will need to do 160 x 100

what I use for testing:
export N=sakuya2.tga && cc -fopenmp -O3 -g -o dog dog.c -lm && time ./dog "$N" out.ctif test.tga && convert test.tga -filter Box -resize 960x600 test2.png && display test2.png
*/

/*
dog
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

// VIM tip: select text then 79gw to wrap to 79 chars wide ;)

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <math.h>

// start of editables

// you probably only want one of these at a time
// but both is still valid
#define POSITIONAL_DITHER
//define ERROR_DITHER

// 4bpp mode
// note that for actual T2 support you will need to set CW,CH to 80,25
//define TIER2

#define CW 160
#define CH 50

// slower but much more thorough method, best left enabled
#define BRUTE_FORCE_BLOCK

// end of editables

// really, the algo is not set up for changing this
// so stop being a CC scrub and use the better mod already
#define BW 2
#define BH 4
#define VW (CW*BW)
#define VH (CH*BH)

void get_ocpal(int idx, int *r, int *g, int *b);
int mse3(int *u, uint8_t *v);

uint8_t header[18];
uint8_t has_exact_match[VH][VW];
int ocpal_dist[VH][VW];
int ocpal_pidx[VH][VW];
uint8_t raw_input[VH][VW][4];
int raw_input_yuv[VH][VW][4];
uint8_t raw_output[VH][VW][4];

#ifdef TIER2
uint8_t ctif_output[CH][CW][2];
#else
uint8_t ctif_output[CH][CW][3];
#endif

#ifdef ERROR_DITHER
int input_error[VH+1][VW][4];
#endif

#ifdef POSITIONAL_DITHER
int dithertab[4][4] = {
	{ 0x0, 0x8, 0x2, 0xA },
	{ 0xC, 0x4, 0xE, 0x6 },
	{ 0x3, 0xB, 0x1, 0x9 },
	{ 0xF, 0x7, 0xD, 0x5 },
};

int dither_split_tab[3][16] = {
	{ 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF },
	//{ 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x0, 0x1, 0x2, 0x3, 0x4 },
	//{ 0xB, 0xC, 0xD, 0xE, 0xF, 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA },
	{ 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF },
	{ 0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF },
};
#endif

int mse3(int *u, uint8_t *v)
{
	int u0 = u[0];
	int u1 = u[1];
	int u2 = u[2];

	int v0 = v[0];
	int v1 = v[1];
	int v2 = v[2];

	int d0 = u0 - v0;
	int d1 = u1 - v1;
	int d2 = u2 - v2;

	return d0*d0 + d1*d1 + d2*d2;
}

void to_ycbcr(int r, int g, int b, int *restrict l, int *restrict cb, int *restrict cr)
{
	*l  = ( 76*r + 150*g +  29*b +128)>>8;
	*cb = ((-37*r -  74*g + 111*b +128)>>8) + 0x80;
	*cr = ((78*r - 65*g -  13*b +128)>>8) + 0x80;

	if(*l < 0) *l = 0;
	if(*cb < 0) *cb = 0;
	if(*cr < 0) *cr = 0;

	if(*l > 255) *l = 255;
	if(*cb > 255) *cb = 255;
	if(*cr > 255) *cr = 255;
}

void from_ycbcr(int l, int cb, int cr, int *restrict r, int *restrict g, int *restrict b)
{
	cb -= 0x80;
	cr -= 0x80;

	cr <<= 1;
	*r = ( 256*l +  0*cb +292*cr + 128)>>8;
	*g = ( 256*l -101*cb -148*cr + 128)>>8;
	*b = ( 256*l +520*cb +  0*cr + 128)>>8;

	if(*r < 0x00) *r = 0x00;
	if(*r > 0xFF) *r = 0xFF;
	if(*g < 0x00) *g = 0x00;
	if(*g > 0xFF) *g = 0xFF;
	if(*b < 0x00) *b = 0x00;
	if(*b > 0xFF) *b = 0xFF;
}

int mse3_to_yuv(int *u, uint8_t *v)
{
	int u0 = u[0];
	int u1 = u[1];
	int u2 = u[2];

	int v0 = v[0];
	int v1 = v[1];
	int v2 = v[2];

	to_ycbcr(u0, u1, u2, &u0, &u1, &u2);
	to_ycbcr(v0, v1, v2, &v0, &v1, &v2);

	int d0 = u0 - v0;
	int d1 = u1 - v1;
	int d2 = u2 - v2;

	return d0*d0 + d1*d1 + d2*d2;
}

int convert_to_ctif(int pal)
{
	/*
	if(pal >= 16 && pal <= 255) {
		pal -= 16;
		int g = (pal%8);
		int r = (pal/8)%6;
		int b = (pal/8)/6;
		assert(r >= 0 && r < 6);
		assert(g >= 0 && g < 8);
		assert(b >= 0 && b < 5);
		pal = 16+b+5*(g+8*r);
		return pal;
	} else {
		assert(pal >= 0 && pal < 16);
		return pal;
	}
	*/
	return pal;
}

int base_pal[256][3];
int base_pal_yuv[256][3];

void compute_pal(void)
{
	// compute which colours have exact match
	for(int y = 0; y < VH; y++)
	for(int x = 0; x < VH; x++)
	{
		int best_dist = 1;
		int best_idx = 0;
		has_exact_match[y][x] = 0;
#ifndef TIER2
		for(int i = 16; i < 256; i++)
		{
			int pr, pg, pb;
			get_ocpal(i, &pr, &pg, &pb);
			int grp[3] = {pr, pg, pb};
			int dist = mse3(grp, raw_input[y][x]);

			if(i == 16 || dist < best_dist)
			{
				best_dist = dist;
				best_idx = i;
				if(dist == 0) {
					has_exact_match[y][x] = 1;
					break;
				}
			}
		}
#endif

		ocpal_dist[y][x] = sqrt(best_dist)+0.5;
	}

	// create box
	int cmin[3] = {0xFF, 0xFF, 0xFF};
	int cmax[3] = {0x00, 0x00, 0x00};

	for(int y = 0; y < VH; y++)
	for(int x = 0; x < VH; x++)
	for(int j = 0; j < 3; j++)
	{
		if(has_exact_match[y][x]) {
			continue;
		}

		if(cmin[j] > raw_input_yuv[y][x][j]) {
			cmin[j] = raw_input_yuv[y][x][j];
		}

		if(cmax[j] < raw_input_yuv[y][x][j]) {
			cmax[j] = raw_input_yuv[y][x][j];
		}
	}

	// create initial 8 points
	for(int i = 0; i < 8; i++)
	for(int j = 0; j < 3; j++)
	{
		base_pal_yuv[i][j] = ((1<<j)&i) ? cmax[j] : cmin[j];
	}
	
	// move cmin, cmax in w/ 1/3 separation
	for(int j = 0; j < 3; j++)
	{
		int vmin = cmin[j];
		int vmax = cmax[j];
		int vdiff = vmax - vmin;

		cmin[j] = (vmin + vdiff/3);
		cmax[j] = (vmax - vdiff/3);
	}

	// now create inner 1/3 points
	for(int i = 0; i < 8; i++)
	for(int j = 0; j < 3; j++)
	{
		base_pal_yuv[i+8][j] = ((1<<j)&i) ? cmax[j] : cmin[j];
	}

	// apply k-means
	for(int reps = 0; reps < 10; reps++)
	{
		printf("pal rep %d\n", reps);

		int each_count[16];
		int each_total[16][3];

		memset(each_count, 0, sizeof(each_count));
		memset(each_total, 0, sizeof(each_total));

		// XXX: needs to avoid colours that map EXACTLY to an OC 6:8:5 colour
		for(int y = 0; y < VH; y++)
		for(int x = 0; x < VW; x++)
		{
			if(has_exact_match[y][x]) {
				continue;
			}

			int nearest = 0;
			int near_dist = -1;

			for(int i = 0; i < 16; i++)
			{
				int dist = 0;
				for(int j = 0; j < 3; j++)
				{
					int cd = base_pal_yuv[i][j] - (int)raw_input_yuv[y][x][j];
					dist += cd*cd;
				}

				if(i == 0 || dist < near_dist) {
					nearest = i;
					near_dist = dist;
				}
			}

			each_count[nearest] += ocpal_dist[y][x];

			for(int j = 0; j < 3; j++)
			{
				each_total[nearest][j] += ocpal_dist[y][x]*(int)(raw_input_yuv[y][x][j]);
			}
		}

		// move pal if possible
		for(int i = 0; i < 16; i++)
		{
			// if nothing uses this colour, pick a random one
			if(each_count[i] == 0) {
				base_pal[i][0] = (rand()>>7)&0xFF;
				base_pal[i][1] = (rand()>>7)&0xFF;
				base_pal[i][2] = (rand()>>7)&0xFF;
				continue;
			}

			for(int j = 0; j < 3; j++)
			{
				base_pal_yuv[i][j] = (each_total[i][j] + each_count[i]/2)/each_count[i];
			}

			int r, g, b;
			from_ycbcr(
				base_pal_yuv[i][0],
				base_pal_yuv[i][1],
				base_pal_yuv[i][2],
				&r, &g, &b);

			base_pal[i][0] = r;
			base_pal[i][1] = g;
			base_pal[i][2] = b;

			for(int j = 0; j < 3; j++)
			{
				assert(base_pal[i][j] >= 0x00);
				assert(base_pal[i][j] <= 0xFF);
			}
		}

	}

	// spit out pal
	for(int i = 0; i < 16; i++)
	{
		for(int j = 0; j < 3; j++)
		{
			printf("%02X", base_pal[i][j]);
		}

		printf("\n");
	}

#ifdef ERROR_DITHER
	// precalc initial error
	for(int y = 0; y < VH; y++)
	for(int x = 0; x < VH; x++)
	{
		int pr, pg, pb;
		int best_dist = 1;
		int best_idx = 0;
		has_exact_match[y][x] = 0;

#ifdef TIER2
		for(int i = 0; i < 256; i++)
#else
		for(int i = 0; i < 16; i++)
#endif
		{
			int pr, pg, pb;
			get_ocpal(i, &pr, &pg, &pb);
			int grp[3] = {pr, pg, pb};
			int dist = mse3(grp, raw_input[y][x]);

			if(i == 0 || dist < best_dist)
			{
				best_dist = dist;
				best_idx = i;
			}
		}

		ocpal_pidx[y][x] = best_idx;
		get_ocpal(best_idx, &pr, &pg, &pb);
		int ea[3];
		ea[0] = (raw_input[y][x][0] - pr);
		ea[1] = (raw_input[y][x][1] - pg);
		ea[2] = (raw_input[y][x][2] - pb);

		for(int j = 0; j < 3; j++)
		{
			input_error[y][x][j] = ea[j];
		}
	}
#endif
}

void get_ocpal_direct(int idx, int *r, int *g, int *b)
{
	if(idx < 16) {
		//*r = *g = *b = ((idx+1)*255)/17;
		//*r = *g = *b = 0;

		*r = base_pal[idx][0];
		*g = base_pal[idx][1];
		*b = base_pal[idx][2];

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

void get_ocpal(int idx, int *r, int *g, int *b)
{
	assert(idx >= 0x00);
	assert(idx <= 0xFF);

	*r = base_pal[idx][0];
	*g = base_pal[idx][1];
	*b = base_pal[idx][2];

	assert(*r >= 0x00); assert(*r <= 0xFF);
	assert(*g >= 0x00); assert(*g <= 0xFF);
	assert(*b >= 0x00); assert(*b <= 0xFF);
}

void pack_col_to_u8(const int *s, uint8_t *d)
{
	for(int j = 0; j < 3; j++)
	{
		if(s[j] < 0x00) {
			d[j] = 0x00;
		} else if(s[j] > 0xFF) {
			d[j] = 0xFF;
		} else {
			d[j] = s[j];
		}
	}
}

void unpack_col_from_u8(const uint8_t *s, int *d)
{
	for(int j = 0; j < 3; j++)
	{
		d[j] = s[j];
	}
}

int col_to_ocpal(uint8_t *u)
{
	int r = u[0];
	int g = u[1];
	int b = u[2];

	int l, cb, cr;
	to_ycbcr(r, g, b, &l, &cb, &cr);

	int best_r = 0;
	int best_g = 0;
	int best_b = 0;
	int best_pal = 0;
	int best_d2 = 0;

#ifdef TIER2
	for(int i = 0; i < 16; i++)
#else
	for(int i = 0; i < 256; i++)
#endif
	{
		int test_r, test_g, test_b;
		get_ocpal(i, &test_r, &test_g, &test_b);

		int test_l, test_cb, test_cr;
		to_ycbcr(test_r, test_g, test_b, &test_l, &test_cb, &test_cr);

		int dl = (test_l - l);
		int dcb = (test_cb - cb);
		int dcr = (test_cr - cr);

		int d2 = dl*dl + dcb*dcb + dcr*dcr;

		if(i == 0 || d2 < best_d2) {
			best_d2 = d2;
			best_r = test_r;
			best_g = test_g;
			best_b = test_b;
			best_pal = i;
		}
	}

	assert(best_r >= 0); assert(best_r <= 255);
	assert(best_g >= 0); assert(best_g <= 255);
	assert(best_b >= 0); assert(best_b <= 255);

	u[0] = best_r;
	u[1] = best_g;
	u[2] = best_b;

	return best_pal;
}

#ifdef POSITIONAL_DITHER
void apply_dither(uint8_t *c, int j, int x, int y)
{
	static const int csteps[3] = {6, 8, 5};

	x &= 7;
	y &= 7;

#if 0
	if(y&4) {
		// flip diag 8x8
		x = 7-x;
		y = 7-y;
	}

	if(x&4) {
		// rotate
		int t = x;
		x = 7-y;
		y = t;
	}
#endif

	int v = c[j];
	int d = (dithertab[(y+0)&3][(x+0)&3]-0x8);
	d = (d*128)/(csteps[j]*12);
	v += d;
	if(v < 0x00) v = 0x00;
	if(v > 0xFF) v = 0xFF;
	c[j] = v;
}
#endif

void process_block(int sx, int sy)
{
	uint8_t cols[8][4];
	uint8_t cols_dither[8][4];
	uint8_t outmap = 0;

	// Copy in
	for(int y = 0, i = 0; y < 4; y++)
	for(int x = 0; x < 2; x++, i++)
	{
		for(int j = 0; j < 4; j++)
		{
			cols[i][j] = raw_input[y+sy][x+sx][j];
			cols_dither[i][j] = cols[i][j];
#ifdef POSITIONAL_DITHER
			if(j != 3) {
				apply_dither(cols_dither[i], j, x + sx, y + sy);
			}
#endif
#ifdef ERROR_DITHER
			int v = cols_dither[i][j];
			v += input_error[sy+y][sx+x][j];
			if(v < 0x00) v = 0x00;
			if(v > 0xFF) v = 0xFF;
			int e = raw_input[sy+y][sx+x][j]-v;
			input_error[sy+y+0][sx+x+1][j] += (e+2)>>2;
			input_error[sy+y+1][sx+x-1][j] += (e+2)>>2;
			input_error[sy+y+1][sx+x+0][j] += (e+1)>>1;
			cols_dither[i][j] = v;
#endif
		}

		//col_to_ocpal(cols[j]);
	}

	// Get foreground + background
	int fg[4];
	int bg[4];
	fg[3] = bg[3] = 0xFF; // NOT DOING ALPHA

	fg[0] = fg[1] = fg[2] = 0x00;
	bg[0] = bg[1] = bg[2] = 0xFF;

	uint8_t fgp[4];
	uint8_t bgp[4];
	fgp[3] = bgp[3] = 0xFF;

#ifdef BRUTE_FORCE_BLOCK

	// Calculate best cluster
	int best_om = 0;
	int best_dist = 0;
	int best_fg_idx = 0;
	int best_bg_idx = 0;
	int msetab[256][8];
	int test_col[4];

	// Calculate MSEs
	for(int i = 0; i < 256; i++)
	{
		get_ocpal(i, &test_col[0], &test_col[1], &test_col[2]);

		for(int c = 0; c < 8; c++)
		{
			msetab[i][c] = mse3_to_yuv(test_col, cols_dither[c]);
			//msetab[i][c] = mse3(test_col, cols_dither[c]);
		}
	}

	for(int om = 0; om < 256; om++)
	{
		int dist = 0;

		// Find nearest possible fg/bg pairs
		int aux_fg[4];
		int aux_bg[4];
		int best_fg_dist = 0;
		int best_bg_dist = 0;
		int aux_fg_idx = 0;
		int aux_bg_idx = 0;

		// Test colours
		for(int i = 0; i < 256; i++)
		{
			int bg_dist = 0;
			int fg_dist = 0;

			for(int c = 0, b = 0x80; c < 8; c++, b >>= 1)
			{
				if((om & b) == 0) {
					bg_dist += msetab[i][c];
				} else {
					fg_dist += msetab[i][c];
				}
			}

			if(i == 0 || fg_dist < best_fg_dist) {
				best_fg_dist = fg_dist;
				aux_fg_idx = i;
			}

			if(i == 0 || bg_dist < best_bg_dist) {
				best_bg_dist = bg_dist;
				aux_bg_idx = i;
			}
		}

		// Now do nearest for this pattern
		dist = best_fg_dist + best_bg_dist;

		if(om == 0 || dist < best_dist) {
			best_om = om;
			best_dist = dist;
			best_fg_idx = aux_fg_idx;
			best_bg_idx = aux_bg_idx;
		}
	}

	get_ocpal(best_fg_idx, &fg[0], &fg[1], &fg[2]);
	get_ocpal(best_bg_idx, &bg[0], &bg[1], &bg[2]);
	outmap = best_om;
#else
	// Do min/max
	for(int i = 0, y = 0; y < 4; y++)
	for(int x = 0; x < 2; x++, i++)
	for(int j = 0; j < 3; j++)
	{
		if(((int)cols[i][j]) > fg[j]) {
			fg[j] = cols[i][j];
		}

		if(((int)cols[i][j]) < bg[j]) {
			bg[j] = cols[i][j];
		}
	}

	// Cluster
	for(int reps = 0; reps < 10; reps++)
	{
		int tot_fg[3];
		int count_fg;
		int tot_all[3];

		// Get nearest
		outmap = 0;
		count_fg = 0;
		memset(tot_fg, 0, sizeof(tot_fg));
		memset(tot_all, 0, sizeof(tot_all));
		for(int i = 0, y = 0, b = 0x80; y < 4; y++)
		for(int x = 0; x < 2; x++, i++, b >>= 1)
		{
			for(int j = 0; j < 3; j++)
			{
				tot_all[j] += (int)(cols_dither[i][j]);
			}

			if(mse3(fg, cols_dither[i]) >= mse3(bg, cols_dither[i])) {
				outmap |= b;
				count_fg++;
				for(int j = 0; j < 3; j++)
				{
					tot_fg[j] += (int)(cols_dither[i][j]);
				}
			}
		}

		if(count_fg == 0 || count_fg == 8) {
			break;
		}

		// Calculate means for fg, bg
		for(int j = 0; j < 3; j++)
		{
			fg[j] = (tot_fg[j] + count_fg/2)/count_fg;
			bg[j] = (tot_all[j] - tot_fg[j] + (8-count_fg)/2)/(8-count_fg);
		}

		// Apply ocpal
		pack_col_to_u8(fg, fgp);
		pack_col_to_u8(bg, bgp);
		//col_to_ocpal(fgp); col_to_ocpal(bgp);
		unpack_col_from_u8(fgp, fg);
		unpack_col_from_u8(bgp, bg);
	}
#endif

	// Convert fg and bg to nearest
	pack_col_to_u8(fg, fgp);
	pack_col_to_u8(bg, bgp);
	int pal_fg = col_to_ocpal(fgp);
	int pal_bg = col_to_ocpal(bgp);

	// Copy out
#ifdef TIER2
	assert(pal_fg >= 0x0 && pal_fg <= 0xF);
	assert(pal_bg >= 0x0 && pal_bg <= 0xF);
	ctif_output[sy/BH][sx/BW][0] = (pal_bg<<4)|pal_fg;
	ctif_output[sy/BH][sx/BW][1] = outmap;
#else
	assert(pal_fg >= 0x00 && pal_fg <= 0xFF);
	assert(pal_bg >= 0x00 && pal_bg <= 0xFF);
	ctif_output[sy/BH][sx/BW][0] = convert_to_ctif(pal_bg);
	ctif_output[sy/BH][sx/BW][1] = convert_to_ctif(pal_fg);
	ctif_output[sy/BH][sx/BW][2] = outmap;
#endif
	for(int y = 0, b = 0x80; y < 4; y++)
	for(int x = 0; x < 2; x++, b >>= 1)
	for(int j = 0; j < 4; j++)
	{
		raw_output[y+sy][x+sx][j] = (outmap & b ? fgp[j] : bgp[j]);
#ifdef ERROR_DITHER
		if(j != 3) {
			int u = raw_output[y+sy][x+sx][j];
			int v = raw_input[y+sy][x+sx][j];
			int e = (v-u);
			//e -= (input_error[y+sy][x+sx][j]+0)>>0;
			input_error[(y^0)+sy+0][(x^0)+sx+1][j] += ((e+2)>>2);
			input_error[(y^0)+sy+1][(x^0)+sx-1][j] += ((e+2)>>2);
			input_error[(y^0)+sy+1][(x^0)+sx-0][j] += ((e+1)>>1);
			//e += input_error[y+sy][x+sx][j];
			/*
			input_error[(y^0)+sy+0][(x^0)+sx+2][j] += ((e+1)>>1);
			input_error[(y^0)+sy+4][(x^0)+sx+0][j] += ((e+2)>>2);
			input_error[(y^0)+sy+4][(x^0)+sx-2][j] += ((e+2)>>2);
			*/
		}
#endif
	}

	//raw_output[sy][sx][0] += 0x80;
}

int main(int argc, char *argv[])
{
	FILE *fp;

	// Read
	fp = fopen(argv[1], "rb");
	fread(header, 18, 1, fp);
	fread(raw_input, 4, VW*VH, fp);
	fclose(fp);

	// Prep initial palette
	for(int i = 16; i < 256; i++)
	{
		get_ocpal_direct(i
			, &base_pal[i][0]
			, &base_pal[i][1]
			, &base_pal[i][2]
		);

		to_ycbcr(
			base_pal[i][0],
			base_pal[i][1],
			base_pal[i][2],
			&base_pal_yuv[i][0],
			&base_pal_yuv[i][1],
			&base_pal_yuv[i][2]);
	}

	// Convert image to YUV + BR-flip
	for(int y = 0; y < VH; y++)
	for(int x = 0; x < VW; x++)
	{
		int t = raw_input[y][x][2];
		raw_input[y][x][2] = raw_input[y][x][0];
		raw_input[y][x][0] = t;

		to_ycbcr(
			raw_input[y][x][0],
			raw_input[y][x][1],
			raw_input[y][x][2],
			&raw_input_yuv[y][x][0],
			&raw_input_yuv[y][x][1],
			&raw_input_yuv[y][x][2]);
	}

#ifdef ERROR_DITHER
	// Clear error table
	memset(input_error, 0, sizeof(input_error));
#endif

	// Compute palette
	compute_pal();

	// Process
#ifndef ERROR_DITHER
#pragma omp parallel for
#endif
	for(int sy = 0; sy < VH; sy += 4)
	{
		for(int sx = 0; sx < VW; sx += 2)
		{
			process_block(sx, sy);
		}
	}

	// Write
	fp = fopen(argv[2], "wb");
	fwrite("CTIF\x01\x00\x01\x00", 8, 1, fp);
	fputc(CW&255, fp);
	fputc(CW>>8,  fp);
	fputc(CH&255, fp);
	fputc(CH>>8,  fp);
	fputc(BW, fp);
	fputc(BH, fp);
#ifdef TIER2
	fputc(4, fp);
#else
	fputc(8, fp);
#endif
	fputc(3, fp);
	fputc(16, fp); fputc(0, fp);
	for(int i = 0; i < 16; i++)
	for(int j = 0; j < 3; j++)
	{
		int v = base_pal[i][2-j];
		assert(v >= 0x00 && v <= 0xFF);
		fputc(v, fp);
	}
	fwrite(ctif_output, sizeof(ctif_output), 1, fp);
	fclose(fp);

	// Write TGA test
	if(argc > 3) {
		fp = fopen(argv[3], "wb");
		fwrite(header, 18, 1, fp);

		// BR-flip
		for(int y = 0; y < VH; y++)
		for(int x = 0; x < VW; x++)
		{
			int t = raw_output[y][x][2];
			raw_output[y][x][2] = raw_output[y][x][0];
			raw_output[y][x][0] = t;
		}
		fwrite(raw_output, 4, VW*VH, fp);
		fclose(fp);
	}
	
	return 0;
}


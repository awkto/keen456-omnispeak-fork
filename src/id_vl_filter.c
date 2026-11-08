/* Screen filters, see id_vl_filter.h. Parameters match the old shell's FILTERS table; the
 * effects are laid out on the 320x200 game grid, evaluated at the 4x output resolution. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3") /* the pixel loops are written to be auto-vectorised */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "id_vl_filter.h"

#define GAME_W 320
/* #103: frames may be wider than 320 game pixels (widescreen); the game width of a w x h frame */
#define GAME_W_OF(w, h) ((w) * GAME_H / (h))
#define GAME_H 200

const char *vlf_filterStrings[] = {"off", "scanlines", "crt", "curved", "rgb", "soft", "amber", "green", 0};
const char *vlf_pixelsStrings[] = {"crisp", "light", "low", "medium", "high", 0};

typedef struct
{
	float scan, mask, vig, curve;
	float blur;		   /* gaussian-ish sigma in game pixels */
	float tint[3];	   /* monochrome tint, or all zero */
} FilterDef;

static const FilterDef defs[VLF_Filter_Count] = {
	/* off */ {0, 0, 0, 0, 0, {0, 0, 0}},
	/* scanlines */ {0.45f, 0, 0, 0, 0, {0, 0, 0}},
	/* crt */ {0.45f, 0.18f, 0.45f, 0, 0, {0, 0, 0}},
	/* curved */ {0.42f, 0.16f, 0.50f, 0.12f, 0, {0, 0, 0}},
	/* rgb */ {0, 0.22f, 0, 0, 0, {0, 0, 0}},
	/* soft */ {0.30f, 0, 0, 0, 0.30f, {0, 0, 0}},
	/* amber */ {0.42f, 0, 0.25f, 0, 0, {1.0f * 1.05f, 0.69f * 1.05f, 0.0f}},
	/* green */ {0.42f, 0, 0.25f, 0, 0, {0.30f * 1.04f, 1.0f * 1.04f, 0.32f * 1.04f}},
};
/* "Pixels: smooth" amounts: the old shell's SMOOTH_SCALE (light .35, low .6, medium 1, high 1.6)
 * applied to a base of ~0.4 game pixels, which is what a bilinear 320x200 upscale looks like. */
static const float pixelsSigma[VLF_Pixels_Count] = {0, 0.14f, 0.24f, 0.40f, 0.64f};

double vl_filterWorkS;
static VLF_Filter curFilter;
static VLF_Pixels curPixels;

void VLF_SetFilter(VLF_Filter f) { curFilter = (f >= 0 && f < VLF_Filter_Count) ? f : VLF_Filter_Off; }
void VLF_SetPixels(VLF_Pixels p) { curPixels = (p >= 0 && p < VLF_Pixels_Count) ? p : VLF_Pixels_Crisp; }
VLF_Filter VLF_GetFilter(void) { return curFilter; }
VLF_Pixels VLF_GetPixels(void) { return curPixels; }
bool VLF_Active(void) { return curFilter != VLF_Filter_Off || curPixels != VLF_Pixels_Crisp; }

static uint32_t *bufExp, *bufBlur, *bufTmp, *lutMul;
static int32_t *lutIdx;
static int bufW, bufH;
static int lutFilter = -1, lutW, lutH;

static void EnsureBuffers(int w, int h)
{
	if (bufExp && bufW == w && bufH == h)
		return;
	free(bufExp); free(bufBlur); free(bufTmp); free(lutMul); free(lutIdx);
	bufExp = (uint32_t *)malloc((size_t)w * h * 4);
	bufBlur = (uint32_t *)malloc((size_t)w * h * 4);
	bufTmp = (uint32_t *)malloc((size_t)w * h * 4);
	lutMul = (uint32_t *)malloc((size_t)w * h * 4);
	lutIdx = (int32_t *)malloc((size_t)w * h * 4);
	bufW = w; bufH = h;
	lutFilter = -1;
	if (!bufExp || !bufBlur || !bufTmp || !lutMul || !lutIdx)
	{
		fprintf(stderr, "filter: out of memory\n");
		exit(1);
	}
}

static uint8_t Q(float v)
{
	int i = (int)(v * 255.0f + 0.5f);
	return i < 0 ? 0 : i > 255 ? 255 : i;
}

/* Per output pixel: source (y << 16 | x) (or -1 outside the curved screen) and an RGB multiplier
 * holding scanlines * mask * vignette, all as the old fragment shader computed them. */
static void BuildLut(VLF_Filter f, int w, int h)
{
	const FilterDef *d = &defs[f];
	for (int y = 0; y < h; ++y)
	{
		for (int x = 0; x < w; ++x)
		{
			float px = ((x + 0.5f) / w) * 2.0f - 1.0f, py = ((y + 0.5f) / h) * 2.0f - 1.0f;
			int sx = x, sy = y;
			if (d->curve > 0)
			{
				float k = 1.0f + d->curve * (px * px + py * py);
				px *= k; py *= k;
				float u = px * 0.5f + 0.5f, v = py * 0.5f + 0.5f;
				sx = (int)floorf(u * w); sy = (int)floorf(v * h);
				if (u < 0 || u > 1 || v < 0 || v > 1 || sx < 0 || sx >= w || sy < 0 || sy >= h)
				{
					lutIdx[y * w + x] = -1;
					lutMul[y * w + x] = 0x00100401u; /* black-ish: factor 1/256 */
					continue;
				}
			}
			float u = px * 0.5f + 0.5f, v = py * 0.5f + 0.5f;
			float m[3] = {1, 1, 1};
			if (d->scan > 0)
			{
				float s = sinf(3.14159265f * v * GAME_H);
				float k = 1.0f - d->scan + d->scan * s * s;
				m[0] *= k; m[1] *= k; m[2] *= k;
			}
			if (d->mask > 0)
			{
				int ph = (int)floorf(u * GAME_W_OF(w, h)) % 3;
				if (ph < 0) ph += 3;
				for (int c = 0; c < 3; ++c)
					if (c != ph)
						m[c] *= 1.0f - d->mask;
			}
			if (d->vig > 0)
			{
				float k = 1.0f - d->vig * (px * px + py * py) * 0.5f;
				m[0] *= k; m[1] *= k; m[2] *= k;
			}
			lutIdx[y * w + x] = (sy << 16) | sx;
			lutMul[y * w + x] = (Q(m[0]) + 1u) | (Q(m[1]) + 1u) << 10 | (Q(m[2]) + 1u) << 20; /* factor/256, 1..256 */
		}
	}
	lutFilter = f; lutW = w; lutH = h;
}

/* Box blur of real width bw (in pixels, >= 1) with fractional edge taps, edges clamped.
 * Horizontal then vertical, running sums: cost independent of the width. */
static void BoxBlur(const uint32_t *src, uint32_t *dst, uint32_t *tmp, int w, int h, float bw)
{
	if (bw > 7.0f) bw = 7.0f;
	float R = (bw - 1.0f) * 0.5f;
	int ri = (int)R;
	int fw = (int)((R - ri) * 256.0f + 0.5f);
	int total = 256 * (2 * ri + 1) + 2 * fw;
	uint32_t rcp = (1u << 22) / total; /* S * rcp >> 22; fits 32 bits for bw <= 7 */
	int pad = ri + 2;
	int *row = (int *)malloc(sizeof(int) * 3 * (size_t)(w + 2 * pad));
	if (!row) return;
	/* horizontal: src -> tmp */
	for (int y = 0; y < h; ++y)
	{
		const uint32_t *s = src + (size_t)y * w;
		int *rr = row, *rg = row + (w + 2 * pad), *rb = row + 2 * (w + 2 * pad);
		for (int i = 0; i < w + 2 * pad; ++i)
		{
			int sx = i - pad;
			sx = sx < 0 ? 0 : sx >= w ? w - 1 : sx;
			uint32_t c = s[sx];
			rr[i] = (c >> 16) & 255; rg[i] = (c >> 8) & 255; rb[i] = c & 255;
		}
		int Br = 0, Bg = 0, Bb = 0;
		for (int k = -ri; k <= ri; ++k)
		{
			Br += rr[pad + k]; Bg += rg[pad + k]; Bb += rb[pad + k];
		}
		uint32_t *o = tmp + (size_t)y * w;
		for (int x = 0; x < w; ++x)
		{
			int i = pad + x;
			uint32_t Sr = 256u * Br + (uint32_t)fw * (rr[i - ri - 1] + rr[i + ri + 1]);
			uint32_t Sg = 256u * Bg + (uint32_t)fw * (rg[i - ri - 1] + rg[i + ri + 1]);
			uint32_t Sb = 256u * Bb + (uint32_t)fw * (rb[i - ri - 1] + rb[i + ri + 1]);
			o[x] = 0xFF000000u | (uint32_t)((Sr * rcp) >> 22) << 16 | (uint32_t)((Sg * rcp) >> 22) << 8 | (uint32_t)((Sb * rcp) >> 22);
			Br += rr[i + ri + 1] - rr[i - ri];
			Bg += rg[i + ri + 1] - rg[i - ri];
			Bb += rb[i + ri + 1] - rb[i - ri];
		}
	}
	free(row);
	/* vertical: tmp -> dst */
	int *B = (int *)malloc(sizeof(int) * 3 * (size_t)w);
	if (!B) return;
#define ROWP(yy) (tmp + (size_t)((yy) < 0 ? 0 : (yy) >= h ? h - 1 : (yy)) * w)
	memset(B, 0, sizeof(int) * 3 * (size_t)w);
	for (int k = -ri; k <= ri; ++k)
	{
		const uint32_t *r = ROWP(k);
		for (int x = 0; x < w; ++x)
		{
			uint32_t c = r[x];
			B[3 * x] += (c >> 16) & 255; B[3 * x + 1] += (c >> 8) & 255; B[3 * x + 2] += c & 255;
		}
	}
	for (int y = 0; y < h; ++y)
	{
		const uint32_t *rm = ROWP(y - ri - 1), *rp = ROWP(y + ri + 1), *rm2 = ROWP(y - ri);
		uint32_t *o = dst + (size_t)y * w;
		for (int x = 0; x < w; ++x)
		{
			uint32_t a = rm[x], b = rp[x];
			int *bx = B + 3 * x;
			uint32_t Sr = 256u * bx[0] + (uint32_t)fw * (((a >> 16) & 255) + ((b >> 16) & 255));
			uint32_t Sg = 256u * bx[1] + (uint32_t)fw * (((a >> 8) & 255) + ((b >> 8) & 255));
			uint32_t Sb = 256u * bx[2] + (uint32_t)fw * ((a & 255) + (b & 255));
			o[x] = 0xFF000000u | (uint32_t)((Sr * rcp) >> 22) << 16 | (uint32_t)((Sg * rcp) >> 22) << 8 | (uint32_t)((Sb * rcp) >> 22);
			uint32_t c = rm2[x];
			bx[0] += ((b >> 16) & 255) - ((c >> 16) & 255);
			bx[1] += ((b >> 8) & 255) - ((c >> 8) & 255);
			bx[2] += (b & 255) - (c & 255);
		}
	}
#undef ROWP
	free(B);
}

/* c: ARGB pixel, m: three 10-bit factors (1..256 = x/256). 256 keeps a channel exact. */
static inline uint32_t Shade(uint32_t c, uint32_t m)
{
	return 0xFF000000u | ((((c >> 16) & 255) * (m & 1023)) >> 8) << 16 | ((((c >> 8) & 255) * ((m >> 10) & 1023)) >> 8) << 8 | (((c & 255) * ((m >> 20) & 1023)) >> 8);
}
static inline uint32_t ShadeTint(uint32_t c, uint32_t m, uint32_t t0, uint32_t t1, uint32_t t2)
{
	uint32_t lum = (((c >> 16) & 255) * 77 + ((c >> 8) & 255) * 150 + (c & 255) * 29) >> 8;
	return 0xFF000000u | (((lum * t0 >> 8) * (m & 1023)) >> 8) << 16 | (((lum * t1 >> 8) * ((m >> 10) & 1023)) >> 8) << 8 | (((lum * t2 >> 8) * ((m >> 20) & 1023)) >> 8);
}

/* Test hook: OMNI_DEBUG_FILTER_SHOT=<file.ppm> [OMNI_DEBUG_FILTER_SHOT_FRAME=n]: write the n-th
 * filtered frame (default 1) as a PPM and exit. Never touches the CI frame dumps. */
static void DebugShot(const uint32_t *p, int w, int h)
{
	static int n;
	const char *path = getenv("OMNI_DEBUG_FILTER_SHOT");
	if (!path)
		return;
	const char *fr = getenv("OMNI_DEBUG_FILTER_SHOT_FRAME");
	if (++n < (fr ? atoi(fr) : 1))
		return;
	FILE *f = fopen(path, "wb");
	if (!f)
		return;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; ++i)
	{
		unsigned char px[3] = {(unsigned char)(p[i] >> 16), (unsigned char)(p[i] >> 8), (unsigned char)p[i]};
		fwrite(px, 1, 3, f);
	}
	fclose(f);
	exit(0);
}

static const uint32_t *ApplyInner(const uint32_t *src, int w, int h, int *outW, int *outH);

const uint32_t *VLF_Apply(const uint32_t *src, int w, int h, int *outW, int *outH)
{
	const uint32_t *r = ApplyInner(src, w, h, outW, outH);
	DebugShot(r, *outW, *outH); /* test hook; also unfiltered, for the widescreen shots (#103) */
	return r;
}

static const uint32_t *ApplyInner(const uint32_t *src, int w, int h, int *outW, int *outH)
{
	*outW = w; *outH = h;
	if (!VLF_Active())
		return src;
	int ow = w, oh = h;
	if (h == GAME_H && w >= GAME_W && w <= 4 * GAME_W) /* a 1x frame (320, or wider): expand 4x */
		ow = w * 4, oh = h * 4;
	EnsureBuffers(ow, oh);
	*outW = ow; *outH = oh;

	const uint32_t *cur = src;
	if (ow != w)
	{
		for (int y = 0; y < oh; ++y)
		{
			const uint32_t *s = src + (size_t)(y / 4) * w;
			uint32_t *d = bufExp + (size_t)y * ow;
			for (int x = 0; x < ow; ++x)
				d[x] = s[x >> 2];
		}
		cur = bufExp;
	}

	const FilterDef *d = &defs[curFilter];
	int cw = ow, shift = 0;
	float sigma = d->blur, ps = pixelsSigma[curPixels];
	sigma = sqrtf(sigma * sigma + ps * ps);
	if (sigma > 0)
	{
		/* Blur at half resolution: 2x2 average, then a box blur; the display's own linear
		 * scaling (or the multiply pass below) brings it back up. 4x the pixels would cost
		 * ~20 ms in WASM. sigma is in game pixels, 1 game pixel = ow/gameW/2 half-res pixels. */
		int hw = ow / 2, hh = oh / 2;
		const uint32_t M = 0x00FF00FFu;
		for (int y = 0; y < hh; ++y)
		{
			const uint32_t *r0 = cur + (size_t)(2 * y) * ow, *r1 = r0 + ow;
			uint32_t *o = bufTmp + (size_t)y * hw;
			for (int x = 0; x < hw; ++x)
			{
				uint32_t a = r0[2 * x], b = r0[2 * x + 1], c = r1[2 * x], e = r1[2 * x + 1];
				uint32_t rb = (a & M) + (b & M) + (c & M) + (e & M) + 0x00020002u;
				uint32_t ag = ((a >> 8) & M) + ((b >> 8) & M) + ((c >> 8) & M) + ((e >> 8) & M) + 0x00020002u;
				o[x] = ((rb >> 2) & M) | (((ag >> 2) & M) << 8);
			}
		}
		float bw = sigma * (ow / (float)GAME_W_OF(ow, oh) / 2.0f) * 3.4641016f; /* box width, same variance */
		if (bw > 1.05f)
		{
			BoxBlur(bufTmp, bufBlur, bufExp /* free once downsampled */, hw, hh, bw);
			cur = bufBlur;
		}
		else
			cur = bufTmp;
		cw = hw; shift = 1;
		*outW = hw; *outH = hh;
		if (curFilter == VLF_Filter_Off)
			return cur;
		*outW = ow; *outH = oh;
	}
	if (curFilter == VLF_Filter_Off)
		return cur;

	if (lutFilter != (int)curFilter || lutW != ow || lutH != oh)
		BuildLut(curFilter, ow, oh);

	uint32_t *dst = (cur == bufBlur) ? bufTmp : bufBlur;
	bool tint = d->tint[0] > 0 || d->tint[1] > 0 || d->tint[2] > 0;
	/* tint factors as x/256 (256 = 1.0) */
	uint32_t t0 = (uint32_t)(d->tint[0] * 256.0f + 0.5f), t1 = (uint32_t)(d->tint[1] * 256.0f + 0.5f), t2 = (uint32_t)(d->tint[2] * 256.0f + 0.5f);
	t0 = t0 > 256 ? 256 : t0; t1 = t1 > 256 ? 256 : t1; t2 = t2 > 256 ? 256 : t2;
	bool curved = d->curve > 0;
	if (shift && (curved || tint))
	{
		/* rare combination: bring the blurred frame back to full size first */
		uint32_t *up = (cur == bufBlur) ? bufTmp : bufBlur;
		for (int y = 0; y < oh; ++y)
			for (int x = 0; x < ow; ++x)
				up[(size_t)y * ow + x] = cur[(size_t)(y >> 1) * cw + (x >> 1)];
		cur = up; cw = ow; shift = 0;
		dst = (cur == bufBlur) ? bufTmp : bufBlur;
	}
	for (int y = 0; y < oh; ++y)
	{
		const uint32_t *crow = cur + (size_t)(y >> shift) * cw;
		const uint32_t *mrow = lutMul + (size_t)y * ow;
		uint32_t *drow = dst + (size_t)y * ow;
		if (curved)
		{
			const int32_t *irow = lutIdx + (size_t)y * ow;
			for (int x = 0; x < ow; ++x)
			{
				int32_t v = irow[x];
				uint32_t c = v < 0 ? 0 : cur[(size_t)(v >> 16) * cw + (v & 0xFFFF)];
				drow[x] = Shade(c, mrow[x]);
			}
		}
		else if (tint)
		{
			for (int x = 0; x < ow; ++x)
				drow[x] = ShadeTint(crow[x], mrow[x], t0, t1, t2);
		}
		else if (shift)
		{
			for (int x = 0; x < ow / 2; ++x)
			{
				uint32_t c = crow[x];
				drow[2 * x] = Shade(c, mrow[2 * x]);
				drow[2 * x + 1] = Shade(c, mrow[2 * x + 1]);
			}
		}
		else
		{
			for (int x = 0; x < ow; ++x)
				drow[x] = Shade(crow[x], mrow[x]);
		}
	}
	return dst;
}

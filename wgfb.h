/* wgfb.h - a framebuffer at the screen's own size, with anti-aliased shapes
 * and text, for games that draw at any size (wg_resize). Define WG_OWN_FB,
 * include wg.h and sdf_font.h, then this file.
 *
 *   fb_size(w, h, layers)   (re)allocates `layers` full-screen buffers after
 *                           __heap_base; fb is the first, fb_layer(i) the rest
 *   rrect / disc / capsule  filled shapes with soft edges, alpha 0..1
 *   text_c / text_fit       Inter (sdf_font.h), centred on a point
 * It also exports wg_fb, wg_w and wg_h, and calls the game's fb_resized()
 * when it first makes the buffers. Define WGFB_LAYERS for more than one. */
#ifndef WGFB_H
#define WGFB_H
#ifndef WGFB_LAYERS
#define WGFB_LAYERS 1
#endif

extern unsigned char __heap_base;
static u32 *fb;
static i32 FW, FH, FB_LAYERS;

static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float absf(float v) { return v < 0 ? -v : v; }
static i32 floori(float v) { return (i32)__builtin_floorf(v); }
static i32 ceili(float v) { return (i32)__builtin_ceilf(v); }
static float sqrtf_(float v) { return __builtin_sqrtf(v); }

/* sin, good to about 0.001: plenty for wobbles and pulses */
static float sinf_(float x) {
  const float PI = 3.14159265f;
  x -= (float)floori(x / (2 * PI) + 0.5f) * 2 * PI;
  float y = 4 / PI * x - 4 / (PI * PI) * x * absf(x);
  return 0.225f * (y * absf(y) - y) + y;
}

static i32 fb_size(i32 w, i32 h, i32 layers) {
  if (w < 64) w = 64;
  if (h < 64) h = 64;
  if (w > 4096) w = 4096;
  if (h > 4096) h = 4096;
  unsigned long need = (unsigned long)&__heap_base + (unsigned long)w * (unsigned long)h * 4ul * (unsigned long)layers;
  unsigned long have = __builtin_wasm_memory_size(0) * 65536ul;
  if (need > have && __builtin_wasm_memory_grow(0, (need - have + 65535) / 65536) == (unsigned long)-1) return 0;
  fb = (u32 *)&__heap_base;
  FW = w;
  FH = h;
  FB_LAYERS = layers;
  return 1;
}
static u32 *fb_layer(i32 i) { return (u32 *)&__heap_base + (unsigned long)FW * FH * i; }

/* blends colour c over the pixel with coverage a (0..256) */
static void blend(u32 *p, u32 c, u32 a) {
  if (a >= 256) { *p = c; return; }
  if (!a) return;
  u32 d = *p, ia = 256 - a;
  u32 rb = (((c & 0xFF00FFu) * a + (d & 0xFF00FFu) * ia) >> 8) & 0xFF00FFu;
  u32 g = (((c & 0x00FF00u) * a + (d & 0x00FF00u) * ia) >> 8) & 0x00FF00u;
  *p = 0xFF000000u | rb | g;
}

/* mixes two colours, a = 0..1 of c */
static u32 mixc(u32 c, u32 d, float a) {
  u32 k = (u32)(clampf(a, 0, 1) * 256), out = d;
  blend(&out, c, k);
  return out;
}

static void clear(u32 c) {
  u32 *p = fb, *end = fb + FW * FH;
  while (p < end) *p++ = c;
}

/* a crisp rectangle on whole pixels */
static void box(i32 x, i32 y, i32 w, i32 h, u32 c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > FW) w = FW - x;
  if (y + h > FH) h = FH - y;
  for (i32 j = 0; j < h; j++) {
    u32 *row = fb + (y + j) * FW + x;
    for (i32 i = 0; i < w; i++) row[i] = c;
  }
}

/* rounded rectangle */
static void rrect(float x, float y, float w, float h, float r, u32 c, float alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  r = minf(r, minf(w, h) / 2);
  float cx = x + w / 2, cy = y + h / 2, hw = w / 2 - r, hh = h / 2 - r;
  i32 x0 = floori(x), x1 = ceili(x + w), y0 = floori(y), y1 = ceili(y + h);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FW) x1 = FW;
  if (y1 > FH) y1 = FH;
  for (i32 j = y0; j < y1; j++) {
    float qy = absf(j + 0.5f - cy) - hh;
    u32 *row = fb + j * FW;
    for (i32 i = x0; i < x1; i++) {
      float qx = absf(i + 0.5f - cx) - hw, d;
      if (qx <= 0 && qy <= 0) d = maxf(qx, qy) - r;
      else if (qx <= 0) d = qy - r;
      else if (qy <= 0) d = qx - r;
      else d = sqrtf_(qx * qx + qy * qy) - r;
      float cov = clampf(0.5f - d, 0, 1);
      if (cov > 0) blend(row + i, c, (u32)(cov * alpha * 256));
    }
  }
}

/* a line from a to b with round ends, radius r; a disc when a == b */
static void capsule(float ax, float ay, float bx, float by, float r, u32 c, float alpha) {
  if (r <= 0 || alpha <= 0) return;
  i32 x0 = floori(minf(ax, bx) - r - 1), x1 = ceili(maxf(ax, bx) + r + 1);
  i32 y0 = floori(minf(ay, by) - r - 1), y1 = ceili(maxf(ay, by) + r + 1);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FW) x1 = FW;
  if (y1 > FH) y1 = FH;
  float dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy, inv = ll > 0 ? 1 / ll : 0;
  float in2 = r > 0.5f ? (r - 0.5f) * (r - 0.5f) : 0, out2 = (r + 0.5f) * (r + 0.5f);
  u32 full = (u32)(alpha * 256);
  for (i32 j = y0; j < y1; j++) {
    float py = j + 0.5f;
    u32 *row = fb + j * FW;
    for (i32 i = x0; i < x1; i++) {
      float px = i + 0.5f;
      float t = clampf(((px - ax) * dx + (py - ay) * dy) * inv, 0, 1);
      float qx = ax + t * dx - px, qy = ay + t * dy - py, d2 = qx * qx + qy * qy;
      if (d2 >= out2) continue;
      if (d2 <= in2) { blend(row + i, c, full); continue; }
      float cov = clampf(r + 0.5f - sqrtf_(d2), 0, 1);
      blend(row + i, c, (u32)(cov * alpha * 256));
    }
  }
}

/* a capsule whose radius and colour change along it, from ra and ca at a to
 * rb and cb at b: consecutive ones that share ends join without a seam */
static void capsule_grad(float ax, float ay, float bx, float by, float ra, float rb, u32 ca, u32 cb, float alpha) {
  float rm = maxf(ra, rb);
  if (rm <= 0 || alpha <= 0) return;
  i32 x0 = floori(minf(ax, bx) - rm - 1), x1 = ceili(maxf(ax, bx) + rm + 1);
  i32 y0 = floori(minf(ay, by) - rm - 1), y1 = ceili(maxf(ay, by) + rm + 1);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FW) x1 = FW;
  if (y1 > FH) y1 = FH;
  float dx = bx - ax, dy = by - ay, ll = dx * dx + dy * dy, inv = ll > 0 ? 1 / ll : 0;
  float out2 = (rm + 0.5f) * (rm + 0.5f);
  for (i32 j = y0; j < y1; j++) {
    float py = j + 0.5f;
    u32 *row = fb + j * FW;
    for (i32 i = x0; i < x1; i++) {
      float px = i + 0.5f;
      float t = clampf(((px - ax) * dx + (py - ay) * dy) * inv, 0, 1);
      float qx = ax + t * dx - px, qy = ay + t * dy - py, d2 = qx * qx + qy * qy;
      if (d2 >= out2) continue;
      float r = ra + (rb - ra) * t;
      float cov = clampf(r + 0.5f - sqrtf_(d2), 0, 1);
      if (cov > 0) blend(row + i, mixc(cb, ca, t), (u32)(cov * alpha * 256));
    }
  }
}

static void disc(float cx, float cy, float r, u32 c, float alpha) { capsule(cx, cy, cx, cy, r, c, alpha); }

/* ---------- text (Inter, from sdf_font.h) ---------- */

/* Without a game's extra glyphs (tools/sdf-font.py --extra), only Inter's. */
#ifndef SDF_EXTRA
#define SDF_EXTRA_N 0
static const unsigned int SDF_EXTRA_CHARS[1] = {0};
static const struct sdf_glyph SDF_EXTRA_GLYPHS[1] = {{0, 0, 0, 0, 0, 0}};
static const unsigned char SDF_EXTRA_DATA[1] = {0};
#endif

/* The glyph for character ch and its field; a space for ones the font lacks. */
static const struct sdf_glyph *glyph(u32 ch, const unsigned char **data) {
  *data = SDF_DATA;
  if (ch < 128) {
    for (i32 i = 0; SDF_CHARS[i]; i++)
      if ((u32)SDF_CHARS[i] == ch) return &SDF_GLYPHS[i];
    return &SDF_GLYPHS[0];
  }
  i32 lo = 0, hi = SDF_EXTRA_N - 1;
  while (lo <= hi) {
    i32 mid = (lo + hi) / 2;
    if (SDF_EXTRA_CHARS[mid] == ch) {
      *data = SDF_EXTRA_DATA;
      return &SDF_EXTRA_GLYPHS[mid];
    }
    if (SDF_EXTRA_CHARS[mid] < ch) lo = mid + 1;
    else hi = mid - 1;
  }
  return &SDF_GLYPHS[0];
}

static float text_width(const char *s, float size) {
  float k = size / SDF_EM, w = 0;
  const unsigned char *d;
  for (u32 ch; (ch = utf8_next(&s));) w += glyph(ch, &d)->adv * k;
  return w;
}

static void draw_glyph(const struct sdf_glyph *g, const unsigned char *data, float gx, float gy, float k, u32 c, float alpha) {
  const unsigned char *f = data + g->off;
  i32 w = g->w, h = g->h;
  if (!w) return;
  i32 x0 = floori(gx), y0 = floori(gy), x1 = ceili(gx + w * k), y1 = ceili(gy + h * k);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FW) x1 = FW;
  if (y1 > FH) y1 = FH;
  float inv = 1 / k, scale = SDF_SPREAD / 127.0f * k;
  for (i32 j = y0; j < y1; j++) {
    float fy = (j + 0.5f - gy) * inv - 0.5f;
    i32 iy = floori(fy);
    float ty = fy - iy;
    if (iy < -1 || iy >= h) continue;
    for (i32 i = x0; i < x1; i++) {
      float fx = (i + 0.5f - gx) * inv - 0.5f;
      i32 ix = floori(fx);
      float tx = fx - ix;
      if (ix < -1 || ix >= w) continue;
      float s00 = 0, s10 = 0, s01 = 0, s11 = 0;
      if (iy >= 0) {
        if (ix >= 0) s00 = f[iy * w + ix];
        if (ix + 1 < w) s10 = f[iy * w + ix + 1];
      }
      if (iy + 1 < h) {
        if (ix >= 0) s01 = f[(iy + 1) * w + ix];
        if (ix + 1 < w) s11 = f[(iy + 1) * w + ix + 1];
      }
      float v = (s00 * (1 - tx) + s10 * tx) * (1 - ty) + (s01 * (1 - tx) + s11 * tx) * ty;
      float cov = clampf((v - 128) * scale + 0.5f, 0, 1);
      if (cov > 0) blend(fb + j * FW + i, c, (u32)(cov * alpha * 256));
    }
  }
}

/* centre of the digits' height below the line top, in em units */
#define DIGIT_MID 24.6f

/* draws s with its left edge at x and the middle of its digits at cy; size is the em in pixels */
static float text_l(const char *s, float x, float cy, float size, u32 c, float alpha) {
  float k = size / SDF_EM, top = cy - DIGIT_MID * k;
  const unsigned char *d;
  for (u32 ch; (ch = utf8_next(&s));) {
    const struct sdf_glyph *g = glyph(ch, &d);
    draw_glyph(g, d, x + g->left * k, top + g->top * k, k, c, alpha);
    x += g->adv * k;
  }
  return x;
}

static void text_c(const char *s, float cx, float cy, float size, u32 c, float alpha) {
  text_l(s, cx - text_width(s, size) / 2, cy, size, c, alpha);
}

/* the page may ask for the size before wg_init */
static void fb_resized(void);
static void ensure_fb(void) {
  if (!fb) {
    fb_size(512, 448, WGFB_LAYERS);
    fb_resized();
  }
}
WG_EXPORT(wg_fb) u32 *wg_fb(void) { ensure_fb(); return fb; }
WG_EXPORT(wg_w) i32 wg_w(void) { ensure_fb(); return FW; }
WG_EXPORT(wg_h) i32 wg_h(void) { ensure_fb(); return FH; }

#endif

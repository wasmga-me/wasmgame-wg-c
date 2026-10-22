/* wgview.h - smooth drawing in a game's own units, for games laid out on a
 * fixed plan (say 400x300) that draw at the screen's size (wg_resize).
 * Include after wgfb.h. view_fit() scales the plan to fit the screen and
 * centres it; every v_* call takes plan units and draws anti-aliased at
 * full resolution. Also: pointer helpers, key repeat and a redraw gate, so
 * a still picture isn't repainted every frame. */
#ifndef WGVIEW_H
#define WGVIEW_H

typedef unsigned short u16;

/* pointer buttons passed to wg_mouse */
#define M_LEFT 1
#define M_RIGHT 2
#define M_MIDDLE 4
#define M_TOUCH 8

static float VK = 1, VX, VY, LW = 1, LH = 1;

static void view_fit(float lw, float lh) {
  LW = lw;
  LH = lh;
  VK = minf(FW / lw, FH / lh);
  VX = (FW - lw * VK) / 2;
  VY = (FH - lh * VK) / 2;
}
/* a screen pixel in plan units, for pointer input */
static i32 plan_x(i32 px) { return floori((px + 0.5f - VX) / VK); }
static i32 plan_y(i32 py) { return floori((py + 0.5f - VY) / VK); }
/* one screen pixel in plan units: hairlines */
static float hair(void) { return 1 / VK; }

static void v_box(float x, float y, float w, float h, float r, u32 c, float a) {
  rrect(VX + x * VK, VY + y * VK, w * VK, h * VK, r * VK, c, a);
}
static void v_line(float ax, float ay, float bx, float by, float r, u32 c, float a) {
  capsule(VX + ax * VK, VY + ay * VK, VX + bx * VK, VY + by * VK, r * VK, c, a);
}

/* the screen-pixel bounds of a plan rectangle, clipped */
static void v_bounds(float x0, float y0, float x1, float y1, i32 *b) {
  b[0] = floori(VX + x0 * VK) - 1;
  b[1] = floori(VY + y0 * VK) - 1;
  b[2] = ceili(VX + x1 * VK) + 1;
  b[3] = ceili(VY + y1 * VK) + 1;
  if (b[0] < 0) b[0] = 0;
  if (b[1] < 0) b[1] = 0;
  if (b[2] > FW) b[2] = FW;
  if (b[3] > FH) b[3] = FH;
}

/* a rounded box whose colour runs from top to bottom */
static void v_box_grad(float x, float y, float w, float h, float r, u32 top, u32 bottom, float a) {
  x = VX + x * VK; y = VY + y * VK; w *= VK; h *= VK; r *= VK;
  if (w <= 0 || h <= 0 || a <= 0) return;
  r = minf(r, minf(w, h) / 2);
  float cx = x + w / 2, cy = y + h / 2, hw = w / 2 - r, hh = h / 2 - r;
  i32 x0 = floori(x), x1 = ceili(x + w), y0 = floori(y), y1 = ceili(y + h);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > FW) x1 = FW;
  if (y1 > FH) y1 = FH;
  for (i32 j = y0; j < y1; j++)
    rrect_row(fb + j * FW, x0, x1, cx, absf(j + 0.5f - cy) - hh, hw, r, mixc(bottom, top, (j + 0.5f - y) / h), a);
}

/* a soft shadow: a rounded box blurred over `soft` plan units; a hollow
 * one leaves out the middle, for shadows under something opaque */
static void v_shadow_(float x, float y, float w, float h, float r, float soft, u32 c, float a, float hollow) {
  i32 b[4];
  v_bounds(x - soft, y - soft, x + w + soft, y + h + soft, b);
  float s = soft * VK + 1, cx = VX + (x + w / 2) * VK, cy = VY + (y + h / 2) * VK;
  float rr = minf(r, minf(w, h) / 2) * VK, hw = w / 2 * VK - rr, hh = h / 2 * VK - rr, skip = -hollow * VK - rr;
  for (i32 j = b[1]; j < b[3]; j++) {
    float qy = absf(j + 0.5f - cy) - hh;
    u32 *row = fb + j * FW;
    for (i32 i = b[0]; i < b[2]; i++) {
      float qx = absf(i + 0.5f - cx) - hw, d;
      if (hollow > 0 && qx < skip && qy < skip) continue;
      if (qx <= 0 && qy <= 0) d = maxf(qx, qy) - rr;
      else if (qx <= 0) d = qy - rr;
      else if (qy <= 0) d = qx - rr;
      else d = sqrtf_(qx * qx + qy * qy) - rr;
      float t = clampf(0.5f - d / s, 0, 1);
      if (t > 0) blend(row + i, c, (u32)(t * t * (3 - 2 * t) * a * 256));
    }
  }
}

/* half the width of an ellipse at height dy from its middle, 0 outside */
static float half_span(float rx, float ry, float dy) {
  if (rx <= 0 || ry <= 0) return -1;
  float t = 1 - dy * dy / (ry * ry);
  return t > 0 ? rx * sqrtf_(t) : -1;
}

static void v_shadow(float x, float y, float w, float h, float r, float soft, u32 c, float a) {
  v_shadow_(x, y, w, h, r, soft, c, a, 0);
}

/* an ellipse; rx may shrink to nothing, as a disc turning over. Each row
 * fills its middle straight and works out coverage only near the edge. */
static void v_ellipse(float cx, float cy, float rx, float ry, u32 c, float a) {
  rx *= VK; ry *= VK;
  if (ry <= 0 || a <= 0) return;
  if (rx < 0.35f) rx = 0.35f;
  float px0 = VX + cx * VK, py0 = VY + cy * VK, irx = 1 / rx, iry = 1 / ry;
  u32 full = (u32)(a * 256);
  i32 y0 = floori(py0 - ry - 1), y1 = ceili(py0 + ry + 1);
  if (y0 < 0) y0 = 0;
  if (y1 > FH) y1 = FH;
  for (i32 j = y0; j < y1; j++) {
    float dy = j + 0.5f - py0, xo = half_span(rx + 1, ry + 1, dy), xi = half_span(rx - 1.2f, ry - 1.2f, dy);
    if (xo < 0) continue;
    i32 x0 = floori(px0 - xo), x1 = ceili(px0 + xo);
    if (x0 < 0) x0 = 0;
    if (x1 > FW) x1 = FW;
    float y = dy * iry;
    u32 *row = fb + j * FW;
    for (i32 i = x0; i < x1; i++) {
      float dx = i + 0.5f - px0;
      if (absf(dx) < xi) { blend(row + i, c, full); continue; }
      float x = dx * irx, f = x * x + y * y - 1;
      /* distance to the edge, from the field's value over its slope */
      float gx = x * irx, gy = y * iry, g = 2 * sqrtf_(gx * gx + gy * gy);
      float cov = clampf(0.5f - (g > 0 ? f / g : -1), 0, 1);
      if (cov > 0) blend(row + i, c, (u32)(cov * a * 256));
    }
  }
}

/* a circle's outline t units thick, inside radius r */
static void v_ring(float cx, float cy, float r, float t, u32 c, float a) {
  float px0 = VX + cx * VK, py0 = VY + cy * VK, ro = r * VK, ri = (r - t) * VK;
  i32 y0 = floori(py0 - ro - 1), y1 = ceili(py0 + ro + 1);
  if (y0 < 0) y0 = 0;
  if (y1 > FH) y1 = FH;
  for (i32 j = y0; j < y1; j++) {
    float dy = j + 0.5f - py0, xo = half_span(ro + 1, ro + 1, dy), xi = half_span(ri - 1, ri - 1, dy);
    if (xo < 0) continue;
    i32 x0 = floori(px0 - xo), x1 = ceili(px0 + xo);
    if (x0 < 0) x0 = 0;
    if (x1 > FW) x1 = FW;
    u32 *row = fb + j * FW;
    for (i32 i = x0; i < x1; i++) {
      float dx = i + 0.5f - px0;
      if (absf(dx) < xi) continue;
      float d = sqrtf_(dx * dx + dy * dy);
      float cov = clampf(ro + 0.5f - d, 0, 1) - clampf(ri + 0.5f - d, 0, 1);
      if (cov > 0) blend(row + i, c, (u32)(cov * a * 256));
    }
  }
}

static void v_disc(float cx, float cy, float r, u32 c, float a) { v_ellipse(cx, cy, r, r, c, a); }

/* a filled convex polygon, points (x, y) in plan units, either winding */
static void v_poly(const float *p, i32 n, u32 c, float a) {
  float nx[8], ny[8], off[8], minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f, area = 0;
  if (n < 3 || n > 8) return;
  for (i32 k = 0; k < n; k++) {
    i32 m = (k + 1) % n;
    area += p[2 * k] * p[2 * m + 1] - p[2 * m] * p[2 * k + 1];
  }
  float sgn = area > 0 ? 1 : -1;
  for (i32 k = 0; k < n; k++) {
    i32 m = (k + 1) % n;
    float ax = VX + p[2 * k] * VK, ay = VY + p[2 * k + 1] * VK, bx = VX + p[2 * m] * VK, by = VY + p[2 * m + 1] * VK;
    float ex = bx - ax, ey = by - ay, l = sqrtf_(ex * ex + ey * ey);
    if (l <= 0) l = 1;
    /* outward normal */
    nx[k] = ey / l * sgn;
    ny[k] = -ex / l * sgn;
    off[k] = nx[k] * ax + ny[k] * ay;
    minx = minf(minx, p[2 * k]); maxx = maxf(maxx, p[2 * k]);
    miny = minf(miny, p[2 * k + 1]); maxy = maxf(maxy, p[2 * k + 1]);
  }
  i32 b[4];
  v_bounds(minx, miny, maxx, maxy, b);
  for (i32 j = b[1]; j < b[3]; j++) {
    float py = j + 0.5f;
    u32 *row = fb + j * FW;
    for (i32 i = b[0]; i < b[2]; i++) {
      float px = i + 0.5f, d = -1e9f;
      for (i32 k = 0; k < n; k++) d = maxf(d, nx[k] * px + ny[k] * py - off[k]);
      float cov = clampf(0.5f - d, 0, 1);
      if (cov > 0) blend(row + i, c, (u32)(cov * a * 256));
    }
  }
}

static void v_tri(float ax, float ay, float bx, float by, float cx, float cy, u32 c, float a) {
  float p[6] = {ax, ay, bx, by, cx, cy};
  v_poly(p, 3, c, a);
}

/* text in plan units: size is the em; y is the middle of the digits */
static float v_text_w(const char *s, float size) { return text_width(s, size * VK) / VK; }
static void v_text(const char *s, float x, float cy, float size, u32 c, float a) {
  text_l(s, VX + x * VK, VY + cy * VK, size * VK, c, a);
}
static void v_text_c(const char *s, float cx, float cy, float size, u32 c, float a) {
  text_c(s, VX + cx * VK, VY + cy * VK, size * VK, c, a);
}
static void v_text_r(const char *s, float rx, float cy, float size, u32 c, float a) {
  v_text(s, rx - v_text_w(s, size), cy, size, c, a);
}

/* ---------- small helpers ---------- */

static i32 inside(i32 px, i32 py, i32 x, i32 y, i32 w, i32 h) {
  return px >= x && py >= y && px < x + w && py < y + h;
}

static char *join(char *buf, const char *a, const char *b) {
  i32 i = 0;
  while (*a) buf[i++] = *a++;
  while (*b) buf[i++] = *b++;
  buf[i] = 0;
  return buf;
}

/* tenths of a second as "12.3" */
static char *tenths(i32 t, char *buf) {
  char tmp[12];
  itoa10(t / 10, tmp);
  i32 i = 0;
  for (; tmp[i]; i++) buf[i] = tmp[i];
  buf[i++] = '.';
  buf[i++] = (char)('0' + t % 10);
  buf[i] = 0;
  return buf;
}

/* seconds as "1:05" */
static char *clock_text(i32 secs, char *buf) {
  char tmp[12];
  itoa10(secs / 60, tmp);
  i32 i = 0;
  for (; tmp[i]; i++) buf[i] = tmp[i];
  buf[i++] = ':';
  buf[i++] = (char)('0' + secs % 60 / 10);
  buf[i++] = (char)('0' + secs % 10);
  buf[i] = 0;
  return buf;
}

/* held arrows repeat after a delay; returns the arrow bits to act on now */
static u32 key_repeat(u32 held, u32 pressed, i32 dt, i32 *timer) {
  u32 dirs = held & (B_UP | B_DOWN | B_LEFT | B_RIGHT);
  if (pressed & dirs) { *timer = -230; return pressed & dirs; }
  if (!dirs) { *timer = 0; return 0; }
  *timer += dt;
  if (*timer >= 70) { *timer -= 70; return dirs; }
  return 0;
}

static u32 hash_bytes(u32 h, const void *p, u32 n) {
  const u8 *b = (const u8 *)p;
  if (!h) h = 2166136261u;
  while (n--) { h ^= *b++; h *= 16777619u; }
  return h;
}
static u32 hash_int(u32 h, i32 v) { return hash_bytes(h, &v, sizeof v); }

/* ---------- a still background ----------
 * What doesn't change from frame to frame (a board, a table) is drawn once
 * into layer 1 (define WGFB_LAYERS 2) and copied under each frame. */
static u32 bg_key;

static void view_background(u32 key, void (*draw)(void)) {
  key = hash_int(hash_int(key, FW), FH);
  if (key != bg_key || !key) {
    u32 *keep = fb;
    fb = fb_layer(1);
    draw();
    fb = keep;
    bg_key = key;
  }
  memcpy(fb, fb_layer(1), (unsigned long)FW * FH * 4);
}

/* ---------- sprites ----------
 * Something drawn many times the same (a card back) can be drawn once into
 * a sprite after the screen layers, then copied. Its pixels must be opaque. */
typedef struct { u32 *px; i32 w, h; u32 key; } Sprite;

/* room for n pixels after the layers, or 0 */
static u32 *scratch(unsigned long n) {
  u32 *p = fb_layer(FB_LAYERS);
  unsigned long need = (unsigned long)(p + n), have = __builtin_wasm_memory_size(0) * 65536ul;
  if (need > have && __builtin_wasm_memory_grow(0, (need - have + 65535) / 65536) == (unsigned long)-1) return 0;
  return p;
}

/* draws the plan box x, y, w, h into the sprite with draw(x, y) when the
 * key or the scale changed; returns whether the sprite is usable */
static i32 sprite_make(Sprite *s, u32 key, float x, float y, float w, float h, void (*draw)(float, float)) {
  key = hash_int(hash_int(hash_int(key, FW), FH), (i32)(VK * 1000));
  i32 sw = ceili(w * VK), sh = ceili(h * VK);
  u32 *px = scratch((unsigned long)sw * sh);
  if (!px) return 0;
  if (s->key == key && s->px == px) return 1;
  u32 *kf = fb;
  i32 kw = FW, kh = FH;
  float kx = VX, ky = VY;
  fb = px; FW = sw; FH = sh;
  VX = -x * VK; VY = -y * VK;
  draw(x, y);
  fb = kf; FW = kw; FH = kh; VX = kx; VY = ky;
  s->px = px; s->w = sw; s->h = sh; s->key = key;
  return 1;
}

/* copies the sprite with its top left at plan x, y, to the nearest pixel */
static void sprite_put(const Sprite *s, float x, float y) {
  i32 dx = floori(VX + x * VK + 0.5f), dy = floori(VY + y * VK + 0.5f);
  /* the sprite's columns i0..i1 fall on the screen */
  i32 i0 = dx < 0 ? -dx : 0, i1 = s->w < FW - dx ? s->w : FW - dx;
  if (i1 <= i0) return;
  for (i32 j = 0; j < s->h; j++) {
    i32 yy = dy + j;
    if (yy >= 0 && yy < FH) memcpy(fb + yy * FW + dx + i0, s->px + j * s->w + i0, (unsigned long)(i1 - i0) * 4);
  }
}

/* ---------- redraw gate ----------
 * A game hashes what its picture depends on each frame; the frame is drawn
 * (and wg_drawn says so) only when that changed, something moves, or the
 * screen was resized. */
static u32 view_sig, view_force = 1;
static i32 view_drawn;


/* after a resize or a load: draw everything again, background too */
static void view_redraw(void) { view_force = 1; bg_key = 0; }

/* draws when needed; returns whether it did */
static i32 view_present(u32 sig, i32 moving, void (*draw)(void)) {
  view_drawn = moving || view_force || sig != view_sig;
  if (view_drawn) {
    draw();
    view_sig = sig;
    view_force = 0;
  }
  return view_drawn;
}

WG_EXPORT(wg_drawn) i32 wg_drawn(void) { return view_drawn; }

#endif

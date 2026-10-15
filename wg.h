/* wg.h - tiny freestanding runtime shared by wasmga.me original games.
 *
 * Every native game exports the same ABI so the player can drive it:
 *   wg_init(seed)            reset to title screen
 *   wg_frame(buttons, dt_ms) advance one frame, returns WG_EV_* bits
 *   wg_fb() / wg_w() / wg_h() RGBA framebuffer (0xAABBGGRR words)
 *   wg_state() / wg_state_size() / wg_state_loaded()
 *                            the whole game state lives in one struct, so a
 *                            save is a byte copy of it and a load is the reverse
 *   wg_score()               current score (reported to the scoreboard)
 * and may export (see src/scripts/engines/native.ts):
 *   wg_resize(w, h)          draw at this size from now on (device pixels)
 *   wg_mouse(x, y, buttons)  pointer in game pixels (-1, -1 when it leaves);
 *                            buttons 1 left, 2 right, 4 middle, 8 touch
 *   wg_drawn()               1 if the last wg_frame changed the picture
 *   wg_pointer(x)            paddle games: pointer x, -1 when it leaves
 * and may import:
 *   env.wg_panic(msg, len)   a UTF-8 message the page throws as the crash, so
 *                            the error screen shows it with the stack below
 *
 * Games with a setup in the catalog (`setup`: speed, map size and so on) let
 * the page draw the setup, pause and game over screens (src/scripts/menu.ts),
 * so they have no title or pause of their own and export two more:
 *   wg_option(i, v)          v >= 0 picks choice v of option i (catalog order)
 *                            for the next wg_init; returns option i's choice
 *   wg_over()                1 once the run has ended, from the moment it is
 *                            lost or won (no autosave is kept from then on)
 *   wg_stat(i)               optional: stat i of the run, in the order of the
 *                            catalog's `stats` (labels and units live there),
 *                            listed on the game over screen; -1 hides it
 * For them wg_init(seed) starts a run with the options picked, Start never
 * reaches wg_frame (it opens the pause screen), and the options live in the
 * state, so a save brings its own setup and scoreboard back. Keeping them as
 * an `i32 opt[N]` there, with a `choices[N]` table of how many each has, lets
 * wg_pick and wg_picks_ok below do the checks.
 */
#ifndef WG_H
#define WG_H

typedef unsigned int u32;
typedef unsigned char u8;
typedef int i32;

#define WG_EXPORT(name) __attribute__((export_name(#name)))


/* buttons */
#define B_UP 1
#define B_DOWN 2
#define B_LEFT 4
#define B_RIGHT 8
#define B_A 16
#define B_B 32
#define B_START 64

/* events returned from wg_frame, the player turns them into sound and UI */
#define WG_EV_BLIP 1
#define WG_EV_HIT 2
#define WG_EV_LOSE 4
#define WG_EV_GAMEOVER 8
#define WG_EV_LEVEL 16
#define WG_EV_WIN 32 /* a win tune */

void *memset(void *d, int c, unsigned long n) {
  u8 *p = (u8 *)d;
  while (n--) *p++ = (u8)c;
  return d;
}
void *memcpy(void *d, const void *s, unsigned long n) {
  u8 *p = (u8 *)d;
  const u8 *q = (const u8 *)s;
  while (n--) *p++ = *q++;
  return d;
}

#define RGB(r, g, b) (0xFF000000u | ((u32)(b) << 16) | ((u32)(g) << 8) | (u32)(r))

/* Games that size their own framebuffer (wg_resize) define WG_OWN_FB and
 * skip the fixed one with its pixel font. */
#ifndef WG_OWN_FB
/* a game may define its own W and H before including this file */
#ifndef W
#define W 256
#endif
#ifndef H
#define H 224
#endif
static u32 fb[W * H];
WG_EXPORT(wg_fb) u32 *wg_fb(void) { return fb; }
WG_EXPORT(wg_w) int wg_w(void) { return W; }
WG_EXPORT(wg_h) int wg_h(void) { return H; }

static void fill(int x, int y, int w, int h, u32 c) {
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  for (int j = 0; j < h; j++) {
    u32 *row = fb + (y + j) * W + x;
    for (int i = 0; i < w; i++) row[i] = c;
  }
}

static void clear(u32 c) { fill(0, 0, W, H, c); }

static const char FONT_CHARS[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-!?/+',";
static const u8 FONT[][7] = {
  {0, 0, 0, 0, 0, 0, 0},
  {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E},
  {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E},
  {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
  {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
  {0x0E, 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11}, {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
  {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}, {0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C},
  {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
  {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}, {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
  {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}, {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C},
  {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
  {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, {0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11},
  {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
  {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
  {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
  {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
  {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
  {0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04}, {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}, {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00},
  {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}, {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
  {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}, {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
  {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}, {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00},
  {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08},
};

static int glyph_index(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  for (int i = 0; FONT_CHARS[i]; i++)
    if (FONT_CHARS[i] == ch) return i;
  return 0;
}

/* draws text with integer scale s; returns width in pixels */
static int text(int x, int y, const char *str, int s, u32 c) {
  int x0 = x;
  for (; *str; str++) {
    const u8 *g = FONT[glyph_index(*str)];
    for (int r = 0; r < 7; r++)
      for (int b = 0; b < 5; b++)
        if (g[r] & (0x10 >> b)) fill(x + b * s, y + r * s, s, s, c);
    x += 6 * s;
  }
  return x - x0 - s;
}

static int text_width(const char *str, int s) {
  int n = 0;
  while (str[n]) n++;
  return n ? n * 6 * s - s : 0;
}

static void text_center(int y, const char *str, int s, u32 c) {
  text((W - text_width(str, s)) / 2, y, str, s, c);
}

#endif

/* writes decimal n into buf (at least 12 bytes) */
static char *itoa10(int n, char *buf) {
  char tmp[12];
  int i = 0, neg = n < 0;
  u32 v = neg ? (u32)(-n) : (u32)n;
  do { tmp[i++] = (char)('0' + v % 10); v /= 10; } while (v);
  int j = 0;
  if (neg) buf[j++] = '-';
  while (i) buf[j++] = tmp[--i];
  buf[j] = 0;
  return buf;
}

static u32 xorshift(u32 *s) {
  u32 x = *s ? *s : 0x9E3779B9u;
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return *s = x;
}

/* For games with a setup: wg_option's body, and its check for wg_state_loaded. */
static i32 wg_pick(i32 *opt, const i32 *choices, i32 n, i32 i, i32 v) {
  if (i < 0 || i >= n) return -1;
  if (v >= 0 && v < choices[i]) opt[i] = v;
  return opt[i];
}
static i32 wg_picks_ok(const i32 *opt, const i32 *choices, i32 n) {
  for (i32 i = 0; i < n; i++)
    if (opt[i] < 0 || opt[i] >= choices[i]) return 0;
  return 1;
}

#endif

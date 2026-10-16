# wasmgame-wg-c

The C engine of the wasmga.me native games: freestanding headers (no libc, no JS glue) that a game includes and builds with clang to one `.wasm` file.

| File | What it is |
| --- | --- |
| `wg.h` | The ABI every native game exports (`wg_init`, `wg_frame`, `wg_fb`, `wg_state`, `wg_score`, optional `wg_resize`, `wg_mouse`, `wg_option`, `wg_over`, `wg_stat`), with a 256x224 framebuffer and pixel font for simple games |
| `wgfb.h` | Framebuffer at the screen's own size, with layers and anti-aliased shapes |
| `wgview.h` | A fixed plan scaled to the screen (`view_fit`), `v_*` shapes and text, a cached background layer and a redraw gate |
| `sdf_font.h` | Signed distance fields of Inter Display ExtraBold, made by `tools/sdf-font.py` |

## Build a game

```sh
clang --target=wasm32 -O2 -ffreestanding -fno-builtin -nostdlib \
  -Wl,--no-entry -Wl,--strip-debug -I path/to/wasmgame-wg-c \
  -o game.wasm game.c
```

## Change the font

Add glyphs to `CHARS` in `tools/sdf-font.py`, then run `python3 tools/sdf-font.py [InterDisplay-ExtraBold.otf]` (needs Pillow and NumPy). It rewrites `sdf_font.h`; copy the new file into the Rust engine, [wasmgame-wg](https://github.com/wasmga-me/wasmgame-wg), too.

## Games on it

[wasmga-me/games](https://github.com/wasmga-me/games) pins a commit of this repo in `wg-c.rev`; its `build.sh` fetches it into `wg-c/` when it compiles. After pushing here, bump that file there.

# vendor

Single-header libraries, unmodified, fetched from upstream master on 2026-09-25.

| File | Upstream | License |
|---|---|---|
| sokol_app.h, sokol_gfx.h, sokol_glue.h, sokol_log.h | github.com/floooh/sokol | zlib |
| sokol_nuklear.h | github.com/floooh/sokol (util/) | zlib |
| nuklear.h | github.com/Immediate-Mode-UI/Nuklear | MIT / public domain |
| nuklear_style.c | github.com/Immediate-Mode-UI/Nuklear (demo/common/style.c, fetched 2026-09-29) | MIT / public domain |
| stb_image_write.h | github.com/nothings/stb | MIT / public domain |
| stb_sprintf.h (v1.10, fetched 2026-10-02) | github.com/nothings/stb | MIT / public domain |
| tinyfiledialogs.c/.h (3.21.4) | sourceforge.net/projects/tinyfiledialogs | zlib (compiled on Windows/macOS only) |
| minih264e.h | github.com/lieff/minih264 | CC0 / public domain |
| minimp4.h | github.com/lieff/minimp4 | CC0 / public domain |

## fonts

The UI font is compiled in: `src/font_data.c` holds subsets of the fonts below
as byte arrays and `src/icons.h` their glyph ranges, both written by
`scripts/embed-fonts.py` (fonttools) from the full TTFs, fetched 2026-10-02. The
subsets keep each font's copyright and licence entries in its name table; the
licence texts are in `fonts/` and ship in `binaries/licenses/`.

| Font | Upstream | What is embedded | License |
|---|---|---|---|
| Inter 4.001 (Regular, pinned from Inter[opsz,wght]) | github.com/rsms/inter, via github.com/google/fonts (ofl/inter) | Latin, Latin Extended-A, Greek, punctuation, super- and subscripts, arrows, maths, technical, geometric shapes: 634 glyphs | SIL OFL 1.1, [fonts/OFL-Inter.txt](fonts/OFL-Inter.txt) |
| Noto Sans Math 3.000 | github.com/notofonts/math, via github.com/google/fonts (ofl/notosansmath) | the 24 maths symbols Inter lacks (∇ ∀ ∃ ∈ ∠ ⊥ ∥ ⌀ ...) | SIL OFL 1.1, [fonts/OFL-NotoSansMath.txt](fonts/OFL-NotoSansMath.txt) |
| Lucide | github.com/lucide-icons/lucide (lucide.ttf) | 28 icons (open, play, step, fit, ...) | ISC (some icons MIT, from Feather), [fonts/LICENSE-Lucide.txt](fonts/LICENSE-Lucide.txt) |
| ProggyClean | bundled in nuklear.h (Tristan Grimmer) | the optional pixel font | MIT |

The subsets are Modified Versions under the OFL; neither font declares a
Reserved Font Name, so they keep their names.

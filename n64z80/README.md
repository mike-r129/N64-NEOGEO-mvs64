# n64z80: the N64-Z80 asm core

A plain copy of [N64-Z80](https://github.com/mike-r129/N64-Z80), a MIPS
assembly Z80 interpreter for the N64 that is bit-exact with `z80.c` (same
struct, same `z80_run` / `z80_step` behaviour, same bus cycle stamps). Build
mvs64 with `z80.c` instead with `make ... Z80_CORE=c` (see Makefile.mvs64); the
PC build always uses `z80.c`.

Source commit: `001662ef` (N64-Z80 PR #9, branch `docs/public-release`; same core as `92eac14`, with the license and attribution notes added).

| File | From N64-Z80 |
| --- | --- |
| `n64z80.h`, `n64z80_offsets.h`, `n64z80.c`, `n64z80_asm.S`, `LICENSE` | copied unchanged (`NOTICE` is local to mvs64) |
| `n64z80_tables.h` | generated there by `tools/gen_tables.py reference/z80.c` (`build/n64z80_tables.h`); its reference is mvs64 d04c08f's `z80.c` |

If `z80.c` changes, re-sync N64-Z80's reference, re-run its differential
test, regenerate the tables and copy the files again. Don't edit them here.

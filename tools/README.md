# tools

Helper scripts for the measurement and correctness gates. Shell scripts run
from the repository root (Linux or WSL); the Python scripts need Python 3.

| Script | Use |
| --- | --- |
| `compare-fbcrc.py` | Pixel gate: compare the `[FBCRC]` frame checksums of two `MVS64_FBCRC` runs. |
| `trcdiff.sh` | 68k gate: first divergence between two `[TRCRC]` streams (`TRCRC_ON=1` builds). |
| `iodiff.sh` | First divergence between two `[IO]` MMIO read streams (`MVS64_IOLOG_N64` builds). |
| `posdcmp.py` | Compare two `MVS64_PERFOSD` logs window by window, over busy windows with matching tile counts. |
| `analyze-ophist.py` | Rank the `[OPHIST]` 68k opcode histogram by form and fast-path coverage. |
| `analyze-pcprof.py` | Symbolize an `MVS64_PCPROF` dump against `mips64-elf-nm -n -S` of the same ELF. |
| `nm-hotsets.sh` | Addresses and cache sets of the hot code and data in an ELF. |
| `nm-perfsyms.sh` | Cache sets of the 68k perf counters against the m64k context. |
| `wavcheck.py` | Continuity, gap and click report for a PC harness `MVS64_WAV` capture. |
| `l0.py` | Derives the vertical-shrink table in `video.c`. |
| `z80replay.c` | Replays an `MVS64_Z80TRACE` recording through a Z80 core and checks every recorded result. Format: [z80trace-format.md](z80trace-format.md). Build: `gcc -O2 -I. -o z80replay tools/z80replay.c z80.c`. |
| `z80tstat.py` | Record-type histogram of a Z80 trace, including the IRQ/BANK effects inside port callbacks. |

The gates compare two builds of the same tree that differ in one switch,
fed the same scripted input (`MVS64_INPUT` on PC, `make ... INPUT=` on N64).
Builds from different trees can diverge for reasons unrelated to the change
under test, and `MVS64_DET_AUDIO` keeps the audio pump from making N64 runs
depend on wall-clock speed.

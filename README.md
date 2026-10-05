## MVS64 -- A NeoGeo emulator for Nintendo 64

<img src="https://github.com/rasky/mvs64/raw/main/screens/image0.jpg" width="320">
<img src="https://github.com/rasky/mvs64/raw/main/screens/pbobblen.png" width="320">

### Status

MVS64 is still in an early stage. Only a handful of games boot or work, and
only Samurai Shodown II (`samsho2`) has been tested with everything below.

- **Sound** is emulated: the Z80 sound CPU and the YM2610 (FM, SSG,
  ADPCM-A/B). The FM and ADPCM synthesis runs on the RSP. Audio plays at
  11,025 Hz on N64, and the ADPCM samples are streamed from the cart.
- **Speed:** samsho2's attract mode runs at 60 fps in ares. Fights do not
  hold 60 fps: they ran around 50 fps on a real console, down to about 42 in
  the heaviest scenes, in the samsho2 fork this work comes from.
- **Expansion Pak** recommended: with 8 MB the C-ROM tile cache gets 4,096
  slots instead of 1,280, which matters in busy scenes.
- Other games need their own idle-skip entries (see below) and may hit
  hardware features that are not emulated yet, such as the LSPC timer
  interrupt used for raster effects.

### How to build

MVS64 is written using [libdragon](https://github.com/DragonMinded/libdragon).
The build system assumes that you will be using the official Docker container
for libdragon ([libdragon-docker](https://github.com/anacierdem/libdragon-docker)),
that will work on Windows (under WSL), Linux and Mac. First, follow the installation
instructions of libdragon-docker if you haven't already.

Once you have the docker container configured, clone mvs64:

	$ git clone https://github.com/rasky/mvs64
	$ cd mvs64

Start the docker container in the mvs64 directory:

	$ libdragon start

Now build mvs64:

	$ make mvs64 BIOS=<path/to/bios.bin> ROM=<path/to/game.zip>

When building, you need to specify the path to a NeoGeo BIOS file that you
want to use, and the path to a NeoGeo game ROM, as a zip file. Both `BIOS` and
`ROM` are actually environment variables, so you can set them in your environment
once to avoid specifying them on the command line (doing that for `BIOS` is
especially useful, as you rarely change that).

This command will create a Nintendo 64 ROM called `mvs64-<gamename>.z64`, that
you can use with an emulator or on a real console using a development kit
like 64drive or EverDrive 64. `mvsmakerom` prints the size of the converted
ROM set; most flash carts take at most 64 MB.

**NOTE**: during the build, the path of the BIOS will be inspected to search for
a ROM called `sfix.sfix`, which is also part of the standard BIOS sets. That
ROM must reside in the same folder of the specified BIOS.

#### libdragon patches (RSP audio offload)

The sound emulation runs the YM2610 synthesis on the RSP, issuing hundreds of
short high-priority rspq segments per second. That load hits two races in
libdragon's rspq that wedge the RSP on real hardware (the RSP sleeps with work
pending, and the next wait times out into the crash screen). Until the fixes
are merged into libdragon, apply the patches in `patches/` to the libdragon
source tree you build from (with libdragon-docker, the `libdragon/` folder
of this repository), then rebuild and reinstall libdragon:

	$ cd libdragon
	$ patch -p1 < ../patches/libdragon-rspq-closed-loop-flush.patch
	$ patch -p1 < ../patches/libdragon-rspq-highpri-wedge.patch
	$ patch -p1 < ../patches/libdragon-rspq-lowpri-size.patch
	$ cd .. && libdragon install

The first two fix the races; the third lets mvs64 enlarge the rspq lowpri
command buffers (a busy frame issues ~10 KB of video commands), and without it
the buffers keep libdragon's default size. To build without the RSP audio
offload (all synthesis on the CPU, much slower), add `ADPCM_CPU=1 WP_OFF=1`
to the `make mvs64` command line.

#### Build options

Add these to the `make mvs64` command line:

| Option | Effect |
| --- | --- |
| `EXTRA_DEFINES=-DMVS64_QUIET` | Release build: the per-frame debug log is compiled out (it costs frame time on hardware) |
| `FRAMESKIP=n` | Skip drawing up to n frames in a row when emulation falls behind, so game speed holds; off by default |
| `INPUT=<file>` | Replay a scripted input file (see "Scripted input" below) |
| `WP_OFF=1`, `ADPCM_CPU=1 WP_OFF=1` | Turn off the RSP whole-pump audio offload, or all RSP audio |
| `FP_OFF=1`, `BLOCKOPS_OFF=1`, `PORTSTORE_OFF=1` | Turn off the 68000 fast paths, the fused DBF copy/fill loops, or the inline video-port stores (for A/B tests) |
| `MUSASHI=1` | Use the portable Musashi 68000 core instead of m64k (much slower; for isolating m64k bugs) |

On the N64 controller, Z inserts a coin and C-up is the select button.

#### Per-game settings and the idle skip

`mvsmakerom` writes a `game.ini` next to the converted ROMs, from a small
database keyed by the game's NGH number. Its main entry is
`idle_skip=pc[,pc...]`: the addresses of the game's vblank-wait loops. When
the 68000 branches back to one of them, the emulator ends the time slice
instead of interpreting the wait, which is a large speedup. Each address must
be a loop that only polls a flag set by an interrupt, such as
`loop: tst.b flag; beq loop`; anything else changes the game's behavior. The
Universe BIOS 4.0 vblank wait is added automatically.

To find the loops of a new game, build with `EXTRA_DEFINES=-DMVS64_IDLEPROBE`.
The log then prints `[IDLEPROBE] long spin at 68k pc=...` for every branch
target that runs thousands of times in a row. Check the disassembly at that
address, and if it is a pure poll, add it to the game's row in `mvsmakerom.c`.

### Diagnostics

All of these are off by default and compile to nothing in a normal build.
Most are enabled with `EXTRA_DEFINES=-D<name>`.

| Switch | What it does |
| --- | --- |
| `MVS64_PERFOSD` | Frame-time overlay on screen (see below), also logged as `[PERFOSD]` |
| `MVS64_SNDOSD` | Audio-health overlay on screen (see below) |
| `MVS64_SNDHEALTH` | Audio-health logs (`[AIPUMP]`, `[SNDRMS]`), also written to `sd:/mvs64log.txt` on flash carts whose SD card libdragon supports |
| `MVS64_PERFCOUNT` | Per-frame counters: 68000 instructions, idle skips, MMIO traps, draw and DMA split (`[PERF]`, `[PERF2]`, `[PERF3]`) |
| `MVS64_OPHIST`, `MVS64_PCPROF=<frame>` | 68000 opcode histogram; sampling profiler of the N64 CPU |
| `MVS64_FBCRC` | Hash of every finished frame (`[FBCRC]`), for pixel-exact A/B comparisons |
| `TRCRC_ON=1` (make option) | Hash of the 68000 state per frame (`[TRCRC]`), for execution-exact A/B comparisons |
| `MVS64_DET_AUDIO` | Generate exactly one frame of audio per frame, so that runs of builds with different speeds stay comparable |

The scripts in `tools/` compare and summarize these logs; see
[tools/README.md](tools/README.md).

#### Reading the performance overlay

The overlay draws ten lines, refreshed every 60 frames. Times are
milliseconds per emulated frame, averaged over the window; a frame at 60 fps
has 16.7 ms.

| Line | Meaning |
| --- | --- |
| `F d g` | Frames drawn per second, then emulated (game-speed) frames per second; they differ only with `FRAMESKIP` |
| `M m S s` | 68000 time (including I/O and Z80 catch-up), sound time (Z80 + YM2610) |
| `V v W w` | Drawing time on the CPU, then time spent waiting for a free display buffer (large when the RDP is the bottleneck) |
| `B b L l` | Inside V: palette conversion at frame start, fix layer |
| `R r` | Inside V: sprites |
| `Q q E e` | Inside R: C-ROM tile lookups, RSP command issue; the rest of R is the sprite walk |
| `C c N n` | Inside Q: tile-cache misses read from the cart, then sprite tiles drawn per frame |
| `G g H h` | Tiles drawn in RDP COPY mode, then tiles that would be COPY but are flipped |
| `A a X x` | Whole frame, then the part of it no other line accounts for |
| `P p T t` | RDP pipe-busy and TMEM-busy time per drawn frame (real hardware only; ares reads 0) |

#### Reading the audio-health overlay

The overlay draws five lines in the top-left corner, refreshed every 60
frames.

| Line | Meaning | Healthy |
| --- | --- | --- |
| `F d g` | Drawn fps, then emulated fps | Varies by scene |
| `D wamh n` | RSP audio offloads marked dead (whole-pump, ADPCM, FM, fallback hatch), then the death count | `D 0000 0` |
| `K k R r W w` | Lost-wakeup watchdog kicks, offload revives, RSP queue wedges recovered by the libdragon patch | `K 0 R 0 W 0` |
| `S s n` | Silence governor engaged, then silence pads per second | `S 0 0` |
| `L n C n` | Buffered audio lead (samples), then samples played per second | `L` about two buffers, `C` about 11025 |

If sound dies but `C` stays near 11025, delivery still works and generation
stopped: check `D` and `S`. If `C` drops to 0, the audio interrupt chain died.

### Compatibility

| Game | NGH | Boots | Gameplay | Sound | Notes |
| --- | --- | --- | --- | --- | --- |
| Samurai Shodown II | 063 | Yes | Yes | Yes | The test game: 60 fps attract in ares; fights about 42-50 fps on hardware (measured in the samsho2 fork) |
| Metal Slug | 201 | ? | ? | ? | Idle-skip address from the original MVS64 list, not tested |

Other games have not been tested since sound was added.

### How to build the PC version of mvs64

mvs64 also includes a PC build of the emulator that can be used to further
test the emulation. In general, if a bug is present in the N64 version and
not in the PC version of mvs64, it is a bug of the Nintendo 64 backend.
Otherwise, it is a bug in the NeoGeo emulation layer.

To build the PC version, run:

	$ make pctest

This will build a `emu` binary. This PC version tries to stay as close
as possible to the N64 version, so it's not optimized to be a fully standalone
PC emulator. In particular, it doesn't load standard game ZIP files, but it
uses the preprocessed ROMs that are generated as part of the N64 build system.

To use the PC version of the emulator on a specific game, first build the N64
emulator for that game using the `make mvs64` command above.
During the build, you will notice that a folder called `game.n64/` (next to
`game.zip`) is created. That folder contains preprocessed ROMs that are then
embedded in the final `.z64` file. Pass that folder to the `emu` binary:

	$ ./emu <path/to/game.n64/>

#### Headless runs and scripted input

The PC build can also run without a window, which is how sound and video
changes are checked: two builds fed the same input must produce the same
audio and the same screenshots.

| Environment variable | Effect |
| --- | --- |
| `MVS64_FRAMES=n` | Run n frames headless, then exit |
| `MVS64_INPUT=<file>` | Replay a scripted input file |
| `MVS64_WAV=<file>` | Write the generated audio to a WAV file |
| `MVS64_SHOT=n` | Save a screenshot every n frames |

A scripted input file has one line per key press, `<first frame> <last frame>
<key>`, where the key is one of `coin start select a b c d up down left
right`; lines starting with `#` are comments. The N64 build replays the same
file when built with `make mvs64 ... INPUT=<file>`, at the same guest frames,
so the PC and N64 builds can be compared on the same game content.

### Credits and licenses

- **MVS64** and the **m64k** 68000 core: Giovanni Bajo (rasky), MIT
  ([LICENSE](LICENSE)).
- **Z80 core:** [superzazu/z80](https://github.com/superzazu/z80) by Nicolas
  Allemand, MIT ([z80.LICENSE](z80.LICENSE)).
- **YM2610:** the MAME FM sound core by Jarek Burczynski and Tatsuyuki Satoh,
  by way of NJ's pspmvs ([ym2610/LICENSE.mame](ym2610/LICENSE.mame)).
- **Musashi** 68000 core (PC build and fallback core): Karl Stenerud.
- **[libdragon](https://github.com/DragonMinded/libdragon):** the N64 SDK this
  is built on.

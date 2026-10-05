# Z80 owner trace format (`MVS64_Z80TRACE`), version 1

A trace records everything mvs64's sound owner (`sound_neogeo.c`) does to the
Z80 core over a stretch of real game audio. Replaying it through another core
must reproduce every recorded result. That makes a trace both a benchmark
on real driver code and a correctness test, including IRQ changes that happen
inside port callbacks.

Definitions (record IDs, state block, hash, CRC) are in `z80trace.h`. The
reference replay is `z80replay.c`; in mvs64 it lives in `local-tools/`, and a
copy ships next to the traces.

## Recording (mvs64 PC build)

```sh
make -f Makefile.pctests EXTRA_DEFINES=-DMVS64_Z80TRACE
MVS64_Z80TRACE=out.z80t MVS64_Z80TRACE_AT=42 MVS64_Z80TRACE_LEN=1 \
MVS64_FRAMES=2700 MVS64_INPUT=mslug_input.txt ./emu roms/mslug.n64/
```

- `AT` and `LEN` are in seconds of Z80 time (`cyc / 4,000,000`).
- Recording starts and stops at the entry of `sound_gen_samples()`, a clean
  boundary between owner loop iterations.

## File layout

All multi-byte fields are little-endian.

| Field | Size |
|---|---|
| magic `MVS64ZT1` | 8 |
| version (`u32`, = 1) | 4 |
| start state block | 43 |
| work RAM, 0xF800–0xFFFF | 2048 |
| bank offsets, `u32` × 4 (windows 0x8000/16K, 0xC000/8K, 0xE000/4K, 0xF000/2K), into the M1 ROM | 16 |
| M1 ROM size (`u32`) | 4 |
| the whole M1 ROM | size |
| record stream, ending with `END` | … |

### State block (43 bytes)

| Fields | Type | Bytes |
|---|---|---|
| `pc, sp, ix, iy, mem_ptr` (WZ) | `u16` each | 10 |
| `a f b c d e h l a' f' b' c' d' e' h' l' i r` | `u8` each | 18 |
| `interrupt_mode, int_data, iff1, iff2, iff_delay, halted, int_pending, nmi_pending, irq_line, wrote, wrote_any` | `u8` each | 11 |
| `cyc` (low 32 bits) | `u32` | 4 |

**State hash:** FNV-1a 32 over these 43 bytes (`z80t_state_hash`).

## Records

Each record is a type byte plus a fixed payload.

| Type | Name | Payload | Replay action |
|---|---|---|---|
| 0x01 | RUN | `u32 until` | `n = z80_run(until, &last_pc)` |
| 0x02 | RUN_END | `u32 nsteps, u16 last_pc, u32 hash` | check `n`, `last_pc` and the state hash |
| 0x03 | STEP | — | `z80_step()` |
| 0x04 | STEP_END | `u32 hash` | check the state hash |
| 0x10 | IN | `u16 port, u8 value, u32 cyc` | inside `port_in`: check port and `cyc`, return `value` |
| 0x11 | OUT | `u16 port, u8 value, u32 cyc` | inside `port_out`: check port, value and `cyc` |
| 0x20 | IRQ | `u8 level` | `irq_line = level; level ? z80_gen_int(0xFF) : (int_pending = 0)` |
| 0x21 | GENINT | `u8 data` | `z80_gen_int(data)` (owner's level re-delivery) |
| 0x22 | NMI | — | `z80_gen_nmi()` |
| 0x23 | SETCYC | `u32 cyc` | `cyc = value` (idle skip) |
| 0x24 | SETR | `u8 r` | `r = value` (idle skip) |
| 0x25 | BANK | `u8 window, u32 offset` | remap that window to M1 ROM + offset |
| 0x30 | RAMCRC | `u32 crc` | CRC-32 (zlib) of the 2 KB RAM must match, checked every 4096 RUNs |
| 0x7F | END | state block + 2 KB RAM | final state and RAM must match |

### Nesting rule (callbacks)

Records between a RUN/STEP and its RUN_END/STEP_END come from the callbacks
the core makes during that call, in order:

- **Each IN or OUT record comes first.** It is immediately followed by its
  *effects*: the IRQ and BANK records caused inside that callback.
  - A YM timer reset written by OUT can drop the IRQ line.
  - An IN from ports 0x08–0x0B switches a bank.
- The replay's `port_in`/`port_out` consumes its own record, then applies the
  run of IRQ/BANK records that follows, **before returning to the core**.
- Inside a run, no other record type can follow a callback record. Outside
  runs, IRQ/GENINT/NMI/SETCYC/SETR/BANK are owner actions; apply them in
  order.
- Memory writes are not recorded. They're deterministic, and RAMCRC and END
  check them.

### Replay owner behavior (must match mvs64)

- **Reads:** 0x0000–0x7FFF come from M1 ROM (fixed). The four windows come
  from M1 ROM at their current offsets. 0xF800–0xFFFF come from RAM.
- **Writes:** only 0xF800–0xFFFF store. **Every** memory write and every OUT
  sets `wrote = 1`; mvs64's callbacks do this. A core that manages `wrote`
  itself must produce the same values: the state hash includes `wrote` and
  `wrote_any`.
- **State loading:** load the start state with plain field stores
  (`z80t_state_load`). A core with derived state, such as an event mask, must
  rebuild it on entry to `z80_run`/`z80_step`.

## Validation (2026-10-05, mvs64 main 61d8574)

All traces replay through mvs64's C core with 0 mismatches. Planted bugs in
the reference core (BIT's H flag, R's bit-7 preservation, IM1 accept cycles)
each produce thousands of mismatches or desync the replay at the first
interrupt.

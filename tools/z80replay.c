// z80replay: replays an MVS64_Z80TRACE file through a Z80 core (PC build)
// and checks every recorded result: RUN_END step counts, last_pc and state
// hashes, STEP_END hashes, the IN/OUT port + value + cycle of every callback,
// RAM CRC checkpoints and the end state + RAM.
//
// It is also the reference for how a replay owner must behave (see
// tools/z80trace-format.md):
//   - memory: fixed M1 ROM at 0x0000-0x7FFF, four banked windows (offsets
//     from the header / BANK records), 2 KB work RAM at 0xF800; writes below
//     0xF800 are ignored; every write and OUT sets cpu.wrote = 1.
//   - port_in returns the recorded value; port_out checks the recorded value;
//     after either, the IRQ/BANK records that follow are applied before the
//     callback returns.
//
// build (from the mvs64 root): gcc -O2 -I. -o z80replay tools/z80replay.c z80.c
// usage: z80replay <trace> [max mismatches to print]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "z80trace.h"

#define cpu z80_hot.cpu
static const uint8_t *rom; static uint32_t rom_size;
static uint8_t ram[0x800];
static uint32_t bank_off[4];
static const uint32_t win_base[4] = { 0x8000, 0xC000, 0xE000, 0xF000 };
static const uint32_t win_size[4] = { 0x4000, 0x2000, 0x1000, 0x0800 };
static uintptr_t rmap[256];
static const uint8_t *t, *tend;        // trace cursor
static long bad, maxbad = 10, nruns, nsteps_total, nio;

static void fail(const char *what) {
  if (bad++ < maxbad)
    fprintf(stderr, "MISMATCH at trace offset %ld: %s (pc=%04x cyc=%lu)\n",
            (long)(t - (tend - (tend - t))), what, cpu.pc, (unsigned long)cpu.cyc);
}
static void map_fill(unsigned lo, unsigned hi, const uint8_t *base) {
  for (unsigned p = lo; p < hi; p++) rmap[p] = (uintptr_t)base - ((uintptr_t)lo << 8);
}
static void map_window(int w) {
  map_fill(win_base[w] >> 8, (win_base[w] + win_size[w]) >> 8, rom + bank_off[w]);
}
static uint8_t rd(void *ud, uint16_t a) { (void)ud; return *(const uint8_t *)(rmap[a >> 8] + a); }
static void wr(void *ud, uint16_t a, uint8_t v) {
  (void)ud;
  if (a >= 0xF800) ram[a - 0xF800] = v;
  cpu.wrote = 1;
}
static void apply_irq(uint8_t level) {
  cpu.irq_line = level;
  if (level) z80_gen_int(&cpu, 0xFF); else cpu.int_pending = 0;
}
// Apply the IRQ/BANK effect records that follow a callback record.
static void effects(void) {
  while (t < tend && (*t == Z80T_IRQ || *t == Z80T_BANK)) {
    if (*t == Z80T_IRQ) { apply_irq(t[1]); t += 2; }
    else { bank_off[t[1]] = z80t_get32(t + 2); map_window(t[1]); t += 6; }
  }
}
static int port_rec(uint8_t type, uint16_t port, uint8_t *val) {
  if (t >= tend || *t != type) { fail(type == Z80T_IN ? "expected IN record" : "expected OUT record"); return 0; }
  if (z80t_get16(t + 1) != port) fail("port address differs");
  if (z80t_get32(t + 4) != (uint32_t)cpu.cyc) fail("callback cycle stamp differs");
  if (type == Z80T_OUT && t[3] != *val) fail("OUT value differs");
  *val = t[3];
  t += 8; nio++;
  return 1;
}
static uint8_t pin(z80 *z, uint16_t port) {
  (void)z; uint8_t v = 0;
  if (port_rec(Z80T_IN, port, &v)) effects();
  return v;
}
static void pout(z80 *z, uint16_t port, uint8_t val) {
  (void)z;
  cpu.wrote = 1;
  if (port_rec(Z80T_OUT, port, &val)) effects();
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: z80replay <trace> [maxprint]\n"); return 2; }
  if (argc > 2) maxbad = atol(argv[2]);
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror(argv[1]); return 2; }
  fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
  uint8_t *buf = malloc(sz);
  if (fread(buf, 1, sz, f) != (size_t)sz) { perror("read"); return 2; }
  fclose(f);
  t = buf; tend = buf + sz;
  if (memcmp(t, Z80T_MAGIC, 8) || z80t_get32(t + 8) != Z80T_VERSION) { fprintf(stderr, "bad header\n"); return 2; }
  t += 12;
  z80_init(&cpu);
  z80t_state_load(&cpu, t); t += Z80T_STATE_SIZE;
  memcpy(ram, t, 0x800); t += 0x800;
  for (int w = 0; w < 4; w++) { bank_off[w] = z80t_get32(t); t += 4; }
  rom_size = z80t_get32(t); t += 4;
  rom = t; t += rom_size;
  map_fill(0x00, 0x80, rom);
  for (int w = 0; w < 4; w++) map_window(w);
  map_fill(0xF8, 0x100, ram);
  cpu.rmap = rmap; cpu.read_byte = rd; cpu.write_byte = wr;
  cpu.port_in = pin; cpu.port_out = pout; cpu.userdata = NULL;
  printf("trace %s: %ld bytes, M1 ROM %u bytes, start pc %04x cyc %lu\n",
         argv[1], sz, rom_size, cpu.pc, (unsigned long)cpu.cyc);

  clock_t c0 = clock();
  int ended = 0;
  while (t < tend && !ended) {
    uint8_t type = *t++;
    switch (type) {
    case Z80T_RUN: {
      unsigned long until = z80t_get32(t); t += 4;
      uint16_t last_pc;
      unsigned n = z80_run(&cpu, until, &last_pc);
      if (t >= tend || *t != Z80T_RUN_END) { fail("expected RUN_END (callback records left over?)"); break; }
      if (z80t_get32(t + 1) != n) fail("RUN step count differs");
      if (z80t_get16(t + 5) != last_pc) fail("RUN last_pc differs");
      if (z80t_get32(t + 7) != z80t_state_hash(&cpu)) fail("RUN end-state hash differs");
      t += 11; nruns++; nsteps_total += n;
    } break;
    case Z80T_STEP:
      z80_step(&cpu);
      if (t >= tend || *t != Z80T_STEP_END) { fail("expected STEP_END"); break; }
      if (z80t_get32(t + 1) != z80t_state_hash(&cpu)) fail("STEP end-state hash differs");
      t += 5; nsteps_total++;
      break;
    case Z80T_IRQ: apply_irq(*t++); break;
    case Z80T_GENINT: z80_gen_int(&cpu, *t++); break;
    case Z80T_NMI: z80_gen_nmi(&cpu); break;
    case Z80T_SETCYC: cpu.cyc = z80t_get32(t); t += 4; break;
    case Z80T_SETR: cpu.r = *t++; break;
    case Z80T_BANK: bank_off[t[0]] = z80t_get32(t + 1); map_window(t[0]); t += 5; break;
    case Z80T_RAMCRC:
      if (z80t_get32(t) != z80t_crc32(ram, 0x800)) fail("RAM CRC differs");
      t += 4; break;
    case Z80T_END: {
      uint8_t s[Z80T_STATE_SIZE]; z80t_state_save(&cpu, s);
      if (memcmp(s, t, Z80T_STATE_SIZE)) fail("END state differs");
      if (memcmp(ram, t + Z80T_STATE_SIZE, 0x800)) fail("END RAM differs");
      t += Z80T_STATE_SIZE + 0x800; ended = 1;
    } break;
    default:
      fprintf(stderr, "unknown or misplaced record %02x at offset %ld\n", type, (long)(t - 1 - buf));
      return 2;
    }
  }
  double secs = (double)(clock() - c0) / CLOCKS_PER_SEC;
  printf("%s: %ld runs, %ld instructions, %ld IN/OUT, %ld mismatches, %.2f s\n",
         ended ? "replayed to END" : "TRACE TRUNCATED", nruns, nsteps_total, nio, bad, secs);
  return (bad || !ended) ? 1 : 0;
}

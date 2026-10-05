// Per-frame diagnostic telemetry for instrumented N64 builds. emu.c's main
// loop calls emu_diag_frame() once per guest frame (EMU_DIAG, emu.h, is set
// when any of the diagnostics below is built in); in a release build the
// file compiles to nothing.
#include <stdio.h>
#include <string.h>
#include "emu.h"
#include "platform.h"
#ifdef EMU_DIAG
#ifdef USE_M64K
#include "m64k/m64k.h"
#endif

extern int g_frame;
extern uint32_t perf_m68k_slices;
#ifdef USE_M64K
extern m64k_t m64k;
#endif

void emu_diag_frame(void) {
	#ifdef MVS64_PERFCOUNT
	{
		// Diagnostic counters (m64k_asm.S / hw_n64.S): executed 68k
		// instructions, idle-skip fires and TLB exceptions this frame,
		// plus the draw-bucket split from emu_render (in 0.01%-of-frame
		// units to stay integer: 100.00% == 10000).
		extern uint32_t perf_m68k_insns, perf_idle_skips, perf_tlb_faults;
		extern uint32_t perf_draw_wait, perf_draw_issue, perf_draw_end;
		extern uint32_t perf_snd_pub;
		const uint32_t fb = TICKS_PER_SECOND / 60 / 10000;  // ticks per 0.01%
		{
			// Trap histogram by 64KB page and direction (hw_n64.S),
			// over guest frames [MVS64_TRAPH_FROM, +MVS64_TRAPH_FRAMES),
			// dumped at the end as traps per frame x100.
			#ifndef MVS64_TRAPH_FROM
			#define MVS64_TRAPH_FROM   600
			#endif
			#ifndef MVS64_TRAPH_FRAMES
			#define MVS64_TRAPH_FRAMES 3000
			#endif
			extern uint32_t perf_trap_hist[512];
			extern uint32_t perf_trap_ring[2048], perf_trap_ring_n;
			if (g_frame == MVS64_TRAPH_FROM) {
				memset(perf_trap_hist, 0, sizeof(uint32_t) * 512);
				perf_trap_ring_n = 0;
			}
			if (g_frame == MVS64_TRAPH_FROM + MVS64_TRAPH_FRAMES) {
				for (uint32_t i = 0; i < perf_trap_ring_n; i++)
					debugf("[TRAPS] epc=%08lx pc=%06lx\n",
					       (unsigned long)perf_trap_ring[2*i],
					       (unsigned long)(perf_trap_ring[2*i+1] & 0xFFFFFF));
				for (int i = 0; i < 512; i++)
					if (perf_trap_hist[i])
						debugf("[TRAPH] %s page=%02x per_frame_x100=%lu\n",
						       i >= 256 ? "W" : "R", i & 255,
						       (unsigned long)(perf_trap_hist[i] * 100UL / MVS64_TRAPH_FRAMES));
			}
		}
		framef("[PERF] insns=%lu skips=%lu tlb=%lu slices=%lu dwait=%lu dissue=%lu dend=%lu pub=%lu\n",
			(unsigned long)perf_m68k_insns,
			(unsigned long)perf_idle_skips,
			(unsigned long)perf_tlb_faults,
			(unsigned long)perf_m68k_slices,
			(unsigned long)(perf_draw_wait / fb),
			(unsigned long)(perf_draw_issue / fb),
			(unsigned long)(perf_draw_end / fb),
			(unsigned long)(perf_snd_pub / fb));
		perf_m68k_insns = perf_idle_skips = perf_tlb_faults = 0;
		perf_m68k_slices = 0;
		perf_draw_wait = perf_draw_issue = perf_draw_end = 0;
		perf_snd_pub = 0;

		// Fine split of the draw-issue bucket (video.c): phase
		// ticks (same 0.01%-of-frame units) + per-frame draw counts.
		// walk = sprite pass minus cache lookups minus rspq issue.
		extern uint32_t perf_dr_begin, perf_dr_sprites, perf_dr_fix;
		extern uint32_t perf_dr_cache, perf_dr_rspq;
		extern uint32_t perf_dr_tiles, perf_dr_cells, perf_dr_empty;
		extern uint32_t perf_walk_spr, perf_walk_iter;
		framef("[PERF2] begin=%lu spr=%lu (cache=%lu rspq=%lu) fix=%lu tiles=%lu cells=%lu empty=%lu wspr=%lu witer=%lu\n",
			(unsigned long)(perf_dr_begin / fb),
			(unsigned long)(perf_dr_sprites / fb),
			(unsigned long)(perf_dr_cache / fb),
			(unsigned long)(perf_dr_rspq / fb),
			(unsigned long)(perf_dr_fix / fb),
			(unsigned long)perf_dr_tiles,
			(unsigned long)perf_dr_cells,
			(unsigned long)perf_dr_empty,
			(unsigned long)perf_walk_spr,
			(unsigned long)perf_walk_iter);
		perf_dr_begin = perf_dr_sprites = perf_dr_fix = 0;
		perf_dr_cache = perf_dr_rspq = 0;
		perf_dr_tiles = perf_dr_cells = perf_dr_empty = 0;
		perf_walk_spr = perf_walk_iter = 0;

		// [PERF3]: C-ROM cache-miss split (roms.c), RDP busy fractions
		// from the free-running 24-bit DPC counters (delta per window,
		// wrap-safe at >=4fps; dppipe/dpclk ~= RDP pipe busy fraction,
		// dptmem = TMEM loads) and ADPCM V-ROM refills.
		{
			extern uint32_t perf_dr_miss, perf_dr_missticks;
			static uint32_t dpc_clk0, dpc_pipe0, dpc_tmem0;
			uint32_t clk  = *(volatile uint32_t*)0xA4100010 & 0xFFFFFF;
			uint32_t pipe = *(volatile uint32_t*)0xA4100018 & 0xFFFFFF;
			uint32_t tmem = *(volatile uint32_t*)0xA410001C & 0xFFFFFF;
			extern uint32_t perf_vrom_reads, perf_vrom_ticks;
			framef("[PERF3] miss=%lu dmat=%lu dpclk=%lu dppipe=%lu dptmem=%lu vrom=%lu vromt=%lu\n",
				(unsigned long)perf_dr_miss,
				(unsigned long)(perf_dr_missticks / fb),
				(unsigned long)((clk  - dpc_clk0)  & 0xFFFFFF),
				(unsigned long)((pipe - dpc_pipe0) & 0xFFFFFF),
				(unsigned long)((tmem - dpc_tmem0) & 0xFFFFFF),
				(unsigned long)perf_vrom_reads,
				(unsigned long)(perf_vrom_ticks / fb));
			perf_vrom_reads = perf_vrom_ticks = 0;
			dpc_clk0 = clk; dpc_pipe0 = pipe; dpc_tmem0 = tmem;
			perf_dr_miss = perf_dr_missticks = 0;
		}
	}
	#endif
	#ifdef MVS64_IDLEPROBE
	{
		extern uint32_t idle_probe_found;
		if (idle_probe_found) {
			debugf("[IDLEPROBE] long spin at 68k pc=%06lx\n",
				(unsigned long)(idle_probe_found & 0xFFFFFF));
			idle_probe_found = 0;
		}
	}
	#endif
}
#endif

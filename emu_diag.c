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

#ifdef MVS64_OPHIST
// Exact per-opcode execution histogram; bumped per dispatched instruction
// in m64k_asm.S (m64k_ophist_ptr points here). 256KB, diagnostic only.
uint32_t m64k_ophist_tab[65536] __attribute__((aligned(16)));
#endif

void emu_diag_frame(void) {
	#ifdef MVS64_PCPROF
	{
		extern void pcprof_frame(int frame, int counted);
		pcprof_frame(g_frame, g_frame >= MVS64_PCPROF);
	}
	#endif
	#ifdef M64K_TRACECRC
	{
		// Per-frame 68k state-trace hash (m64k.c): two runs of the same
		// build and inputs must emit identical [TRCRC] streams, and so must
		// two builds that differ only in a change meant to keep 68k
		// execution identical.
		extern uint32_t __m64k_tracecrc, __m64k_tracecrc_slices;
		framef("[TRCRC] f=%d crc=%08lx slices=%lu\n", g_frame,
			(unsigned long)__m64k_tracecrc,
			(unsigned long)__m64k_tracecrc_slices);
		__m64k_tracecrc = 2166136261u;
		__m64k_tracecrc_slices = 0;
		#ifdef M64K_TRCRC_SPLIT
		{
			// Content-only hash (regs/SR, no pc/cycles): separates
			// timing displacement from real state divergence.
			extern uint32_t __m64k_tracecrc_content;
			framef("[TRCCON] f=%d crc=%08lx\n", g_frame,
				(unsigned long)__m64k_tracecrc_content);
			__m64k_tracecrc_content = 2166136261u;
		}
		#endif
	}
	#endif
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
	#ifdef MVS64_OPHIST
	// Exact per-opcode execution histogram (bumped in m64k_asm.S's
	// dispatch). Every 300 frames: dump every opcode above ~0.05% of
	// the interval's executed instructions, then reset.
	if ((g_frame % 300) == 299) {
		uint64_t total = 0;
		for (int i = 0; i < 65536; i++) total += m64k_ophist_tab[i];
		uint32_t thresh = (uint32_t)(total / 2000);
		if (thresh < 4) thresh = 4;
		enum { OPHIST_MAX = 384 };
		static uint16_t sel_op[OPHIST_MAX];
		static uint32_t sel_n[OPHIST_MAX];
		int nsel = 0;
		uint64_t selected = 0;
		for (int i = 0; i < 65536 && nsel < OPHIST_MAX; i++) {
			if (m64k_ophist_tab[i] >= thresh) {
				sel_op[nsel] = (uint16_t)i;
				sel_n[nsel] = m64k_ophist_tab[i];
				selected += m64k_ophist_tab[i];
				nsel++;
			}
		}
		// insertion sort, descending by count (nsel <= 384)
		for (int i = 1; i < nsel; i++) {
			uint16_t o = sel_op[i]; uint32_t n = sel_n[i]; int j = i - 1;
			while (j >= 0 && sel_n[j] < n) {
				sel_op[j+1] = sel_op[j]; sel_n[j+1] = sel_n[j]; j--;
			}
			sel_op[j+1] = o; sel_n[j+1] = n;
		}
		framef("[OPHIST] total=%llu sel=%llu nsel=%d\n",
			(unsigned long long)total, (unsigned long long)selected, nsel);
		for (int i = 0; i < nsel; i += 8) {
			char line[160]; int p = 0;
			for (int j = i; j < nsel && j < i + 8; j++)
				p += sprintf(line + p, " %04x:%lu",
					sel_op[j], (unsigned long)sel_n[j]);
			framef("[OPH]%s\n", line);
		}
		memset(m64k_ophist_tab, 0, sizeof(m64k_ophist_tab));
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

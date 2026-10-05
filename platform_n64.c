#include "platform.h"
#include "sound.h"
#include <memory.h>
#include <stdio.h>
#include <stdarg.h>

// Telemetry logger — see platform.h. Writes to the libdragon debug channels
// (USB + emulator ISViewer, via stderr/debugf).
void plat_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

volatile int N64_FRAME = 0;
uint32_t RSP_OVL_ID = 0;
uint32_t RSP_AUDIO_OVL_ID = 0;
uint32_t RSP_FM_OVL_ID = 0;

DEFINE_RSP_UCODE(rsp_video);
DEFINE_RSP_UCODE(rsp_audio);
DEFINE_RSP_UCODE(rsp_fm);

// Boot-time self-test of the audio RSP overlay: round-trip a small buffer
// through cmd_adpcm_test (DMA in, +1 every byte, DMA out). Proves overlay
// registration, command dispatch and both DMA directions before the ADPCM
// offload ever runs. One [RSPAUDIO] line either way.
static void rsp_audio_selftest(void) {
    static uint8_t src[64] __attribute__((aligned(16)));
    static uint8_t dst[64] __attribute__((aligned(16)));
    for (int i = 0; i < 64; i++) { src[i] = (uint8_t)(i * 3 + 7); dst[i] = 0; }
    data_cache_hit_writeback_invalidate(src, sizeof(src));
    data_cache_hit_writeback_invalidate(dst, sizeof(dst));
    rspq_write(RSP_AUDIO_OVL_ID, 0x0, PhysicalAddr(src), PhysicalAddr(dst),
               64 - 1 /* DMA_SIZE(64, 1) */);
    rspq_wait();
    data_cache_hit_invalidate(dst, sizeof(dst));
    int bad = -1;
    for (int i = 0; i < 64; i++) {
        if (dst[i] != (uint8_t)(src[i] + 1)) { bad = i; break; }
    }
    if (bad < 0)
        debugf("[RSPAUDIO] selftest OK\n");
    else
        debugf("[RSPAUDIO] selftest FAIL at %d: got %02x want %02x\n",
               bad, dst[bad], (uint8_t)(src[bad] + 1));
}

uint8_t keystate[256];

extern char end __attribute__((section (".data")));

// Audio (libdragon AI) state — see plat_audio_pump below.
static int audio_enabled = 0;
#define AI_NUM_BUFFERS 4          // AI back buffers handed to audio_init

#ifdef MVS64_RSPWP
// rspq lost-wakeup watchdog kicks (see plat_audio_pump).
static uint32_t rspwp_wedge_kicks;
#endif
// Consecutive pump passes that observed ISR silence-padding (the overload
// governor input, see plat_audio_pump).
static int underrun_streak;

// --- Interrupt-fed staging ring ---------------------------------------------
// The N64 AI hardware replays its last DMA buffer forever when its queue runs
// dry (there is no silence-on-underrun mode), and libdragon's AI interrupt
// fires on buffer LATCH, not on a timer: once the queue fully drains there is
// nothing left to latch, the interrupt chain dies, and nothing plays new data
// until the main loop's next audio_write_end() restarts it. A main-loop pump
// therefore cannot prevent replay: one sound_gen_samples() call can take
// 100-300ms when the Z80 is busy (a boot jingle), the queue holds ~160ms,
// and during the call nothing refills or even pushes queued buffers to the
// hardware. That drain-replay is heard as a stuck high-pitch tone.
//
// The fix is pull-based delivery: audio_set_buffer_callback() makes libdragon
// invoke audio_ring_cb() in AI-interrupt context every time the hardware
// needs a buffer. The callback drains this staging ring; if the ring is empty
// it pads clean silence. In callback mode libdragon refills the queue on
// every latch interrupt, so the chain is self-sustaining and the queue can
// never run dry — a stale-buffer replay is physically impossible. The main
// loop's only job is topping the ring up (plat_audio_pump below).
#define ARING_FRAMES 8192              // power of two; ~0.74s @11025Hz stereo
static int16_t aring[ARING_FRAMES * 2];        // stereo frames, cached memory
static volatile uint32_t aring_wr;             // frames produced (main loop)
static volatile uint32_t aring_rd;             // frames consumed (AI IRQ)
static volatile uint32_t aring_pad;            // frames the IRQ padded with silence

// Runs in AI-interrupt context. `buffer` is an uncached libdragon AI buffer
// (malloc_uncached) — write it with packed 32-bit stores (one stereo frame
// per store), same trick sound_neogeo.c's emit() uses, since 16-bit stores
// to uncached RDRAM are twice the transactions.
static void audio_ring_cb(short *buffer, size_t numsamples) {
    uint32_t *dst = (uint32_t *)buffer;
    uint32_t rd = aring_rd;
    uint32_t avail = aring_wr - rd;         // unsigned wraparound-safe
    uint32_t take = avail < (uint32_t)numsamples ? avail : (uint32_t)numsamples;
    for (uint32_t i = 0; i < take; i++) {
        uint32_t s = (rd + i) & (ARING_FRAMES - 1);
        dst[i] = ((uint32_t)(uint16_t)aring[s * 2 + 0] << 16) | (uint16_t)aring[s * 2 + 1];
    }
    for (uint32_t i = take; i < (uint32_t)numsamples; i++)
        dst[i] = 0;
    aring_rd = rd + take;
    aring_pad += (uint32_t)numsamples - take;
}

static void vblank_handler(void) {
    N64_FRAME++;
}

void plat_init(int audiofreq, int fps) {
#ifdef __LIBDRAGON_DEBUG_H
    debug_init_isviewer();
    debug_init_usblog();
#endif
    debugf("MVS64\n");
    register_VI_handler(vblank_handler);

    char *heap_top = (char*)0x80000000 + get_memory_size() - 0x10000;
    char *heap_end = &end;
    debugf("heap [%p - %p = %d]\n", heap_top, heap_end, heap_top-heap_end);

	controller_init();
    // NOTE: there seems to be a bug in libdragon display library when ANTIALIAS_OFF
    // is used. Some RDP register is not configured correctly and the display is
    // corrupted on NTSC consoles.
	extern int mvs64_display_buffers;   // 3 (see plat_detach_show), or 2
	display_init(RESOLUTION_320x240, DEPTH_16_BPP, mvs64_display_buffers, GAMMA_NONE, ANTIALIAS_RESAMPLE);
    dfs_init(DFS_DEFAULT_LOCATION);
    rdpq_init();
    // rdpq_debug_start();

    // Register our custom RSP overlays into the RSP queue engine
    RSP_OVL_ID = rspq_overlay_register(&rsp_video);
    RSP_AUDIO_OVL_ID = rspq_overlay_register(&rsp_audio);
    RSP_FM_OVL_ID = rspq_overlay_register(&rsp_fm);
    rsp_audio_selftest();

    audio_init(audiofreq, AI_NUM_BUFFERS);
    // ORDER MATTERS: register the callback BEFORE the priming write. The AI
    // raises its interrupt when it LATCHES a buffer (start of DMA); whoever
    // services that interrupt must queue the next buffer right then or the
    // chain dies and the interrupt never fires again. The primer's own latch
    // IRQ can arrive within microseconds of audio_write_end(), so if the
    // callback isn't installed yet, that IRQ finds nothing to queue and the
    // whole audio path is dead from boot (total silence).
    audio_set_buffer_callback(audio_ring_cb);
    // One write kick-starts the chain: audio_write_end() runs libdragon's
    // audio_callback synchronously, which (in callback mode) immediately
    // pulls two buffers through audio_ring_cb — the ring is empty so they
    // are clean silence — and hands them to the AI. Every latch IRQ after
    // that refills through the callback: self-sustaining forever.
    audio_write_begin();
    audio_write_end();
}

// --- Audio (libdragon Audio Interface) -------------------------------------
// Delivery is handled entirely by audio_ring_cb() in AI-interrupt context —
// see the ring comment block above. plat_audio_pump() only keeps the ring
// topped up; it never touches the AI queue, so a slow sound_gen_samples()
// call can no longer let the hardware run dry and replay a stale buffer.
// sound_gen_samples() is rate-agnostic (N samples == N/AUDIO_RATE seconds of
// Z80+YM2610 time), so music plays at correct pitch AND tempo regardless of
// video fps. When the Z80 is inactive it writes clean silence.
void plat_enable_audio(int enable) {
    audio_enabled = enable;
}

// Copy one generated buffer into the staging ring (stereo frames).
static void aring_push(const int16_t *src, int n) {
    uint32_t wr = aring_wr;
    for (int i = 0; i < n; i++) {
        uint32_t s = (wr + i) & (ARING_FRAMES - 1);
        aring[s * 2 + 0] = src[i * 2 + 0];
        aring[s * 2 + 1] = src[i * 2 + 1];
    }
    MEMORY_BARRIER();      // publish samples before advancing the index
    aring_wr = wr + n;
}

static int16_t stage[2048 * 2];


#ifdef MVS64_RSPWP
// Cross-pump output deferral (whole-pump offload).
// sound_gen_samples() now returns with its tail RSP chunks still in flight;
// publishing `stage` to the ring is deferred to the NEXT pump entry, so the
// RSP burst deficit drains for free during the inter-pump 68k/draw window
// instead of being paid as a blocking wait at pump end. The fill policy
// counts the pending buffer as staged lead (it is published before the ISR
// could ever need it), and a safety valve publishes immediately whenever
// less than one AI callback of PUBLISHED lead remains.
void YM2610_wp_finish(void);
static int wp_pending_n;       // frames generated into stage, not yet published
static void wp_publish(void) {
    extern uint32_t profile_snd;
    uint32_t t0;
    if (!wp_pending_n) return;
    t0 = TICKS_READ();
    YM2610_wp_finish();        // usually instant: the RSP had the whole window
    profile_snd += TICKS_DISTANCE(t0, TICKS_READ());
    aring_push(stage, wp_pending_n);
    wp_pending_n = 0;
}
#endif

void plat_audio_pump(void) {
    if (!audio_enabled) return;

#if defined(MVS64_RSPWP) && defined(MVS64_WP_DEATHTEST)
    // Revive-cycle gate rig, thrash edition (2026-08-30 permanent-loss
    // postmortem): 8 forced dead-latches ~10s apart starting at pass 3600
    // (~60s at speed), i.e. every kill lands <30s after the previous
    // revive. The old bookkeeping exhausted its 6-attempt budget with no
    // restore path and stayed dead for the session; the fixed bookkeeping
    // must ride the thrash (parole retries) and, once the kills stop,
    // return to sustained health (off=0, budget restored) by run end.
    {
        extern void YM2610_offload_testkill(void);
        static int dt_passes;
        dt_passes++;
        if (dt_passes >= 3600 && dt_passes <= 7800
            && (dt_passes - 3600) % 600 == 0)
            YM2610_offload_testkill();
    }
#endif

#ifdef MVS64_RSPWP
    // rspq lost-wakeup watchdog. The whole-pump audio offload issues bursts
    // of commands separated by idle gaps (~500 halt/wake edges per second),
    // which hits a race in the rspq kernel's going-idle path: the RSP halts
    // just as the CPU sets SIG_MORE, and stays halted with work pending
    // (observed three times as "RSP CRASH ... display_get wait loop timed
    // out", RSP halted at kernel PC 0x18 with SIG_MORE set — on both the
    // highpri and lowpri queues). Halted+SIG_MORE is legal only for the
    // few-cycle window inside libdragon's own wake sequence, so if it
    // persists across two pump calls (~30-70ms, still well under
    // display_get's 200ms panic), clear the halt: that resumes the kernel's
    // idle loop, which re-checks SIG_MORE and proceeds. A spurious kick in
    // the benign window is a no-op (the CPU's own clear-halt follows).
    {
        volatile uint32_t * const SP_STATUS_REG =
            (volatile uint32_t *) 0xA4040010;
        static int wedged_seen;
        uint32_t st = *SP_STATUS_REG;
        if ((st & 1u /*HALTED*/) && (st & (1u << 14) /*SIG_MORE*/)) {
            if (wedged_seen++) {
                *SP_STATUS_REG = 1u /*SP_WSTATUS_CLEAR_HALT*/;
                wedged_seen = 0;
                rspwp_wedge_kicks++;
                debugf("[RSPWP] rspq lost-wakeup kicked (%lu)\n",
                       (unsigned long) rspwp_wedge_kicks);
            }
        } else {
            wedged_seen = 0;
        }
    }
#endif
#ifdef MVS64_RSPWP
    // Publish the previous pump's deferred buffer first: its chunks have had
    // the whole inter-pump window to complete, so the blocking finish inside
    // is normally a no-op poll.
    wp_publish();
#endif
    const int buflen = audio_get_buffer_length();
    const int n = buflen <= 2048 ? buflen : 2048;
    // Ring headroom kept staged ahead of the ISR. Two buffers (~80ms @11kHz)
    // rides out main-loop scheduling jitter at 28-30fps; combined with the
    // <=2 buffers libdragon keeps latched in the AI pipeline, total latency
    // is ~160ms.
    const uint32_t TARGET_LEAD = 2u * (uint32_t)n;

    // Overload detection is read straight from the ring's silence-pad counter:
    // audio_ring_cb() only increments it when it truly had nothing to hand the
    // AI, so this is ground truth for "generation fell behind real time",
    // including drains that happen in the middle of one long
    // sound_gen_samples() call. sound_silent is set from the PREVIOUS pass's
    // streak so one slow frame isn't muted.
    static uint32_t last_pad;
    uint32_t pad_now = aring_pad;
    uint32_t starved = pad_now - last_pad;
    last_pad = pad_now;
    sound_silent = (underrun_streak >= 1);
    if (starved > 0) {
        if (underrun_streak < 1000) underrun_streak++;
    } else {
        underrun_streak = 0;
    }

    // Overload governor: in sustained starvation, generation is slower than
    // real time and the output is zeros anyway (sound_silent), so grinding
    // through more Z80/YM work per pass only steals frame budget from video —
    // a Z80-bound boot jingle can drop the machine to ~4fps this way.
    // Process at most ONE buffer per pass while overloaded; the machine slows
    // uniformly instead of audio starving video. (The starved frames are never
    // "repaid": the ISR already padded that time with silence, so we just
    // resume generating from now.)
    int pass_budget = (underrun_streak >= 1) ? 1 : AI_NUM_BUFFERS;

    int filled = 0;

    // Top the ring up toward TARGET_LEAD. The ISR consumes exactly n frames
    // per callback, so lead is always a multiple of n.
    extern uint32_t profile_snd;
    while (filled < pass_budget) {
        uint32_t lead = aring_wr - aring_rd;
#ifdef MVS64_RSPWP
        lead += (uint32_t)wp_pending_n;   // deferred buffer counts as staged
#endif
        if (lead + (uint32_t)n > TARGET_LEAD) break;   // topped up
#ifdef MVS64_RSPWP
        wp_publish();          // free the staging buffer before reusing it
#endif
        uint32_t snd_t0 = TICKS_READ();
        sound_gen_samples(stage, n);
        profile_snd += TICKS_DISTANCE(snd_t0, TICKS_READ());
#ifdef MVS64_RSPWP
        wp_pending_n = n;      // defer the publish to the next pump entry
#else
        aring_push(stage, n);
#endif
        filled++;
    }

    // Safety valve for a broken emulated AI (BizHawk-Mupen class): if the ring
    // reader (the AI interrupt) stops advancing while the ring sits full, the
    // fill loop above never runs again and the 68k<->Z80 handshake starves
    // (the game hangs waiting for a sound reply). Detect it with a wall-clock
    // accumulator that resets whenever the reader moves; on real hardware and
    // ares the reader is IRQ-driven and always advances, so this is inert
    // there. When it trips, advance the sound machine into a discard buffer
    // (handshake and timers stay alive; the audio is lost, but so is the
    // platform's AI).
    {
        static uint32_t last_t, last_rd;
        static uint64_t wall_acc;
        static int wall_due;
        uint32_t now = TICKS_READ();
        if (last_t == 0) last_t = now;
        uint32_t rd_now = aring_rd;
        if (rd_now != last_rd) {
            wall_due = 0;
            wall_acc = 0;
        } else {
            wall_acc += (uint64_t)TICKS_DISTANCE(last_t, now) * (uint32_t)audio_get_frequency();
            wall_due += wall_acc / TICKS_PER_SECOND;
            wall_acc %= TICKS_PER_SECOND;
            if (wall_due > 4 * n) wall_due = 4 * n;
        }
        last_t = now;
        last_rd = rd_now;
        if (filled == 0 && wall_due >= 2 * n) {
#ifdef MVS64_RSPWP
            wp_publish();      // free the staging buffer before reusing it
#endif
            uint32_t snd_t0 = TICKS_READ();
            sound_gen_samples(stage, n);
#ifdef MVS64_RSPWP
            // discard buffer: complete in-flight chunks before stage reuse,
            // but never publish (the platform's AI is broken here anyway)
            YM2610_wp_finish();
#endif
            profile_snd += TICKS_DISTANCE(snd_t0, TICKS_READ());
            wall_due -= n;
        }
    }

#ifdef MVS64_RSPWP
    // Safety valve: with less than one full AI callback of PUBLISHED lead,
    // the deferred buffer cannot wait for the next pump entry (a slow frame
    // would starve the ISR into a silence pad). Publish now — this pays the
    // residual RSP deficit exactly when we're already behind, which is the
    // old (pre-deferral) behavior.
    if (wp_pending_n && aring_wr - aring_rd < (uint32_t)n)
        wp_publish();
#endif

#if defined(MVS64_RSPWP) && defined(MVS64_RSPQ_WEDGETEST)
    // Fault-injection rig for the rspq highpri wedge (the 2026-09-23 hardware
    // crash; see patches/libdragon-rspq-highpri-wedge.patch). Every 300th pump
    // from pass 900 on, let this pump's audio burst drain, then raise a stale
    // SIG_HIGHPRI_REQUESTED: exactly the state the upstream highpri_begin race
    // leaves behind. (Injected while segments are still queued, their own
    // WRITE_STATUS would consume it: the first rig, 2026-09-23, stuck only 1
    // of 5 times.) The kernel then re-enters highpri at the empty end of the
    // stream and sleeps there with SIG_HIGHPRI_RUNNING set, starving lowpri.
    // Unpatched toolchain: the next lowpri wait times out into the crash
    // screen (reproduced in ares: the exact hardware signature, rspq.c:951,
    // STATUS 0x1403). Patched: the wait-loop watchdog recovers it and play
    // continues.
    {
        static uint32_t wt_passes, wt_injected;
        if (++wt_passes >= 900 && (wt_passes % 300) == 0) {
            rspq_highpri_sync();
            MEMORY_BARRIER();
            *SP_STATUS = SP_WSTATUS_SET_SIG4;   // = SET_SIG_HIGHPRI_REQUESTED
            MEMORY_BARRIER();
            wt_injected++;
            debugf("[WEDGETEST] stale HIGHPRI_REQUESTED injected (%lu)\n",
                   (unsigned long) wt_injected);
        }
    }
#endif
}

int plat_poll(void) {
	controller_scan();
    struct controller_data ckeys = get_keys_pressed();

    memset(keystate, 0, sizeof(keystate));

    if (ckeys.c[0].up)      { keystate[PLAT_KEY_P1_UP] = 1; }
    if (ckeys.c[0].down)    { keystate[PLAT_KEY_P1_DOWN] = 1; }
    if (ckeys.c[0].left)    { keystate[PLAT_KEY_P1_LEFT] = 1; }
    if (ckeys.c[0].right)   { keystate[PLAT_KEY_P1_RIGHT] = 1; }
    if (ckeys.c[0].A)       { keystate[PLAT_KEY_P1_A] = 1; }
    if (ckeys.c[0].B)       { keystate[PLAT_KEY_P1_B] = 1; }
    if (ckeys.c[0].C_down)  { keystate[PLAT_KEY_P1_C] = 1; }
    if (ckeys.c[0].C_right) { keystate[PLAT_KEY_P1_D] = 1; }
    if (ckeys.c[0].start)   { keystate[PLAT_KEY_P1_START] = 1; }

    return 1;
}

void plat_enable_video(int enable) {

}

void plat_save_screenshot(const char *fn) {

}

uint8_t *g_screen_ptr;
int g_screen_pitch;

// Display buffering. With 2 buffers, display_get blocks until the buffer on
// screen is released at the NEXT vblank, so at 35-45 fps the CPU idled ~7.5
// ms per busy frame on hardware - vsync quantization, not RDP
// load (RDP busy ~10 ms/frame). A third buffer removes that wait.
// Two buffers also fenced every frame: display_get could only return after
// the previous frame's RSP+RDP work was done, which is what made it safe for
// the next frame to reuse sprite-cache slots, the palette snapshot and
// PALETTE_RAM_EMU. With 3 buffers that fence is explicit: each frame ends
// with rdpq_detach_cb (show + count the frame done, under the DP interrupt)
// and plat_beginframe waits for that count before video_render touches any
// shared state. The CPU spends ~13 ms in the 68k/sound before it renders,
// so the previous frame's ~10 ms of RDP work is normally long finished.
// mvs64_display_buffers is a .data word: the 2-buffer twin is
// -DMVS64_DISPLAY_BUFFERS=2 (uses the plain rdpq_detach_show path).
#ifndef MVS64_DISPLAY_BUFFERS
#define MVS64_DISPLAY_BUFFERS 3
#endif
int mvs64_display_buffers __attribute__((section(".data"))) = MVS64_DISPLAY_BUFFERS;
static surface_t *cur_disp;
static uint32_t frames_issued;
static volatile uint32_t frames_done;
static void frame_done_cb(void *arg) {   // DP interrupt: RDP finished the frame
	display_show((surface_t *)arg);
	frames_done++;
}
static void plat_detach_show(void) {
	if (mvs64_display_buffers > 2) {
		frames_issued++;
		rdpq_detach_cb(frame_done_cb, cur_disp);
	} else {
		rdpq_detach_show();
	}
}

void plat_beginframe(void) {
    surface_t *rdp_disp = display_get();
    // Previous frame fence (see plat_detach_show): its RDP work, and so all
    // of its RSP commands, must be done before this frame reuses cache slots
    // and palette buffers. Trivially true in 2-buffer and draining builds.
    while ((int32_t)(frames_issued - frames_done) > 0) {}
    cur_disp = rdp_disp;

	g_screen_ptr = rdp_disp->buffer;
	g_screen_pitch = 320*2;

    rdpq_attach(rdp_disp, NULL);
	rdpq_set_scissor(0, 0, 320, 224);
}

void plat_endframe(void) {
	plat_detach_show();
}

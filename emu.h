#ifndef EMU_H
#define EMU_H

// Frameskipping mode:
//   0 - never frameskip, game might slowdown
//   1 - 30 FPS mode (draw one frame every two)
// Auto frameskip on N64 is a separate build knob: make ... FRAMESKIP=n
// (-DMVS64_FRAMESKIP=n) skips drawing up to n frames in a row when behind
// the VI clock; see emu_render() in emu.c.
#define CONFIG_FRAMESKIP_MODE            0

#include <stdint.h>
#include <stdbool.h>

// MVS64_QUIET: silence the per-frame debugf tracing (framef) for release
// builds. On N64 every debugf is an ISViewer/USB write (PI transactions):
// several lines per frame cost real frame time on hardware and flood
// emulator logs.
#ifdef MVS64_QUIET
#define framef(...) ((void)0)
#else
#define framef(...) debugf(__VA_ARGS__)
#endif

#define MVS_CLOCK        24000000
#define M68K_CLOCK_DIV    2
#define FPS        		  60

// Audio output sample rate (Hz). The AI plays at this rate and the YM2610 is
// generated for it (sound_neogeo.c keys cyc_budget off this), so the two MUST
// match — define it in one place. On N64, real-time YM2610 FM synthesis is
// the dominant audio cost and scales with the sample rate, so the N64 build
// defaults to a lower rate to keep the framerate up. Measured (samsho2, ares,
// with the Z80 idle-skip): 44100~5fps, 22050~6.5fps, 11025~13fps steady.
// 11025 keeps NeoGeo FM music clearly recognizable while preserving playable
// speed. Override with EXTRA_DEFINES=-DMVS64_AUDIO_RATE=N.
#ifndef MVS64_AUDIO_RATE
#ifdef N64
#define MVS64_AUDIO_RATE  11025
#else
#define MVS64_AUDIO_RATE  44100
#endif
#endif
#define FRAME_CLOCK       (MVS_CLOCK / FPS)
#define LINE_CLOCK        (FRAME_CLOCK / 264)
#define WATCHDOG_PERIOD   3244030

#define MAX_EVENTS 8

typedef uint32_t (*EmuEventCb)(void *cbarg);

typedef struct {
    int64_t clock;
    EmuEventCb cb;
    void *cbarg;
    bool current;
} EmuEvent;

int emu_add_event(int64_t clock, EmuEventCb cb, void *cbarg);
void emu_change_event(int event_id, int64_t clock);
int64_t emu_clock(void);
int64_t emu_clock_frame(void);
uint32_t emu_pc(void);

void emu_cpu_reset(void);
void emu_cpu_irq(int level, bool state);

// Per-frame diagnostic telemetry (emu_diag.c), instrumented N64 builds only.
#if defined(N64) && (defined(MVS64_PCPROF) || defined(M64K_TRACECRC) || \
    defined(MVS64_PERFCOUNT) || defined(MVS64_OPHIST) || defined(MVS64_IDLEPROBE))
#define EMU_DIAG 1
void emu_diag_frame(void);
#endif

#endif

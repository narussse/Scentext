/*
 * audio_status.c
 */
#include "audio_status.h"

#define AUDIO_RESULT_DISPLAY_SAMPLES 25 /* RECOGNITION_DISPLAY_SEC=5.0s、200msサイクル(gas_recognition.cと同じ値) */

static volatile int g_state = AUDIO_STATUS_IDLE;
static volatile int g_percent = 0;
static volatile int g_display_samples = 0;

void audio_status_set_analyzing(void) {
    g_state = AUDIO_STATUS_ANALYZING;
    g_percent = 0;
    g_display_samples = 0;
}

void audio_status_set_result(int is_spray, int percent) {
    g_state = is_spray ? AUDIO_STATUS_RESULT_SPRAY : AUDIO_STATUS_RESULT_NONE;
    g_percent = percent;
    g_display_samples = AUDIO_RESULT_DISPLAY_SAMPLES;
}

void audio_status_tick(void) {
    if (g_state == AUDIO_STATUS_RESULT_SPRAY || g_state == AUDIO_STATUS_RESULT_NONE) {
        if (g_display_samples > 0) {
            g_display_samples--;
            if (g_display_samples == 0) {
                g_state = AUDIO_STATUS_IDLE;
            }
        }
    }
}

void audio_status_get(int *out_state, int *out_percent, int *out_seconds) {
    *out_state = g_state;
    *out_percent = g_percent;
    *out_seconds = (g_display_samples + 4) / 5;
}

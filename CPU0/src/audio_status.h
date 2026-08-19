/*
 * audio_status.h
 *
 * CPU1のステータス行3段目(音響/スプレー音認識結果)用の状態を保持する。
 * PC側(gas_audio_realtime_plot_v8.py)のaudio_text_objと同じ考え方:
 *   IDLE          : 何も表示しない
 *   ANALYZING     : MFCC+NPU推論を実行中(結果が出るまで)
 *   RESULT_SPRAY / RESULT_NONE : 推論結果を一定時間(AUDIO_RESULT_DISPLAY_SAMPLES)だけ表示
 */
#ifndef AUDIO_STATUS_H
#define AUDIO_STATUS_H

typedef enum {
    AUDIO_STATUS_IDLE = 0,
    AUDIO_STATUS_ANALYZING = 1,
    AUDIO_STATUS_RESULT_SPRAY = 2,
    AUDIO_STATUS_RESULT_NONE = 3,
} audio_status_state_t;

/* run_audio_mfcc_process()を呼ぶ直前に1回呼ぶ */
void audio_status_set_analyzing(void);

/* audio_run_inference()の結果が出た直後に1回呼ぶ */
void audio_status_set_result(int is_spray, int percent);

/* 200msごとに呼び出す(task_gas)。結果表示の残り時間を進める */
void audio_status_tick(void);

/* out_secondsは表示用の残り秒数(切り上げ)。ANALYZING中は0のまま(表示は「解析中」固定文言) */
void audio_status_get(int *out_state, int *out_percent, int *out_seconds);

#endif /* AUDIO_STATUS_H */

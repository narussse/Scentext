#include "audio_task.h"
#include <tm/tmonitor.h>
#include "audio_mfcc.h"
#include "gas_recognition.h"
#include "audio_status.h"

/* app_main.c 側のFSPドライバ経由UART送信。CPU0では tm_printf の送信経路
 * (SCI8直叩き)を無効化しているため、書式化だけ tm_sprintf(ハードウェアに
 * 触れない)で行い、実際の送信はこちらに一本化する(SCI8の二重管理を避ける)。 */
extern void console_write(const char *str);

const int32_t FIXED_OFFSET = 0;
/* PDM/DMACの生スクラッチ用ピンポンバッファ(int32、HALF_SAMPLES=DMAC_CHUNK_SAMPLES=1024の
 * 小さいチャンク)。ハードウェア仕様上32bit固定のFIFOレジスタを読むため、この2本だけは
 * int16化できない。ここには「今書き込み中の直近チャンク」しか保持しない
 * (長時間の履歴はg_lookback_historyの方で持つ)。 */
int32_t sound_buffer_A[HALF_SAMPLES] __attribute__((aligned(32)));
int32_t sound_buffer_B[HALF_SAMPLES] __attribute__((aligned(32)));
volatile uint8_t g_active_buffer_id = 0;
volatile uint32_t g_pdm_callback_count = 0;
volatile uint32_t g_pdm_other_event_count = 0;
volatile uint8_t g_pdm_needs_restart = 0;

#define SIGN_EXTEND_20BIT(v) (int32_t)((((int32_t)(v)) << 12) >> 12)

/* 直近LOOKBACK_SECONDS秒分の音声を連続的に保持する循環リング(int16)。
 * my_pdm_loop_callback()が、sound_buffer_A/B(int32)の1チャンク分が埋まるたびに
 * int16へ変換してここへ追記する(sound_buffer_A/B自体は32bit固定のハードウェア
 * 転送単位に留め、長時間分の保持はこちらでint16化することでRAMを節約する)。 */
static int16_t g_lookback_history[LOOKBACK_TOTAL_SAMPLES] __attribute__((aligned(32)));
/* g_lookback_historyへの次回書き込み位置(循環)。ここが同時に「一番古いサンプルの位置」
 * (次に上書きされる場所)でもある。 */
static volatile uint32_t g_lookback_write_idx = 0;

/* ガス立上りトリガー(stage_preceding_audio)が使う、「凍結スナップショット」バッファ
 * (LOOKBACK_TOTAL_SAMPLES長)。g_lookback_historyは絶え間なく上書きされ続けるため、
 * ガスの確定待ち(数秒)やNPU推論中はここにコピーして内容を固定してから処理する。 */
static int16_t g_shared_lookback_audio[LOOKBACK_TOTAL_SAMPLES] __attribute__((aligned(32)));
/* 共有バッファが使用中(ガスの確定=PROCESS/DISCARDを待っている)かどうかの明示的なロック。
 * 理論上は起こらないはずだが、前回のSTAGEがPROCESS/DISCARDされる前に次のSTAGEが
 * 来た場合に上書きしないための保険。 */
static volatile uint8_t g_staging_locked = 0;

/* sound_buffer_A/B(int32、HALF_SAMPLES長)1本分をint16化し、g_lookback_history
 * (循環リング)へ追記する。my_pdm_loop_callback()から、DMACが1チャンク分の転送を
 * 完了させるたびに(ISRコンテキストで)呼ばれる。HALF_SAMPLES=1024程度の単純なループ
 * なので、1回あたりの実行時間は数us程度でごく短い。 */
static void append_chunk_to_lookback_history(const int32_t *chunk) {
    uint32_t idx = g_lookback_write_idx;
    for (uint32_t i = 0; i < HALF_SAMPLES; i++) {
        int32_t val = SIGN_EXTEND_20BIT(chunk[i]) - FIXED_OFFSET;
        if (val > 32767)  val = 32767;
        if (val < -32768) val = -32768;
        g_lookback_history[idx] = (int16_t)val;
        idx = (idx + 1) % LOOKBACK_TOTAL_SAMPLES;
    }
    g_lookback_write_idx = idx;
}

/* g_lookback_history(循環リング)の直近LOOKBACK_TOTAL_SAMPLES分を、時系列順
 * (古い→新しい)に並べ直してdest(LOOKBACK_TOTAL_SAMPLES長)へコピーする。
 * stage_preceding_audio()が使う。 */
static void snapshot_lookback_history(int16_t *dest) {
    uint32_t start = g_lookback_write_idx; /* 一番古いサンプルの位置 */
    for (uint32_t i = 0; i < LOOKBACK_TOTAL_SAMPLES; i++) {
        uint32_t src = (start + i) % LOOKBACK_TOTAL_SAMPLES;
        dest[i] = g_lookback_history[src];
    }
}

void my_pdm_loop_callback(pdm_callback_args_t *p_args) {
    if (p_args->event == PDM_EVENT_DATA) {
        g_pdm_callback_count++;

        /* 直前まで書き込まれていた方(まだreconfigureする前のg_active_buffer_idが
         * 指す側)が「今完了したチャンク」。これをint16化して履歴リングに追記する。 */
        {
            const int32_t *just_completed = (g_active_buffer_id == 0) ? sound_buffer_A : sound_buffer_B;
            append_chunk_to_lookback_history(just_completed);
        }

        transfer_info_t new_transfer_info = *(g_transfer0.p_cfg->p_info);
        if (g_active_buffer_id == 0) {
            new_transfer_info.p_dest = (void *)sound_buffer_B;
            g_transfer0.p_api->reconfigure(g_transfer0.p_ctrl, &new_transfer_info);
            g_active_buffer_id = 1;
        } else {
            new_transfer_info.p_dest = (void *)sound_buffer_A;
            g_transfer0.p_api->reconfigure(g_transfer0.p_ctrl, &new_transfer_info);
            g_active_buffer_id = 0;
        }
    } else {
        g_pdm_other_event_count++;
        if (p_args->event == PDM_EVENT_ERROR) {
            // 割り込みの中でR_PDM_Stop/Startを呼ぶのは重いので、
            // ここではフラグだけ立てて、実際の再起動はタスクのメインループで行う
            g_pdm_needs_restart = 1;
        }
    }
}

void start_microphone_recording(void) {
    R_IOPORT_PinCfg(&g_ioport_ctrl, IOPORT_PORT_08_PIN_12, IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_PDM);
    R_IOPORT_PinCfg(&g_ioport_ctrl, IOPORT_PORT_05_PIN_02, IOPORT_CFG_PERIPHERAL_PIN | IOPORT_PERIPHERAL_PDM);
}

/*
 * lookback_buf(lookback_samples長、REC_SECONDS秒単位とは限らない)を、
 * モデルの入力窓(TOTAL_SAMPLES=REC_SECONDS秒)単位で均等にK分割し
 * (K=ceil(lookback_samples/TOTAL_SAMPLES))、それぞれ推論にかけて
 * 結果をOR判定でまとめる(1つでも陽性なら全体を陽性とする)。
 *
 * 窓の開始位置は、先頭窓が必ずbuf[0]から、末尾窓が必ずbufの最後の
 * TOTAL_SAMPLES分をちょうどカバーするように(K-1)等分の位置に配置するため、
 * 取りこぼしなくlookback_samples全体をカバーできる。
 * 例: LOOKBACK_SECONDS=8(REC_SECONDS=5)ならK=2(先頭5秒+末尾5秒、2秒分重複)、
 *     12秒ならK=3、REC_SECONDS以下ならK=1(分割なし、従来通り1回推論)。
 *
 * 呼び出し前にaudio_status_set_analyzing()を呼んでおくこと。
 * 最終結果(最も高かった確率、いずれかの窓が陽性か)はaudio_status_set_result()
 * に反映する。
 */
static void run_audio_mfcc_multiwindow(int16_t *lookback_buf, uint32_t lookback_samples) {
    uint32_t win_count = (lookback_samples + TOTAL_SAMPLES - 1) / TOTAL_SAMPLES;
    if (win_count < 1) {
        win_count = 1;
    }

    float best_probability = 0.0f;
    uint8_t any_spray = 0;

    for (uint32_t w = 0; w < win_count; w++) {
        uint32_t offset = 0;
        if (win_count > 1 && lookback_samples > TOTAL_SAMPLES) {
            offset = (uint32_t)(((uint64_t)(lookback_samples - TOTAL_SAMPLES) * w) / (win_count - 1));
        }

        {
            char buf[80];
            tm_sprintf((UB*)buf, (UB*)"[AUDIO] Window %d/%d (offset %d samples)...\r\n",
                      (int)(w + 1), (int)win_count, (int)offset);
            console_write(buf);
        }

        float probability = run_audio_mfcc_process(lookback_buf + offset);
        if (probability > best_probability) {
            best_probability = probability;
        }
        if (probability > 0.5f) {
            any_spray = 1;
        }
    }

    {
        char buf[96];
        tm_sprintf((UB*)buf, (UB*)"[RESULT] Combined (%d window(s)): %s (best %d%%)\r\n",
                  (int)win_count, any_spray ? "SPRAY DETECTED" : "no spray",
                  (int)(best_probability * 100.0f));
        console_write(buf);
    }

    audio_status_set_result(any_spray, (int)(best_probability * 100.0f));
}

/*
 * ガス立上り検知トリガー用(ステージ1): 「呼び出した瞬間」までの直近LOOKBACK_SECONDS秒分の
 * 音声を、g_lookback_history(循環リング、常時更新されている)からg_shared_lookback_audio
 * (凍結スナップショット)へコピーするだけ。MFCC/推論はまだ行わない
 * (ガスの推論結果が確定してから、別途 process_staged_gas_audio() を呼ぶ)。
 */
static void stage_preceding_audio(void) {
    if (g_staging_locked) {
        /* 理論上は起こらないはずだが、念のための保険。前回の保留分がまだ
         * 確定(PROCESS/DISCARD)されていない場合は、上書きせずに諦める。 */
        console_write("[GAS TRIGGER] WARNING: staging buffer still locked. Skipping new stage request.\r\n");
        return;
    }

    snapshot_lookback_history(g_shared_lookback_audio);

    g_staging_locked = 1;
    {
        char buf[96];
        tm_sprintf((UB*)buf, (UB*)"[GAS TRIGGER] Staged the preceding %d seconds of audio. Waiting for gas confirmation...\r\n", LOOKBACK_SECONDS);
        console_write(buf);
    }
}

/*
 * ガス立上り検知トリガー用(ステージ2): ガス推論でアルコールと確定した時点で
 * 呼ばれる。保留バッファに対してMFCC/NPU推論を(複数窓に分割して)実行し、
 * ロックを解除する。
 */
static void process_staged_gas_audio(void) {
    {
        char buf[96];
        tm_sprintf((UB*)buf, (UB*)"<<< GAS-CONFIRMED ALCOHOL. Running MFCC on the staged %d seconds... >>>\r\n", LOOKBACK_SECONDS);
        console_write(buf);
    }
    audio_status_set_analyzing();
    run_audio_mfcc_multiwindow(g_shared_lookback_audio, LOOKBACK_TOTAL_SAMPLES);
    g_staging_locked = 0;
}

/*
 * ガス立上り検知トリガー用(ステージ2の代わり): ガス推論で非アルコールと
 * 確定した時点で呼ばれる。保留バッファは使わず、ロックだけ解除する。
 */
static void discard_staged_gas_audio(void) {
    console_write("[GAS TRIGGER] Gas confirmed NOT alcohol. Discarding staged audio.\r\n");
    g_staging_locked = 0;
}

void task_audio_main(INT stacd, void *exinf) {
    R_PDM_Open(&g_pdm0_ctrl, &g_pdm0_cfg);
    transfer_info_t *p_dmac_info = (transfer_info_t *)g_transfer0.p_cfg->p_info;
    p_dmac_info->transfer_settings_word_b.mode = TRANSFER_MODE_NORMAL;
    ((pdm_instance_ctrl_t *)&g_pdm0_ctrl)->p_callback = my_pdm_loop_callback;

    g_active_buffer_id = 0;
    R_PDM_Start(&g_pdm0_ctrl, sound_buffer_A, sizeof(sound_buffer_A), HALF_SAMPLES);

    while(1) {
        if (g_pdm_needs_restart) {
            console_write("[PDM] Error event detected. Restarting microphone recording...\r\n");
            R_PDM_Stop(&g_pdm0_ctrl);
            g_active_buffer_id = 0;
            R_PDM_Start(&g_pdm0_ctrl, sound_buffer_A, sizeof(sound_buffer_A), HALF_SAMPLES);
            g_pdm_needs_restart = 0;
        }

        if (g_gas_audio_signal != GAS_AUDIO_SIGNAL_NONE) {
            gas_audio_signal_t signal = g_gas_audio_signal;
            g_gas_audio_signal = GAS_AUDIO_SIGNAL_NONE;

            switch (signal) {
                case GAS_AUDIO_SIGNAL_STAGE:
                    {
                        char buf[96];
                        tm_sprintf((UB*)buf, (UB*)"\r\n[GAS TRIGGER] Rising detected. Staging the preceding %d seconds of audio...\r\n", LOOKBACK_SECONDS);
                        console_write(buf);
                    }
                    stage_preceding_audio();
                    break;
                case GAS_AUDIO_SIGNAL_PROCESS:
                    process_staged_gas_audio();
                    break;
                case GAS_AUDIO_SIGNAL_DISCARD:
                    discard_staged_gas_audio();
                    break;
                default:
                    break;
            }
        }

        tk_dly_tsk(10);
    }
}

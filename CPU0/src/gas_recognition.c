/*
 * gas_recognition.c
 *
 * ガス4チャンネル(NO2/C2H5CH/VOC/CO)の立上り検知とアルコール認識。
 *
 * 200ms周期(gas_sensor.cのCYCLE_MS)の固定サンプリング前提で、
 * PC側(gas_audio_realtime_plot_v6.py / train_gas_recognition_model.py)と
 * 同じ考え方をサンプル数ベースのリングバッファで実装している:
 *
 *   WIN_SEC=2.5s        -> GAS_SLOPE_LOOKBACK_SAMPLES=13 (2.6s、丸め誤差はごく僅か)
 *   RISE_THRESH=4.0/s   -> GAS_RISE_THRESH
 *   RISE_CONFIRM_SEC=0.6s -> GAS_RISE_CONFIRM_SAMPLES=3
 *   EXIT_CONFIRM_SEC=5.0s -> GAS_EXIT_CONFIRM_SAMPLES=25
 *   BASE_WINDOW=10.0s   -> GAS_BASE_SAMPLES=50 (gas_recognition_model.hのGAS_MODEL_BASE_WINDOW_SECと一致させること)
 *   POST_WINDOW=5.0s    -> GAS_POST_SAMPLES=25 (gas_recognition_model.hのGAS_MODEL_POST_WINDOW_SECと一致させること)
 *
 * 「立上り」検知(4チャンネルのどれかがRISINGになった瞬間)をイベント開始(onset)
 * とし、そこから POST_WINDOW 秒間のデータでピーク値・ピーク時刻を求めて
 * 特徴量を計算し、gas_recognition_model.h の判定式(logit = weights・x + bias)
 * でアルコールか否かを判定する。
 *
 * アルコールと判定された場合のみ、onsetから合計10秒間(GAS_COOLDOWN_SAMPLES)は
 * 新しい立上りがあっても推論を開始しないクールダウンに入る。推論自体に
 * POST_WINDOW=5秒かかっているので、推論完了時点でさらに5秒
 * (GAS_COOLDOWN_SAMPLES - GAS_POST_SAMPLES)のクールダウンを設定することで、
 * 「onsetから合計10秒間」を実現している。
 * 非アルコールと判定された場合はクールダウンを設けず、すぐに次の立上りを
 * 検知できるようにしている。
 *
 * ※ センサーへの物理的な衝撃(触れる等)は学習データにない未知の外乱のため、
 *   引き続き誤認識のリスクがある(運用で回避すること。詳細は
 *   train_gas_recognition_model.py のコメントを参照)。
 */
#include <math.h>
#include <tm/tmonitor.h>
#include "gas_recognition.h"
#include "gas_recognition_model.h"

/* app_main.c 側のFSPドライバ経由UART送信。CPU0では tm_printf の送信経路
 * (SCI8直叩き)を無効化しているため、書式化だけ tm_sprintf(ハードウェアに
 * 触れない)で行い、実際の送信はこちらに一本化する(SCI8の二重管理を避ける)。 */
extern void console_write(const char *str);

/* ==========================================
 * パラメータ (PC側と同じ値。変更する場合は両方揃えること)
 * ========================================== */
#define GAS_CYCLE_SEC 0.2f

#define GAS_SLOPE_LOOKBACK_SAMPLES 13   /* WIN_SEC=2.5s相当 (実際は2.6s) */
#define GAS_RISE_THRESH 4.0f             /* count/s (NO2/C2H5CH/VOCの既定値) */
#define GAS_RISE_THRESH_CO 6.0f          /* COは立上り検出が過敏(誤検知しやすい)ため、閾値を上げて抑える */
#define GAS_RISE_CONFIRM_SAMPLES 3       /* RISE_CONFIRM_SEC=0.6s */
#define GAS_EXIT_CONFIRM_SAMPLES 25      /* EXIT_CONFIRM_SEC=5.0s */

#define GAS_BASE_SAMPLES 50              /* BASE_WINDOW=10.0s */
#define GAS_POST_SAMPLES 25              /* POST_WINDOW=5.0s (GAS_MODEL_POST_WINDOW_SECと一致させること) */
#define GAS_COOLDOWN_SAMPLES 50          /* onsetから合計10.0秒のクールダウン */
#define GAS_RESULT_DISPLAY_SAMPLES 25    /* RECOGNITION_DISPLAY_SEC=5.0s (PC側と同じ、ALCOHOL確定時用) */
#define GAS_RESULT_DISPLAY_SAMPLES_CLEAR 10 /* NOT_ALCOHOL(誤検知)時は表示を短くして早くREADYに戻す(2.0s) */

typedef enum {
    GAS_CH_NO2 = 0,
    GAS_CH_C2H5CH = 1,
    GAS_CH_VOC = 2,
    GAS_CH_CO = 3,
    GAS_CH_COUNT = 4,
} gas_channel_id_t;

typedef enum {
    GAS_STATE_NOT_RISING = 0,
    GAS_STATE_RISING = 1,
} gas_rise_state_t;

/* --- 傾き計算用のリングバッファ + 立上り状態機械 (チャンネルごと) --- */
typedef struct {
    int32_t buf[GAS_SLOPE_LOOKBACK_SAMPLES];
    int buf_count;
    int buf_idx;
    float slope;
    gas_rise_state_t state;
    int rising_count;
    int below_count;
} gas_channel_state_t;

/* --- ベースライン計算用のリングバッファ (チャンネルごと) --- */
typedef struct {
    int32_t buf[GAS_BASE_SAMPLES];
    int buf_count;
    int buf_idx;
} gas_baseline_buf_t;

static gas_channel_state_t g_channel_state[GAS_CH_COUNT];
static gas_baseline_buf_t g_baseline_buf[GAS_CH_COUNT];

/* --- 推論(特徴量収集)の状態 --- */
static int g_collecting = 0;
static int g_collect_sample_count = 0;
static float g_collect_baseline[GAS_CH_COUNT];
static float g_peak_rise[GAS_CH_COUNT];
static int g_peak_time_samples[GAS_CH_COUNT];
static int g_cooldown_remaining_samples = 0;
static int g_prev_ready = 1;

/* CPU1のステータス行2段目("解析中..."等)用の状態。gas_recognition_get_line2()経由で
 * app_main.cのtask_gasが読み出し、g_gas_ipcに書き込む。 */
static int g_line2_state = GAS_LINE2_IDLE;
static int g_line2_display_samples = 0; /* RESULT_ALCOHOL/RESULT_CLEAR表示中の残りサンプル数 */

/* --- センサーのヒーター立上り(ウォームアップ)判定 ---
 * 実測ログ(dataset/gas/heating/*.csv)で、NO2チャンネルが他チャンネルより早く
 * スロープ0付近に収束することを確認済み。NO2のスロープの絶対値が
 * GAS_WARMUP_SLOPE_THRESH未満の状態が(リーキーバケット方式で)
 * GAS_WARMUP_CONFIRM_SAMPLES分蓄積したら「ヒーター安定」と判定する。
 * 保険用のタイムアウトは設けていない。
 *
 * 理論上の最短解除時間は「スロープ計算に必要な最小データがそろうまでの時間
 * (2.6秒、GAS_SLOPE_LOOKBACK_SAMPLES分)+ここでの蓄積時間」で決まる。
 * 書込み時点で既にセンサーが十分温まっていて条件の良いケースなら最短60秒で
 * 解除できるよう、蓄積時間を57.4秒(287サンプル)に設定。その分だけ閾値を
 * 1.0に絞ることで、ノイズの多い個体・真の冷えきり状態では自然と長め
 * になるようにしてある(実測6ログでの解除時間: 約60〜305秒、良好な個体ほど
 * 60秒に近く、ノイジーな個体・真の冷えきり状態ほど長くなる)。 */
#define GAS_WARMUP_SLOPE_THRESH 1.0f       /* count/s */
#define GAS_WARMUP_CONFIRM_SAMPLES 287     /* 57.4s連続 (200ms周期 * 287) */

static int g_warmup_done = 0;
static int g_warmup_low_slope_count = 0;

int gas_recognition_is_warming_up(void) {
    return !g_warmup_done;
}

#define GAS_DETECT_FLASH_SAMPLES 5 /* 検知直後1.0秒間だけ「立上り検出！」を表示する */

void gas_recognition_get_line2(int *out_state, int *out_seconds) {
    if (g_line2_state == GAS_LINE2_COLLECTING) {
        int remaining = GAS_POST_SAMPLES - g_collect_sample_count;
        if (remaining < 0) {
            remaining = 0;
        }
        if (g_collect_sample_count < GAS_DETECT_FLASH_SAMPLES) {
            /* 検知した直後のごく短い間は「解析中」ではなく「立上り検出！」を表示する */
            *out_state = GAS_LINE2_RISING_DETECTED;
            *out_seconds = 0;
        } else {
            *out_state = GAS_LINE2_COLLECTING;
            *out_seconds = (remaining + 4) / 5; /* GAS_CYCLE_SEC=0.2s -> 5サンプル/秒、切り上げ */
        }
    } else if (g_line2_state == GAS_LINE2_RESULT_ALCOHOL || g_line2_state == GAS_LINE2_RESULT_CLEAR) {
        *out_state = g_line2_state;
        *out_seconds = (g_line2_display_samples + 4) / 5;
    } else {
        *out_state = g_line2_state;
        *out_seconds = 0;
    }
}

/* audio_task.c との連携用シグナル (詳細は gas_recognition.h 参照) */
volatile gas_audio_signal_t g_gas_audio_signal = GAS_AUDIO_SIGNAL_NONE;

/* CPU1(表示)側へ共有メモリ経由で伝える「いずれかのチャンネルが立上り中か」の状態。
 * app_main.cのtask_gasがgas_recognition_is_any_rising()経由で読み出し、
 * g_gas_ipc.any_risingに書き込む。 */
static int g_any_rising_state = 0;

int gas_recognition_is_any_rising(void) {
    return g_any_rising_state;
}

/* ==========================================
 * 傾き計算・立上り状態機械の更新 (1チャンネル分)
 * ========================================== */
static gas_rise_state_t gas_channel_update(gas_channel_state_t *cs, int32_t value, float rise_thresh) {
    int have_full_window = (cs->buf_count >= GAS_SLOPE_LOOKBACK_SAMPLES);
    int32_t oldest = cs->buf[cs->buf_idx]; /* 上書きされる直前の、一番古いサンプル */

    cs->buf[cs->buf_idx] = value;
    cs->buf_idx = (cs->buf_idx + 1) % GAS_SLOPE_LOOKBACK_SAMPLES;
    if (cs->buf_count < GAS_SLOPE_LOOKBACK_SAMPLES) {
        cs->buf_count++;
    }

    if (!have_full_window) {
        /* データが溜まるまではスロープ計算をスキップ(PC側と同じ) */
        return cs->state;
    }

    float window_sec = GAS_SLOPE_LOOKBACK_SAMPLES * GAS_CYCLE_SEC;
    cs->slope = ((float)value - (float)oldest) / window_sec;

    if (cs->slope > rise_thresh) {
        cs->below_count = 0;
        cs->rising_count++;
        if (cs->rising_count >= GAS_RISE_CONFIRM_SAMPLES) {
            cs->state = GAS_STATE_RISING;
        }
    } else {
        cs->rising_count = 0;
        cs->below_count++;
        if (cs->below_count >= GAS_EXIT_CONFIRM_SAMPLES) {
            cs->state = GAS_STATE_NOT_RISING;
        }
    }
    return cs->state;
}

/* ==========================================
 * ベースライン用リングバッファ
 * ========================================== */
static void gas_baseline_push(gas_baseline_buf_t *bb, int32_t value) {
    bb->buf[bb->buf_idx] = value;
    bb->buf_idx = (bb->buf_idx + 1) % GAS_BASE_SAMPLES;
    if (bb->buf_count < GAS_BASE_SAMPLES) {
        bb->buf_count++;
    }
}

static float gas_baseline_mean(const gas_baseline_buf_t *bb) {
    if (bb->buf_count == 0) {
        return 0.0f;
    }
    int64_t sum = 0;
    for (int i = 0; i < bb->buf_count; i++) {
        sum += bb->buf[i];
    }
    return (float)sum / (float)bb->buf_count;
}

/* tm_printfの簡易printfは%fを扱えないため、整数部と小数部3桁に分けて表示する。
 * 符号は呼び出し側で別途"-"を付けるので、ここでは絶対値を返す
 * (そのままだと負の値で符号が2重("--2.500"のように)になってしまうため)。 */
static int gas_float_int_part(float f) {
    int i = (int)f;
    return (i < 0) ? -i : i;
}
static int gas_float_frac3(float f) {
    float diff = f - (float)((int)f);
    if (diff < 0.0f) {
        diff = -diff;
    }
    return (int)(diff * 1000.0f);
}

/* ==========================================
 * 推論の実行 (POST_WINDOW分のデータが集まった時点で呼ばれる)
 * ========================================== */
static void gas_run_inference(void) {
    float total = g_peak_rise[GAS_CH_NO2] + g_peak_rise[GAS_CH_C2H5CH]
                + g_peak_rise[GAS_CH_VOC] + g_peak_rise[GAS_CH_CO];

    float x[GAS_MODEL_N_FEATURES];
    x[0] = (total > 0.0f) ? (g_peak_rise[GAS_CH_NO2] / total) : 0.0f;
    x[1] = (total > 0.0f) ? (g_peak_rise[GAS_CH_C2H5CH] / total) : 0.0f;
    x[2] = (total > 0.0f) ? (g_peak_rise[GAS_CH_VOC] / total) : 0.0f;
    x[3] = (float)(g_peak_time_samples[GAS_CH_NO2] - g_peak_time_samples[GAS_CH_CO]) * GAS_CYCLE_SEC;
    x[4] = (float)(g_peak_time_samples[GAS_CH_C2H5CH] - g_peak_time_samples[GAS_CH_CO]) * GAS_CYCLE_SEC;
    x[5] = (float)(g_peak_time_samples[GAS_CH_VOC] - g_peak_time_samples[GAS_CH_CO]) * GAS_CYCLE_SEC;
    x[6] = logf(1.0f + ((total > 0.0f) ? total : 0.0f));

    float logit = gas_model_logit(x);
    int is_alcohol = gas_model_is_alcohol(x);

    {
        char buf[96];
        tm_sprintf((UB*)buf, (UB*)"[GAS RECOGNITION] logit=%s%d.%03d -> %s\r\n",
                  (logit < 0.0f) ? "-" : "",
                  gas_float_int_part(logit), gas_float_frac3(logit),
                  is_alcohol ? "ALCOHOL" : "NOT_ALCOHOL");
        console_write(buf);
    }

    g_line2_state = is_alcohol ? GAS_LINE2_RESULT_ALCOHOL : GAS_LINE2_RESULT_CLEAR;
    g_line2_display_samples = is_alcohol ? GAS_RESULT_DISPLAY_SAMPLES : GAS_RESULT_DISPLAY_SAMPLES_CLEAR;

    if (is_alcohol) {
        /* onsetから合計10秒(GAS_COOLDOWN_SAMPLES)になるよう、
         * 推論に使った POST_WINDOW 分を差し引いた残りをクールダウンにする */
        g_cooldown_remaining_samples = GAS_COOLDOWN_SAMPLES - GAS_POST_SAMPLES;
        char buf[96];
        tm_sprintf((UB*)buf, (UB*)"[GAS RECOGNITION] Cooldown started (%d.%01ds remaining, alcohol only).\r\n",
                  (int)(g_cooldown_remaining_samples * GAS_CYCLE_SEC),
                  (int)((g_cooldown_remaining_samples * GAS_CYCLE_SEC
                         - (int)(g_cooldown_remaining_samples * GAS_CYCLE_SEC)) * 10.0f));
        console_write(buf);

        /* アルコール確定 -> 音声側に保留バッファへのMFCC/NPU推論実行を指示する */
        g_gas_audio_signal = GAS_AUDIO_SIGNAL_PROCESS;
    } else {
        /* 非アルコール確定 -> 音声側に保留バッファの破棄(ロック解除)を指示する */
        g_gas_audio_signal = GAS_AUDIO_SIGNAL_DISCARD;
    }
}

/* ==========================================
 * 公開関数: 200msごとにガス4値を渡して呼び出す
 * ========================================== */
void gas_recognition_update(int32_t no2, int32_t c2h5ch, int32_t voc, int32_t co) {
    int32_t values[GAS_CH_COUNT] = { no2, c2h5ch, voc, co };
    static const float rise_thresh_per_ch[GAS_CH_COUNT] = {
        GAS_RISE_THRESH,     /* NO2 */
        GAS_RISE_THRESH,     /* C2H5CH */
        GAS_RISE_THRESH,     /* VOC */
        GAS_RISE_THRESH_CO,  /* CO (過敏なので閾値高め) */
    };

    /* 単独チャンネルの誤検知(特にCOが過敏)に引きずられないよう、
     * 「同時に立上り中のチャンネル数が2以上」の場合だけ「立上り」とみなす。 */
    int rising_count = 0;
    for (int ch = 0; ch < GAS_CH_COUNT; ch++) {
        gas_rise_state_t st = gas_channel_update(&g_channel_state[ch], values[ch], rise_thresh_per_ch[ch]);
        if (st == GAS_STATE_RISING) {
            rising_count++;
        }
    }
    int any_rising = (rising_count >= 2);
    int ready = !any_rising;
    g_any_rising_state = any_rising;

    /* ヒーターのウォームアップ判定(NO2チャンネルのスロープを利用)。
     * gas_channel_update()を抜けた直後なので、この時点のg_channel_state[NO2].slope
     * が最新値になっている。
     *
     * リーキーバケット方式: 閾値未満のサンプルで+1、閾値以上のサンプルで-1
     * (0未満にはしない)。単発の閾値越え(ノイズ・量子化起因の揺れ)で
     * カウントを完全に0へリセットしてしまうと、閾値付近で小刻みに揺れる
     * 個体でいつまで経っても解除されない事例が実測で見つかったため、
     * 「完全リセット」ではなく「1つ分だけ後退」に変更した。 */
    if (!g_warmup_done) {
        float no2_slope = g_channel_state[GAS_CH_NO2].slope;
        if (fabsf(no2_slope) < GAS_WARMUP_SLOPE_THRESH) {
            g_warmup_low_slope_count++;
        } else if (g_warmup_low_slope_count > 0) {
            g_warmup_low_slope_count--;
        }
        if (g_warmup_low_slope_count >= GAS_WARMUP_CONFIRM_SAMPLES) {
            g_warmup_done = 1;
        }
    }

    for (int ch = 0; ch < GAS_CH_COUNT; ch++) {
        gas_baseline_push(&g_baseline_buf[ch], values[ch]);
    }

    if (g_cooldown_remaining_samples > 0) {
        g_cooldown_remaining_samples--;
    }

    if (!g_collecting) {
        if (!ready && g_prev_ready && g_cooldown_remaining_samples == 0 && g_warmup_done) {
            g_collecting = 1;
            g_collect_sample_count = 0;
            g_line2_state = GAS_LINE2_COLLECTING;
            for (int ch = 0; ch < GAS_CH_COUNT; ch++) {
                g_collect_baseline[ch] = gas_baseline_mean(&g_baseline_buf[ch]);
                g_peak_rise[ch] = -1.0e18f;
                g_peak_time_samples[ch] = 0;
            }
            {
                char buf[96];
                tm_sprintf((UB*)buf, (UB*)"[GAS RECOGNITION] New rising event detected. Collecting for %d.%01ds...\r\n",
                          (int)(GAS_POST_SAMPLES * GAS_CYCLE_SEC),
                          (int)((GAS_POST_SAMPLES * GAS_CYCLE_SEC - (int)(GAS_POST_SAMPLES * GAS_CYCLE_SEC)) * 10.0f));
                console_write(buf);
            }

            /* 検知した瞬間に音声側へ通知(ガス推論の結果を待たない)。
             * 音声側はこの瞬間までの直近5秒を「保留バッファ」に抽出するだけで、
             * MFCC/NPU推論はガスの確定(PROCESS/DISCARD)まで行わない。 */
            g_gas_audio_signal = GAS_AUDIO_SIGNAL_STAGE;
        }
    }

    if (g_collecting) {
        for (int ch = 0; ch < GAS_CH_COUNT; ch++) {
            float rise = (float)values[ch] - g_collect_baseline[ch];
            if (rise > g_peak_rise[ch]) {
                g_peak_rise[ch] = rise;
                g_peak_time_samples[ch] = g_collect_sample_count;
            }
        }
        g_collect_sample_count++;
        if (g_collect_sample_count >= GAS_POST_SAMPLES) {
            gas_run_inference();
            g_collecting = 0;
        }
    }

    if (!g_collecting && g_line2_display_samples > 0) {
        g_line2_display_samples--;
        if (g_line2_display_samples == 0) {
            g_line2_state = GAS_LINE2_IDLE;
        }
    }

    g_prev_ready = ready;
}

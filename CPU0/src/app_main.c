/*
 * app_main.c
 *
 * μT-Kernel 3.0 Phase 2b-ii: ガスセンサー(I2C)読み取り+ガス連動認識(task_4相当)、
 * 音声取り込み+MFCC+NPU推論(task_audio相当)の2タスク構成。
 * 表示はCPU1が担当するため、task_5(display)・task_mfcc(デッドスタブ)は
 * 参照プロジェクト(v3)から意図的に含めていない。
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "hal_data.h"
#include "gas_sensor.h"
#include "gas_recognition.h"
#include "audio_task.h"
#include "audio_inference.h"
#include "audio_status.h"
#include "ipc_shared.h"

static volatile bool g_uart_tx_complete = false;

void console_uart_callback(uart_callback_args_t *p_args)
{
    if (p_args->event == UART_EVENT_TX_COMPLETE)
    {
        g_uart_tx_complete = true;
    }
}

/* 文字列をUARTへ送信し、送信完了まで待つ(ブロッキング) */
void console_write(const char *str)
{
    uint32_t len = 0;
    while (str[len] != '\0') { len++; }
    if (len == 0)
    {
        return;
    }

    g_uart_tx_complete = false;
    fsp_err_t err = R_SCI_B_UART_Write(&g_uart0_ctrl, (uint8_t const *)str, len);
    if (err != FSP_SUCCESS)
    {
        return;
    }

    uint32_t timeout = 1000;
    while (!g_uart_tx_complete && timeout > 0)
    {
        R_BSP_SoftwareDelay(1, BSP_DELAY_UNITS_MILLISECONDS);
        timeout--;
    }
}

/* 符号なし32bit整数を10進文字列に変換する(標準ライブラリのitoaが無い環境向け) */
static void format_uint(uint32_t value, char *buf)
{
    char tmp[12];
    int len = 0;

    if (value == 0)
    {
        tmp[len++] = '0';
    }
    else
    {
        while (value > 0)
        {
            tmp[len++] = (char)('0' + (value % 10));
            value /= 10;
        }
    }

    int n = 0;
    for (int i = len - 1; i >= 0; i--)
    {
        buf[n++] = tmp[i];
    }
    buf[n] = '\0';
}

/* 既存の mtk3bsp2_ra8p1_ek_grove_pdm_glcdc の task_4_main と同じ形式
 * "[GAS] NO2: n | C2H5CH: n | VOC: n | CO: n" で1行出力する
 * (PC側の gas_audio_realtime_plot_v8.py がそのまま解析できるように)。 */
static void print_gas_line(uint32_t no2, uint32_t c2h5ch, uint32_t voc, uint32_t co)
{
    char line[96];
    char numbuf[12];
    uint32_t pos = 0;

    static const char prefix[] = "[GAS] NO2: ";
    for (uint32_t i = 0; prefix[i] != '\0'; i++) { line[pos++] = prefix[i]; }

    format_uint(no2, numbuf);
    for (uint32_t i = 0; numbuf[i] != '\0'; i++) { line[pos++] = numbuf[i]; }

    static const char sep1[] = " | C2H5CH: ";
    for (uint32_t i = 0; sep1[i] != '\0'; i++) { line[pos++] = sep1[i]; }

    format_uint(c2h5ch, numbuf);
    for (uint32_t i = 0; numbuf[i] != '\0'; i++) { line[pos++] = numbuf[i]; }

    static const char sep2[] = " | VOC: ";
    for (uint32_t i = 0; sep2[i] != '\0'; i++) { line[pos++] = sep2[i]; }

    format_uint(voc, numbuf);
    for (uint32_t i = 0; numbuf[i] != '\0'; i++) { line[pos++] = numbuf[i]; }

    static const char sep3[] = " | CO: ";
    for (uint32_t i = 0; sep3[i] != '\0'; i++) { line[pos++] = sep3[i]; }

    format_uint(co, numbuf);
    for (uint32_t i = 0; numbuf[i] != '\0'; i++) { line[pos++] = numbuf[i]; }

    line[pos++] = '\r';
    line[pos++] = '\n';
    line[pos] = '\0';

    console_write(line);
}

LOCAL void task_gas(INT stacd, void *exinf)
{
    fsp_err_t err = R_SCI_B_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);
    if (err != FSP_SUCCESS)
    {
        while (1) { tk_dly_tsk(1000); }
    }

    console_write("[CPU0] gas task (uT-Kernel task) started.\r\n");

    /* CPU1側の共有メモリ読み取りループが起動直後の不定値を拾わないよう、
     * ここで明示的にゼロクリアしてからループへ入る。 */
    g_gas_ipc.seq             = 0;
    g_gas_ipc.no2             = 0;
    g_gas_ipc.c2h5ch          = 0;
    g_gas_ipc.voc             = 0;
    g_gas_ipc.co              = 0;
    g_gas_ipc.any_rising      = 0;
    g_gas_ipc.gas_line2_state   = 0;
    g_gas_ipc.gas_line2_seconds = 0;
    g_gas_ipc.audio_state        = 0;
    g_gas_ipc.audio_percent      = 0;
    g_gas_ipc.audio_seconds      = 0;
    g_gas_ipc.warming_up         = 1;
    g_gas_ipc.wifi_scan_seq      = 0;

    gas_sensor_init();

    /* 前回の正常な値を記憶しておく（初期値は0）。gas_sensor.c本体と同じロジック。 */
    uint32_t last_no2 = 0, last_c2h5ch = 0, last_voc = 0, last_co = 0;

    /* CPU1(esp_notify.c)がWi-Fiスキャン(AT+CWLAP)結果を書き込むたびに
     * wifi_scan_seqが+1される。CPU1にはデバッグ用UARTが無いため、
     * 変化を検知したらこちらのコンソール(Tera Term)へ中継出力する。 */
    uint32_t last_wifi_scan_seq = 0;

    while (1)
    {
        uint32_t no2    = gas_sensor_read_NO2();
        uint32_t c2h5ch = gas_sensor_read_C2H5CH();
        uint32_t voc    = gas_sensor_read_VOC();
        uint32_t co     = gas_sensor_read_CO();

        if (no2 != 0)    { last_no2 = no2; }       else { no2 = last_no2; }
        if (c2h5ch != 0) { last_c2h5ch = c2h5ch; } else { c2h5ch = last_c2h5ch; }
        if (voc != 0)    { last_voc = voc; }       else { voc = last_voc; }
        if (co != 0)     { last_co = co; }         else { co = last_co; }

        print_gas_line(no2, c2h5ch, voc, co);

        /* 立上り検知 + アルコール認識(gas_recognition.c)。
         * 検知されるとg_gas_audio_signal経由でtask_audio(音声)に
         * 「直近5秒の音声をMFCC+NPU推論にかけろ」と伝える。 */
        gas_recognition_update((int32_t)no2, (int32_t)c2h5ch, (int32_t)voc, (int32_t)co);

        /* CPU1(GLCDC表示)へ共有メモリ経由で最新値を渡す。
         * seqは値を書き終えた最後に+1する(CPU1側は値を読んでからseqを見れば
         * 万一の書き込み最中データを掴んでも次周期には解消される)。 */
        g_gas_ipc.no2    = (int32_t)no2;
        g_gas_ipc.c2h5ch = (int32_t)c2h5ch;
        g_gas_ipc.voc    = (int32_t)voc;
        g_gas_ipc.co     = (int32_t)co;

        /* CPU1のステータス行2段目(ガス収集中/結果)・3段目(音響認識)。
         * 音響側の結果表示カウントダウンもここで一緒に進める
         * (task_audioとは別タスクなので、推論処理自体の実行時間には影響しない)。 */
        int l2_state = 0, l2_seconds = 0;
        gas_recognition_get_line2(&l2_state, &l2_seconds);
        g_gas_ipc.gas_line2_state   = (uint32_t)l2_state;
        g_gas_ipc.gas_line2_seconds = (uint32_t)l2_seconds;

        audio_status_tick();
        int a_state = 0, a_percent = 0, a_seconds = 0;
        audio_status_get(&a_state, &a_percent, &a_seconds);
        g_gas_ipc.audio_state   = (uint32_t)a_state;
        g_gas_ipc.audio_percent = (uint32_t)a_percent;
        g_gas_ipc.audio_seconds = (uint32_t)a_seconds;

        /* センサーのヒーターがまだウォームアップ中かどうか。CPU1側の
         * 「GAS SENSOR HEATING...」表示に使う。 */
        int warming_up = gas_recognition_is_warming_up();
        g_gas_ipc.warming_up = (uint32_t)warming_up;

        /* CPU1のREADY/WAIT表示は、物理的な立上り中(any_rising)だけでなく、
         * 2段目/3段目に何か文字が出ている間(収集中・結果表示中・音響解析中)、
         * および起動直後のセンサーウォームアップ中も、まとめてWAIT扱いにする。
         * そうしないと「READY」に戻った直後の画面に前のNOT ALCOHOL/ALCOHOL
         * DETECTED等がしばらく残ってしまい、切り替わりのタイミングがちぐはぐに
         * 見えるため。 */
        int busy = gas_recognition_is_any_rising()
                 || (l2_state != GAS_LINE2_IDLE)
                 || (a_state != AUDIO_STATUS_IDLE)
                 || warming_up;
        g_gas_ipc.any_rising = (uint32_t)busy;

        /* CPU1(esp_notify.c)からの中継メッセージ(Wi-Fiスキャン結果や診断情報)が
         * 更新されていたら、このコンソールへ出力する(CPU1にはデバッグ用UARTが
         * 無いため)。 */
        if (g_gas_ipc.wifi_scan_seq != last_wifi_scan_seq)
        {
            last_wifi_scan_seq = g_gas_ipc.wifi_scan_seq;

            char scan_buf[sizeof(g_gas_ipc.wifi_scan_text)];
            uint32_t i;
            for (i = 0; i < sizeof(scan_buf) - 1; i++)
            {
                scan_buf[i] = g_gas_ipc.wifi_scan_text[i];
                if (scan_buf[i] == '\0') { break; }
            }
            scan_buf[i] = '\0';

            console_write("[NET RELAY] ---- CPU1 message ----\r\n");
            console_write(scan_buf);
            console_write("\r\n[NET RELAY] ---- end ----\r\n");
        }

        g_gas_ipc.seq++;

        tk_dly_tsk(200);
    }
}

/* 優先度はガス側の方を高くしてある(数値が小さいほど優先度が高い)。
 * ガス読み取り+推論は1サイクルが短く、200ms周期を絶対に守る必要がある一方、
 * 音声側(task_audio)のMFCC/NPU推論は数百ms〜と重く、厳密な締め切りが無い。
 * 逆(音声側を高優先度)にすると、推論実行中ずっとガス側が飢餓状態になり、
 * 200ms周期のサンプリングが止まってしまう。なお、PDMの生サンプリング自体は
 * task_audioの優先度とは無関係にDMAC割込み(タスクより常に優先)で保護されている
 * ため、この優先度入れ替えでサンプリング取りこぼしが増えることはない。 */
LOCAL T_CTSK ctsk_gas =
{
    .itskpri = 10,
    .stksz   = 2048,
    .task    = task_gas,
    .tskatr  = TA_HLNG | TA_RNG3,
};

/* 音声取り込み+MFCC+NPU推論(audio_task.cのtask_audio_main)。 */
LOCAL void task_audio(INT stacd, void *exinf) { task_audio_main(stacd, exinf); }

LOCAL T_CTSK ctsk_audio =
{
    .itskpri = 11,
    .stksz   = 4096,
    .task    = task_audio,
    .tskatr  = TA_HLNG | TA_RNG3,
};

EXPORT INT usermain(void)
{
    tm_putstring((UB *)"Start User-main program.\n");

    audio_inference_init();        /* NPUモデル初期化 */
    start_microphone_recording();  /* PDMマイクのピン設定・録音準備 */

    tk_sta_tsk(tk_cre_tsk(&ctsk_audio), 0);
    tk_sta_tsk(tk_cre_tsk(&ctsk_gas), 0);

    tk_exd_tsk();
    return 0;
}

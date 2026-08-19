/*
 * esp_notify.c
 *
 * ESP-01S(ATコマンドファームウェア)をUART(PMOD2, SCI0, g_uart0)経由で操作し、
 * ntfy.sh(http://ntfy.sh/<topic>)へ平文HTTP POSTでプッシュ通知を送る。
 * TLS/SSLは使わない(ntfy.shが平文HTTPも受理することをcurlで確認済み)。
 *
 * ここはCPU1(表示担当)側のベストエフォートなネットワークI/Oであり、
 * CPU0の音声/ガス処理にも、CPU1の表示タスク(task_display)の描画にも
 * 影響させない(呼び出し元のtask_netはtask_displayより低優先度)。
 */
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <string.h>
#include "esp_notify.h"
#include "ipc_shared.h"

/* Wi-Fi認証情報・ntfy.shの宛先トピック。個人のコンテスト用デバイスのため
 * ソースに直書きしている。トピック名はテスト・デモ用途と割り切って
 * わかりやすい文字列にしてある(ntfy.shは公開トピック方式のため、
 * 名前を知っていれば誰でも購読・受信できる点に注意)。 */
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"
#define NTFY_TOPIC      "your-ntfy-topic-here"
#define NTFY_HOST       "ntfy.sh"

#define ESP_RX_RING_SIZE 512

static volatile bool g_tx_complete = false;
static uint8_t  g_rx_ring[ESP_RX_RING_SIZE];
static volatile uint32_t g_rx_head = 0; /* コールバック(割込み)側が書き込む位置 */
static uint32_t g_rx_tail = 0;          /* このモジュール内の読み出し位置 */

static esp_notify_status_t g_status = ESP_NOTIFY_STATUS_BOOT;

/* SENT/SEND_FAIL表示を保持する残り時間(task_netの300ms周期のtick数)。
 * 5秒程度表示したらREADYへ戻す。 */
#define ESP_NOTIFY_RESULT_DISPLAY_TICKS (17) /* 300ms * 17 ≈ 5.1s */
static uint32_t g_result_display_ticks = 0;

esp_notify_status_t esp_notify_get_status(void)
{
    return g_status;
}

void esp_notify_tick(void)
{
    if (g_result_display_ticks == 0)
    {
        return;
    }

    g_result_display_ticks--;
    if (g_result_display_ticks == 0
        && (g_status == ESP_NOTIFY_STATUS_SENT || g_status == ESP_NOTIFY_STATUS_SEND_FAIL))
    {
        g_status = ESP_NOTIFY_STATUS_READY;
    }
}

void esp_notify_uart_callback(uart_callback_args_t *p_args)
{
    if (p_args->event == UART_EVENT_TX_COMPLETE)
    {
        g_tx_complete = true;
    }
    else if (p_args->event == UART_EVENT_RX_CHAR)
    {
        uint32_t next = (g_rx_head + 1) % ESP_RX_RING_SIZE;
        if (next != g_rx_tail) /* バッファが一杯なら(応答が想定外に長い等)捨てる */
        {
            g_rx_ring[g_rx_head] = (uint8_t)p_args->data;
            g_rx_head = next;
        }
    }
}

static uint32_t rx_available(void)
{
    return (g_rx_head + ESP_RX_RING_SIZE - g_rx_tail) % ESP_RX_RING_SIZE;
}

static uint8_t rx_pop(void)
{
    uint8_t b = g_rx_ring[g_rx_tail];
    g_rx_tail = (g_rx_tail + 1) % ESP_RX_RING_SIZE;
    return b;
}

/* コマンド送信前に、前回までの受信残りを読み捨てる */
static void rx_flush(void)
{
    g_rx_tail = g_rx_head;
}

static void uart_write_blocking(const uint8_t *data, uint32_t len)
{
    if (len == 0)
    {
        return;
    }

    g_tx_complete = false;
    fsp_err_t err = R_SCI_B_UART_Write(&g_uart0_ctrl, data, len);
    if (err != FSP_SUCCESS)
    {
        return;
    }

    uint32_t timeout_ms = 2000;
    while (!g_tx_complete && timeout_ms > 0)
    {
        /* wait_for()/at_command()と同じ理由(タイマー周期10ms)で10ms刻み。 */
        tk_dly_tsk(10);
        timeout_ms = (timeout_ms > 10) ? (timeout_ms - 10) : 0;
    }
}

static void uart_write_str(const char *str)
{
    uart_write_blocking((const uint8_t *)str, (uint32_t)strlen(str));
}

/* 受信バイト列の末尾がneedleと一致するまで(タイムアウト付きで)待つ。
 * scratchは作業用バッファ(呼び出し側で用意する)。 */
static bool wait_for(const char *needle, uint32_t timeout_ms, char *scratch, uint32_t scratch_size)
{
    uint32_t pos = 0;
    uint32_t needle_len = (uint32_t)strlen(needle);

    if (scratch_size == 0)
    {
        return false;
    }
    scratch[0] = '\0';

    while (timeout_ms > 0)
    {
        while (rx_available() > 0 && pos < scratch_size - 1)
        {
            scratch[pos++] = (char)rx_pop();
            scratch[pos] = '\0';

            if (pos >= needle_len && strcmp(&scratch[pos - needle_len], needle) == 0)
            {
                return true;
            }
        }
        /* このμT-Kernel構成のタイマー周期は10ms(mtk3_bsp2/config/config.hの
         * CNF_TIMER_PERIOD)なので、tk_dly_tsk(1)は実際には約10ms待つことになる。
         * timeout_msも同じ10ms刻みで減らし、実待ち時間とtimeout_msの単位を
         * 一致させる(そうしないとタイムアウトが実質10倍に伸びてしまう)。 */
        tk_dly_tsk(10);
        timeout_ms = (timeout_ms > 10) ? (timeout_ms - 10) : 0;
    }
    return false;
}

/* ATコマンドを1つ送信し、"OK\r\n"/"ERROR\r\n"/"FAIL\r\n"のいずれかが来るまで待つ。
 * ("FAIL\r\n"はAT+CWJAPがWi-Fi接続自体に失敗した時の応答。"ERROR\r\n"とは
 * 別物なので両方見る必要がある)
 * 戻り値: OKならtrue、ERROR/FAIL/タイムアウトならfalse。 */
static bool at_command(const char *cmd, uint32_t timeout_ms)
{
    char scratch[160];
    uint32_t pos = 0;

    rx_flush();
    uart_write_str(cmd);
    uart_write_str("\r\n");

    scratch[0] = '\0';
    while (timeout_ms > 0)
    {
        while (rx_available() > 0 && pos < sizeof(scratch) - 1)
        {
            scratch[pos++] = (char)rx_pop();
            scratch[pos] = '\0';

            if (pos >= 4 && strcmp(&scratch[pos - 4], "OK\r\n") == 0)
            {
                return true;
            }
            if (pos >= 7 && strcmp(&scratch[pos - 7], "ERROR\r\n") == 0)
            {
                return false;
            }
            if (pos >= 6 && strcmp(&scratch[pos - 6], "FAIL\r\n") == 0)
            {
                return false;
            }
        }
        /* wait_for()と同じ理由(タイマー周期10ms)で、10ms刻みにしてある。 */
        tk_dly_tsk(10);
        timeout_ms = (timeout_ms > 10) ? (timeout_ms - 10) : 0;
    }
    return false;
}

/* 診断用テキストを共有メモリ(g_gas_ipc.wifi_scan_text)へコピーし、CPU0側の
 * コンソール中継をトリガーする(wifi_scan_seq流用。Wi-Fiスキャン結果に限らず、
 * 汎用の診断メッセージ中継として使う)。 */
static void relay_to_console(const char *text)
{
    uint32_t len = (uint32_t)strlen(text);
    if (len >= sizeof(g_gas_ipc.wifi_scan_text))
    {
        len = (uint32_t)sizeof(g_gas_ipc.wifi_scan_text) - 1;
    }
    for (uint32_t i = 0; i < len; i++)
    {
        g_gas_ipc.wifi_scan_text[i] = text[i];
    }
    g_gas_ipc.wifi_scan_text[len] = '\0';
    g_gas_ipc.wifi_scan_seq++;
}

void esp_notify_scan_wifi(void)
{
    char scratch[600];
    uint32_t pos = 0;
    uint32_t timeout_ms = 10000; /* スキャンはOK/ERRORまでに数秒かかる */
    bool done = false;

    rx_flush();
    uart_write_str("AT+CWLAP");
    uart_write_str("\r\n");

    scratch[0] = '\0';
    while (timeout_ms > 0 && !done)
    {
        while (rx_available() > 0 && pos < sizeof(scratch) - 1)
        {
            scratch[pos++] = (char)rx_pop();
            scratch[pos] = '\0';

            if ((pos >= 4 && strcmp(&scratch[pos - 4], "OK\r\n") == 0)
                || (pos >= 7 && strcmp(&scratch[pos - 7], "ERROR\r\n") == 0))
            {
                done = true;
                break;
            }
        }
        if (!done)
        {
            tk_dly_tsk(10);
            timeout_ms = (timeout_ms > 10) ? (timeout_ms - 10) : 0;
        }
    }

    relay_to_console(scratch);
}

/* AT疎通確認+Wi-Fi接続(AT+CWJAP)を試みる(ブロッキング、最大15秒程度)。
 * esp_notify_init()での初回接続と、esp_notify_send()での再接続
 * (Wi-Fiが途中で切れた場合の自動復帰)の両方から呼ばれる。
 * 戻り値: 接続できればtrue。 */
static bool try_connect(void)
{
    /* "AT"が最初の1回で拾えないことがある(モジュール起動直後のタイミング等)
     * ため、数回リトライする。 */
    bool at_ok = false;
    for (int i = 0; i < 3 && !at_ok; i++)
    {
        at_ok = at_command("AT", 1000);
    }

    if (!at_ok)
    {
        /* "AT"にすら応答が無い = 配線/ボーレート/モジュール自体の問題。
         * Wi-Fi接続を試みても無駄なのでここで打ち切る。 */
        relay_to_console("esp_notify: try_connect: AT x3 no response");
        g_status = ESP_NOTIFY_STATUS_AT_FAIL;
        return false;
    }

    at_command("ATE0", 1000);        /* エコーオフ(以降の応答解析を単純にする) */
    at_command("AT+CWMODE=1", 1000); /* ステーションモード */

    g_status = ESP_NOTIFY_STATUS_JOINING;

    char cwjap[128];
    tm_sprintf((UB *)cwjap, (UB *)"AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASSWORD);
    /* Wi-Fi接続は時間がかかるので長めのタイムアウト */
    bool joined = at_command(cwjap, 15000);

    if (!joined)
    {
        /* 接続失敗時は自動でスキャンし、目的のSSIDがそもそも見えているかを
         * CPU0側コンソールで確認できるようにする。 */
        esp_notify_scan_wifi();
        g_status = ESP_NOTIFY_STATUS_JOIN_FAIL;
        return false;
    }

    g_status = ESP_NOTIFY_STATUS_READY;
    return true;
}

void esp_notify_init(void)
{
    g_status = ESP_NOTIFY_STATUS_BOOT;

    R_SCI_B_UART_Open(&g_uart0_ctrl, &g_uart0_cfg);

    /* 起動直後、モジュール側のブート出力等の不定なバイト列が残っている
     * ことがあるので、少し待ってから受信バッファをクリアする。 */
    tk_dly_tsk(200);
    rx_flush();

    try_connect();
}

bool esp_notify_send(const char *message)
{
    if (g_status == ESP_NOTIFY_STATUS_AT_FAIL
        || g_status == ESP_NOTIFY_STATUS_JOIN_FAIL
        || g_status == ESP_NOTIFY_STATUS_SEND_FAIL)
    {
        /* 接続が切れている/失敗している状態から送信しようとした場合は、
         * まず再接続を試みる(Wi-Fiが途中で切れた場合の自動復帰)。 */
        if (!try_connect())
        {
            return false;
        }
    }

    if (g_status != ESP_NOTIFY_STATUS_READY && g_status != ESP_NOTIFY_STATUS_SENT)
    {
        /* BOOT/JOINING/SENDING中(他の処理が進行中)は送らない。
         * AT_FAIL/JOIN_FAIL/SEND_FAILは上の再接続処理で解消済みのはず。 */
        return false;
    }

    g_status = ESP_NOTIFY_STATUS_SENDING;

    char cipstart[64];
    tm_sprintf((UB *)cipstart, (UB *)"AT+CIPSTART=\"TCP\",\"%s\",80", NTFY_HOST);
    if (!at_command(cipstart, 5000))
    {
        g_status = ESP_NOTIFY_STATUS_SEND_FAIL;
        g_result_display_ticks = ESP_NOTIFY_RESULT_DISPLAY_TICKS;
        return false;
    }

    char request[256];
    tm_sprintf((UB *)request,
               (UB *)"POST /%s HTTP/1.1\r\nHost: %s\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
               NTFY_TOPIC, NTFY_HOST, (int)strlen(message), message);
    uint32_t request_len = (uint32_t)strlen(request);

    char cipsend[32];
    tm_sprintf((UB *)cipsend, (UB *)"AT+CIPSEND=%d", (int)request_len);

    rx_flush();
    uart_write_str(cipsend);
    uart_write_str("\r\n");

    /* ">"プロンプト(送信データ待ち)を待ってから本体を送る */
    char prompt_scratch[16];
    if (!wait_for(">", 3000, prompt_scratch, sizeof(prompt_scratch)))
    {
        at_command("AT+CIPCLOSE", 2000);
        g_status = ESP_NOTIFY_STATUS_SEND_FAIL;
        g_result_display_ticks = ESP_NOTIFY_RESULT_DISPLAY_TICKS;
        return false;
    }

    rx_flush();
    uart_write_blocking((const uint8_t *)request, request_len);

    char send_scratch[64];
    bool send_ok = wait_for("SEND OK", 5000, send_scratch, sizeof(send_scratch));

    at_command("AT+CIPCLOSE", 2000);

    g_status = send_ok ? ESP_NOTIFY_STATUS_SENT : ESP_NOTIFY_STATUS_SEND_FAIL;
    g_result_display_ticks = ESP_NOTIFY_RESULT_DISPLAY_TICKS;
    return send_ok;
}

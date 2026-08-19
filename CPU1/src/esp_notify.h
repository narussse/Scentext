/*
 * esp_notify.h
 *
 * ESP-01S(ATコマンドファームウェア、PMOD2/SCI0/g_uart0経由)を使った、
 * ntfy.sh(http://ntfy.sh/<topic>、平文HTTPでの動作をcurlで確認済み)への
 * プッシュ通知送信。CPU1(表示担当)側の新規タスクtask_net(hal_entry.c)から
 * 使う想定。TLS/SSLは使わない(ESP8266のAT側での証明書設定が不要になるため)。
 */
#ifndef ESP_NOTIFY_H
#define ESP_NOTIFY_H

#include <stdbool.h>
#include "hal_data.h"

/* CPU1表示側のNETステータス行("NET: BOOT/JOINING/READY/..."表示)に使う。
 * CPU1にはデバッグ用UARTが無く、AT応答の中身を直接確認できないため、
 * どの段階で失敗したかを切り分けられるよう状態を細かく分けてある。 */
typedef enum {
    ESP_NOTIFY_STATUS_BOOT = 0,      /* 起動直後、AT初期化前 */
    ESP_NOTIFY_STATUS_AT_FAIL = 1,   /* "AT"コマンド自体に応答が無い(配線/ボーレート/モジュール自体の問題) */
    ESP_NOTIFY_STATUS_JOINING = 2,   /* AT応答は取れた。Wi-Fi接続中 */
    ESP_NOTIFY_STATUS_READY = 3,     /* Wi-Fi接続済み、送信待ち */
    ESP_NOTIFY_STATUS_SENDING = 4,   /* 通知送信中 */
    ESP_NOTIFY_STATUS_SENT = 5,      /* 直近の送信が成功 */
    ESP_NOTIFY_STATUS_JOIN_FAIL = 6, /* AT応答は取れたが、Wi-Fi接続(AT+CWJAP)に失敗 */
    ESP_NOTIFY_STATUS_SEND_FAIL = 7, /* Wi-Fi接続済みだが、直近の通知送信に失敗 */
} esp_notify_status_t;

/* task_netの起動直後に1回呼ぶ。ATコマンド初期化+Wi-Fi接続を行う(ブロッキング、
 * Wi-Fi接続だけで最大15秒程度かかる)。 */
void esp_notify_init(void);

/*
 * ntfy.shへ1件通知を送る(ブロッキング)。戻り値: 送信成功ならtrue。
 * messageはASCII文字のみを想定(日本語・改行はHTTPリクエストの単純さのため
 * 非対応。本文中にHTTPヘッダの区切りと衝突する文字は含めないこと)。
 */
bool esp_notify_send(const char *message);

/* CPU1表示側のNETステータス行表示用。 */
esp_notify_status_t esp_notify_get_status(void);

/*
 * task_netのループから300ms周期で呼ぶこと。SENT/SEND_FAIL状態が一定時間
 * (5秒程度)続いたら自動的にREADYへ戻す(SENT/SEND_FAILのまま表示され
 * 続けないようにするため)。
 */
void esp_notify_tick(void);

/*
 * AT+CWLAPで周辺のWi-Fiアクセスポイント一覧をスキャンし、生の応答テキストを
 * 共有メモリ(g_gas_ipc.wifi_scan_text)へ書き込む(ブロッキング、最大10秒程度)。
 * CPU1にはデバッグ用UARTが無いため、CPU0側の既存コンソールに中継して
 * 目視確認する診断用の機能。Wi-Fi接続(AT+CWJAP)失敗時にesp_notify_init()から
 * 自動的に呼ばれる。
 */
void esp_notify_scan_wifi(void);

/* g_uart0のUARTコールバックとしてFSP Configuration(Callbackプロパティ)に
 * 設定すること。 */
void esp_notify_uart_callback(uart_callback_args_t *p_args);

#endif /* ESP_NOTIFY_H */

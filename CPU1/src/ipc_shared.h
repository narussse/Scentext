/*
 * ipc_shared.h
 *
 * CPU0(ガスセンサー読み取り)とCPU1(GLCDC表示)の間で共有する物理メモリ領域の定義。
 *
 * CPU0のRAM(0x22000000〜0x220ea000)とCPU1のRAM(0x220ea000〜0x221d4000)は
 * 同じ物理SRAMバス上で隣接しており、どちらのコアも0x22000000〜0x221d4000の
 * 全範囲を直接読み書きできる(MPU等による制限なし)。
 * IPC_SHARED_ADDR(CPU0のRAM末尾から4KB手前)は、現状のCPU0側の使用量
 * (ガスセンサー+UARTのみ、静的データはごくわずか)からは十分離れており、
 * 通常のリンカ配置(.bss/.data/heap/stack)がここまで伸びてくることはない。
 *
 * リンカのセクション機構(セクション名→メモリリージョンの自動対応付け)を経由すると、
 * CPU0側のRAMリージョン境界やCPU1側のRAMリージョン外という制約でエラーになりやすい
 * (実際に "section .ipc_shared will not fit in region RAM" で一度失敗した)。
 * そのため、ここでは単純にペリフェラルレジスタと同じ「固定物理アドレスへの
 * ポインタキャスト」でアクセスし、リンカのセクション管理を一切経由しない。
 */
#ifndef IPC_SHARED_H
#define IPC_SHARED_H

#include <stdint.h>

typedef struct {
    uint32_t seq;      /* CPU0が1サンプル書き込むごとに +1 する(CPU1側で更新検知に使える) */
    int32_t  no2;
    int32_t  c2h5ch;
    int32_t  voc;
    int32_t  co;
    uint32_t any_rising; /* CPU1のREADY/WAIT表示用。gas_recognition.cの立上り状態機械で
                          * いずれかのチャンネルが現在RISING中の場合に加え、
                          * gas_line2_state/audio_stateがIDLEでない間(収集中・結果表示中・
                          * 音響解析中)も1になる("busy"の意味。名前は歴史的経緯でany_risingのまま)。 */
    uint32_t gas_line2_state;   /* gas_line2_state_t (gas_recognition.h) */
    uint32_t gas_line2_seconds; /* 収集中/結果表示中の残り秒数(切り上げ) */
    uint32_t audio_state;       /* audio_status_state_t (audio_status.h) */
    uint32_t audio_percent;     /* 音響認識の確率(%)。結果確定時のみ意味を持つ */
    uint32_t audio_seconds;     /* 結果表示中の残り秒数(切り上げ) */
    uint32_t warming_up;        /* gas_recognition_is_warming_up()。1ならセンサーのヒーターが
                                 * まだウォームアップ中(CPU1側で「GAS SENSOR HEATING...」表示に使う)。 */
    uint32_t wifi_scan_seq;     /* CPU1(esp_notify.c)がWi-Fiスキャン(AT+CWLAP)結果を
                                 * 書き込むたびに+1する。CPU0側はこれが変化したら
                                 * wifi_scan_textをコンソールへ中継出力する
                                 * (CPU1にはデバッグ用UARTが無いため)。 */
    char     wifi_scan_text[600]; /* AT+CWLAPの生応答テキスト(ヌル終端)。診断用。 */
} gas_ipc_shared_t;

#define IPC_SHARED_ADDR (0x220E9000UL)
#define g_gas_ipc (*(volatile gas_ipc_shared_t *)IPC_SHARED_ADDR)

#endif /* IPC_SHARED_H */

#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include "hal_data.h"
#include "ipc_shared.h"
#include "esp_notify.h"

#if (1 == BSP_MULTICORE_PROJECT) && BSP_TZ_SECURE_BUILD
bsp_ipc_semaphore_handle_t g_core_start_semaphore =
{
    .semaphore_num = 0
};
#endif

/* --- GLCDC(1024x600 / 32bit RGB888)の色バンド表示テスト ---
 * デュアルコア検証用: CPU0(音声・ガス・NPU)から完全に分離して、
 * CPU1だけでGLCDCを起動・表示し続ける。
 * mtk3bsp2_ra8p1_ek_grove_pdm_glcdc の display_task.c を、
 * μT-Kernel無し(ベアメタル)向けに移植したもの。 */

#define GLCDC_BYTES_PER_PIXEL  (4)  /* 32bit RGB888 */
#define GLCDC_COLOR_RED        (0x00FF0000)
#define GLCDC_COLOR_GREEN      (0x0000FF00)
#define GLCDC_COLOR_BLUE       (0x000000FF)
#define GLCDC_COLOR_BLACK      (0x00000000)
#define GLCDC_COLOR_WHITE      (0xFFFFFFFF)
#define GLCDC_COLOR_GRAY       (0x00808080)
#define GLCDC_COLOR_YELLOW     (0xFFFFFF00)
#define GLCDC_COLOR_ORANGE     (0xFFFF8C00)
#define GLCDC_COLOR_GREEN_DEEP (0x00008000) /* 純緑(0x00FF00)は黄緑寄りに見えるため、C2H5CHパネル用に少し暗めの緑を使う */
#define GLCDC_COLOR_MAGENTA    (0x00FF00FF)
#define GLCDC_COLOR_CYAN       (0x0000FFFF)
#define GLCDC_COLOR_BAND_COUNT (8)

/* DISP_RESETピン(P606)。CPU0側のプロジェクトと競合するため静的ピン設定では
 * 持たせず、ここで実行時にHIGH(リセット解除)にする。 */
#define GLCDC_DISP_RESET_PIN (BSP_IO_PORT_06_PIN_06)
/* バックライト制御ピン(DISP_BLEN)。GLCDCが安定して描画するまではOFFにしておく。 */
#define GLCDC_DISP_BLEN_PIN (BSP_IO_PORT_05_PIN_14)

/* --- 5x7ドットの自前ビットマップフォント(mtk3bsp2_ra8p1_ek_grove_pdm_glcdc の
 * display_task.c から移植)。対応文字: 0-9 . - N O C H V R E A D Y W I T スペース。
 * R/E/A/D/Y/W/I/T は上部の状態表示("READY"/"WAIT")用に追加した。 */
typedef struct {
    char ch;
    uint8_t rows[7];
} glcdc_glyph_t;

static const glcdc_glyph_t g_font_table[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x0E, 0x11, 0x01, 0x06, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'N', {0x11, 0x19, 0x15, 0x15, 0x13, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'%', {0x19, 0x1A, 0x02, 0x04, 0x08, 0x0B, 0x13}},
    {'(', {0x04, 0x08, 0x10, 0x10, 0x10, 0x08, 0x04}},
    {')', {0x04, 0x02, 0x01, 0x01, 0x01, 0x02, 0x04}},
    {'[', {0x1C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1C}},
    {']', {0x07, 0x01, 0x01, 0x01, 0x01, 0x01, 0x07}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
#define GLCDC_FONT_COUNT     ((int)(sizeof(g_font_table) / sizeof(g_font_table[0])))
#define GLCDC_FONT_SCALE     (3)
#define GLCDC_FONT_CHAR_W    (5 * GLCDC_FONT_SCALE)
#define GLCDC_FONT_CHAR_H    (7 * GLCDC_FONT_SCALE)
#define GLCDC_FONT_CHAR_GAP  (GLCDC_FONT_SCALE)

/* --- 4分割パネルのレイアウト(display_task.c と同じ定数) --- */
#define GAS_BAR_LEFT_MARGIN      (20)
#define GAS_BAR_RIGHT_MARGIN     (20)
#define GLCDC_PANEL_COUNT        (4)
#define GLCDC_PANEL_GAP          (20)
#define GLCDC_PANEL_LABEL_TOP    (130)
#define GLCDC_PANEL_GRAPH_TOP    (162)
#define GLCDC_PANEL_GRAPH_HEIGHT (380)
#define GAS_MIN_DISPLAY_RANGE    (20) /* 自動スケールの最低幅。これより実変動が小さい場合は底上げする */

/* --- CPU0から共有メモリ(g_gas_ipc, ipc_shared.h)経由で受け取る実データの履歴。
 * script/fsp.lld で物理アドレス0x220E9000(RAM末尾4KB)に固定配置している。 */
#define GAS_SAMPLE_INTERVAL_MS   (200)
#define GAS_CH_COUNT_DISPLAY     (4)

/* 折れ線グラフに表示する時間幅(直近2分=120秒)。サンプル間隔はCPU1の描画周期
 * (GAS_SAMPLE_INTERVAL_MS)に等しい(1描画につき最大1サンプル追記されるため)。
 * リングバッファ本体(GAS_HISTORY_LEN)は、この表示幅ぴったり入る大きさにする
 * (表示ウィンドウより小さいと直近120秒分を保持しきれない)。 */
#define GAS_WINDOW_MS            (60000)
#define GAS_WINDOW_SAMPLES       (GAS_WINDOW_MS / GAS_SAMPLE_INTERVAL_MS)
#define GAS_HISTORY_LEN          (GAS_WINDOW_SAMPLES)

static int32_t  g_gas_history[GAS_CH_COUNT_DISPLAY][GAS_HISTORY_LEN];
static uint16_t g_gas_history_idx = 0;
static uint16_t g_gas_history_count = 0;
static uint32_t g_last_seq = 0;

/* CPU0が新しい値を書いていれば(seqが進んでいれば)履歴に1サンプル追記する */
static void glcdc_sample_gas_history(void)
{
    uint32_t seq = g_gas_ipc.seq;
    if (seq == g_last_seq)
    {
        return;
    }
    g_last_seq = seq;

    g_gas_history[0][g_gas_history_idx] = g_gas_ipc.no2;
    g_gas_history[1][g_gas_history_idx] = g_gas_ipc.c2h5ch;
    g_gas_history[2][g_gas_history_idx] = g_gas_ipc.voc;
    g_gas_history[3][g_gas_history_idx] = g_gas_ipc.co;

    g_gas_history_idx = (uint16_t)((g_gas_history_idx + 1) % GAS_HISTORY_LEN);
    if (g_gas_history_count < GAS_HISTORY_LEN)
    {
        g_gas_history_count++;
    }
}

/* 指定チャンネルの、現在有効な履歴の中でのmin/maxを求める */
static void glcdc_compute_minmax(int ch, uint16_t count, uint16_t start_idx, int32_t *min_out, int32_t *max_out)
{
    int32_t min_v = g_gas_history[ch][start_idx];
    int32_t max_v = min_v;

    for (uint16_t x = 1; x < count; x++)
    {
        uint16_t idx = (uint16_t)((start_idx + x) % GAS_HISTORY_LEN);
        int32_t v = g_gas_history[ch][idx];
        if (v < min_v) { min_v = v; }
        if (v > max_v) { max_v = v; }
    }
    *min_out = min_v;
    *max_out = max_v;
}

static uint16_t g_lcd_hsize, g_lcd_vsize;
static uint32_t g_lcd_hstride; /* 1行あたりの実バイト数(パディングを含む、hsize*4とは限らない) */
static uint32_t g_lcd_buffer_size;
static uint8_t *g_lcd_single_buf, *g_lcd_double_buf;

/* (x1,y1)-(x2,y2)の矩形を単色で塗りつぶす(ダブルバッファの両面に書く)。
 * 行の先頭アドレス計算にはhsizeではなく、実際のメモリストライド(hstride)を使う。 */
static void glcdc_fill_rect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    uint32_t start_addr = (uint32_t)((x1 * GLCDC_BYTES_PER_PIXEL) + (y1 * g_lcd_hstride));
    uint32_t display_length = (uint32_t)((x2 - x1) * GLCDC_BYTES_PER_PIXEL);

    for (uint16_t row = y1; row < y2; row++)
    {
        for (uint32_t off = 0; off < display_length; off += GLCDC_BYTES_PER_PIXEL)
        {
            *(uint32_t *)(g_lcd_single_buf + start_addr + off) = color;
            *(uint32_t *)(g_lcd_double_buf + start_addr + off) = color;
        }
        start_addr += g_lcd_hstride;
    }
}

/* (x1,y1)-(x2,y2)を結ぶ線分を描画する(ハードウェア高速描画命令が無いための簡易実装)。
 * x方向に1ピクセルずつ進みながらyを線形補間するが、傾きが急な区間では
 * 「1列につき1点」だけだと縦方向に隙間ができて点線に見えてしまうため、
 * 各x列では「直前の列のy」から「この列のy」までを塗りつぶして接続する。 */
#define GAS_LINE_THICK (2) /* 折れ線グラフの太さ(px) */

static void glcdc_draw_line(uint16_t x1, int32_t y1, uint16_t x2, int32_t y2, uint32_t color)
{
    if (x2 <= x1)
    {
        int32_t top = (y1 < y2) ? y1 : y2;
        int32_t bottom = (y1 < y2) ? y2 : y1;
        glcdc_fill_rect(x1, (uint16_t)top, (uint16_t)(x1 + GAS_LINE_THICK + 1), (uint16_t)(bottom + GAS_LINE_THICK + 1), color);
        return;
    }

    int32_t dx = (int32_t)(x2 - x1);
    int32_t prev_y = y1;
    for (uint16_t x = x1; x <= x2; x++)
    {
        int32_t y = y1 + (int32_t)((y2 - y1) * (int32_t)(x - x1)) / dx;
        int32_t top = (y < prev_y) ? y : prev_y;
        int32_t bottom = (y < prev_y) ? prev_y : y;
        glcdc_fill_rect(x, (uint16_t)top, (uint16_t)(x + GAS_LINE_THICK), (uint16_t)(bottom + GAS_LINE_THICK), color);
        prev_y = y;
    }
}

/* 画面を8色の水平帯(色バンド)で埋め尽くす */
static void glcdc_color_band_display(void)
{
    uint32_t colors[GLCDC_COLOR_BAND_COUNT] = {
        GLCDC_COLOR_RED, GLCDC_COLOR_GREEN, GLCDC_COLOR_BLUE, GLCDC_COLOR_BLACK,
        GLCDC_COLOR_WHITE, GLCDC_COLOR_YELLOW, GLCDC_COLOR_MAGENTA, GLCDC_COLOR_CYAN,
    };
    uint16_t band_height = (uint16_t)(g_lcd_vsize / GLCDC_COLOR_BAND_COUNT);

    for (int i = 0; i < GLCDC_COLOR_BAND_COUNT; i++)
    {
        glcdc_fill_rect(0, (uint16_t)(i * band_height), g_lcd_hsize, (uint16_t)((i + 1) * band_height), colors[i]);
    }
}

/* 1文字を(x,y)を左上として描画する。未対応文字は何も描かない(空白扱い)。
 * scale=GLCDC_FONT_SCALEで従来通りのサイズ、小さくしたい場合はより小さい値を渡す。 */
static void glcdc_draw_char_s(uint16_t x, uint16_t y, char ch, uint32_t color, uint16_t scale)
{
    const uint8_t *rows = NULL;

    for (int i = 0; i < GLCDC_FONT_COUNT; i++)
    {
        if (g_font_table[i].ch == ch)
        {
            rows = g_font_table[i].rows;
            break;
        }
    }
    if (rows == NULL)
    {
        return;
    }

    for (int row = 0; row < 7; row++)
    {
        uint8_t bits = rows[row];
        for (int col = 0; col < 5; col++)
        {
            if (bits & (1U << (4 - col)))
            {
                uint16_t px = (uint16_t)(x + col * scale);
                uint16_t py = (uint16_t)(y + row * scale);
                glcdc_fill_rect(px, py, (uint16_t)(px + scale), (uint16_t)(py + scale), color);
            }
        }
    }
}

static void glcdc_draw_char(uint16_t x, uint16_t y, char ch, uint32_t color)
{
    glcdc_draw_char_s(x, y, ch, color, GLCDC_FONT_SCALE);
}

/* 文字列を(x,y)を左上として横方向に描画する。
 * scale=GLCDC_FONT_SCALEで従来通りのサイズ、小さくしたい場合はより小さい値を渡す。 */
static void glcdc_draw_text_s(uint16_t x, uint16_t y, const char *str, uint32_t color, uint16_t scale)
{
    uint16_t cx = x;
    uint16_t char_w = (uint16_t)(5 * scale);
    uint16_t char_gap = scale;
    while (*str)
    {
        glcdc_draw_char_s(cx, y, *str, color, scale);
        cx = (uint16_t)(cx + char_w + char_gap);
        str++;
    }
}

static void glcdc_draw_text(uint16_t x, uint16_t y, const char *str, uint32_t color)
{
    glcdc_draw_text_s(x, y, str, color, GLCDC_FONT_SCALE);
}

/* 符号付き整数を10進文字列に変換する(標準ライブラリのitoaが無い環境向けの自前実装) */
static void glcdc_itoa(int32_t value, char *buf)
{
    char tmp[12];
    int len = 0;
    uint8_t neg = 0;
    uint32_t v;

    if (value < 0)
    {
        neg = 1;
        v = (uint32_t)(-value);
    }
    else
    {
        v = (uint32_t)value;
    }

    if (v == 0)
    {
        tmp[len++] = '0';
    }
    else
    {
        while (v > 0 && len < (int)sizeof(tmp))
        {
            tmp[len++] = (char)('0' + (v % 10));
            v /= 10;
        }
    }
    if (neg)
    {
        tmp[len++] = '-';
    }

    int n = 0;
    for (int i = len - 1; i >= 0; i--)
    {
        buf[n++] = tmp[i];
    }
    buf[n] = '\0';
}

/* 0.1秒単位の値(例: 124 -> "12.4")を文字列にする。横軸の秒数目盛りを
 * 小数点第一位まで表示するために使う。ただし小数部が0の場合は
 * ".0"を付けず整数のみ表示する(例: 120 -> "12")。 */
static void glcdc_format_decisec(uint32_t tenths, char *buf)
{
    uint32_t whole = tenths / 10;
    uint32_t frac = tenths % 10;
    char wbuf[12];
    glcdc_itoa((int32_t)whole, wbuf);

    uint32_t pos = 0;
    for (const char *p = wbuf; *p; p++) { buf[pos++] = *p; }
    if (frac == 0)
    {
        buf[pos] = '\0';
        return;
    }
    buf[pos++] = '.';
    buf[pos++] = (char)('0' + frac);
    buf[pos] = '\0';
}

/* 矩形の外枠だけを描画する(太さ2px)。 */
static void glcdc_draw_border(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, uint32_t color)
{
    glcdc_fill_rect(x1, y1, x2, (uint16_t)(y1 + 2), color);
    glcdc_fill_rect(x1, (uint16_t)(y2 - 2), x2, y2, color);
    glcdc_fill_rect(x1, y1, (uint16_t)(x1 + 2), y2, color);
    glcdc_fill_rect((uint16_t)(x2 - 2), y1, x2, y2, color);
}

#define GAS_TICK_FONT_SCALE   (2) /* 縦軸目盛りの文字は少し小さくする */
#define GAS_Y_AXIS_LABEL_W    (46) /* 縦軸目盛りの数字を置く左側の余白 */
#define GAS_TICK_COUNT        (5)  /* 縦軸目盛りの本数(最小・最大を含む) */
#define GAS_TICK_MARK_LEN     (5)  /* 軸から突き出す目盛りマークの長さ(px) */
#define GAS_TICK_MARK_THICK   (2)  /* 目盛りマークの太さ(px) */
#define GAS_AXIS_TITLE_FONT_SCALE (GAS_TICK_FONT_SCALE)

/* パネルの静的要素(チャンネル名・外枠・目盛りマークの位置・軸タイトル)を描画する。
 * これらはデータに依存せず、位置も内容も一切変化しない。起動時に1回だけ呼び出す
 * (毎フレーム消して描き直すと、変化していない要素までちらつく原因になるため)。 */
static void glcdc_draw_channel_panel_static(uint16_t panel_x1, uint16_t panel_w, const char *name)
{
    uint16_t panel_x2 = (uint16_t)(panel_x1 + panel_w);
    uint16_t graph_left = (uint16_t)(panel_x1 + GAS_Y_AXIS_LABEL_W);
    uint16_t graph_right = panel_x2;
    uint16_t graph_top = GLCDC_PANEL_GRAPH_TOP;
    uint16_t graph_bottom = (uint16_t)(GLCDC_PANEL_GRAPH_TOP + GLCDC_PANEL_GRAPH_HEIGHT);
    uint16_t axis_label_top = (uint16_t)(graph_bottom + 6);
    uint16_t axis_title_top = (uint16_t)(axis_label_top + 7 * GAS_TICK_FONT_SCALE + 6);

    /* チャンネル名(黒・グラフ枠の中央寄せ) */
    {
        char label[24];
        uint32_t pos = 0;
        for (const char *p = name; *p; p++) { label[pos++] = *p; }
        label[pos] = '\0';

        uint16_t label_w = (uint16_t)(pos * (GLCDC_FONT_CHAR_W + GLCDC_FONT_SCALE));
        uint16_t frame_w = (uint16_t)(graph_right - graph_left);
        uint16_t label_x = graph_left;
        if (frame_w > label_w)
        {
            label_x = (uint16_t)(graph_left + (frame_w - label_w) / 2);
        }
        glcdc_draw_text(label_x, GLCDC_PANEL_LABEL_TOP, label, GLCDC_COLOR_BLACK);
    }

    /* グラフ領域の外枠 */
    glcdc_draw_border(graph_left, graph_top, graph_right, graph_bottom, GLCDC_COLOR_BLACK);

    /* 縦軸の目盛りマーク。位置(tick_y)はGLCDC_PANEL_GRAPH_HEIGHTのみで決まり
     * データに依存しないため、ここで固定描画してよい(数値はdynamic側で描く)。 */
    for (int t = 0; t < GAS_TICK_COUNT; t++)
    {
        uint16_t tick_y = (uint16_t)(graph_bottom - (uint32_t)(GLCDC_PANEL_GRAPH_HEIGHT * t) / (GAS_TICK_COUNT - 1));
        glcdc_fill_rect((uint16_t)(graph_left - GAS_TICK_MARK_LEN), tick_y,
                         graph_left, (uint16_t)(tick_y + GAS_TICK_MARK_THICK), GLCDC_COLOR_BLACK);
    }

    /* 横軸の目盛りマーク・軸タイトル。位置はグラフ幅のみで決まり
     * データに依存しないため、ここで固定描画してよい(数値はdynamic側で描く)。 */
    {
        uint16_t graph_span_w = (uint16_t)(graph_right - graph_left);

        for (int k = 0; k < 3; k++)
        {
            uint16_t gx = (uint16_t)(graph_left + (uint32_t)graph_span_w * k / 2);
            glcdc_fill_rect(gx, graph_bottom, (uint16_t)(gx + GAS_TICK_MARK_THICK),
                             (uint16_t)(graph_bottom + GAS_TICK_MARK_LEN), GLCDC_COLOR_BLACK);
        }

        static const char axis_title[] = "TIME(S)";
        uint32_t title_len = 0;
        for (const char *p = axis_title; *p; p++) { title_len++; }
        uint16_t title_step = (uint16_t)(5 * GAS_AXIS_TITLE_FONT_SCALE + GAS_AXIS_TITLE_FONT_SCALE);
        uint16_t title_w = (uint16_t)(title_len * title_step);
        uint16_t title_x = graph_left;
        if (graph_span_w > title_w)
        {
            title_x = (uint16_t)(graph_left + (graph_span_w - title_w) / 2);
        }
        glcdc_draw_text_s(title_x, axis_title_top, axis_title, GLCDC_COLOR_BLACK, GAS_AXIS_TITLE_FONT_SCALE);
    }
}

/* 毎フレーム変化する要素(縦軸・横軸の目盛り数値、折れ線グラフ本体)だけを描画する。
 * 外枠・目盛りマーク・チャンネル名・軸タイトルはglcdc_draw_channel_panel_static()で
 * 起動時に描画済みのものをそのまま使う(触らない)ことで、変化していない要素の
 * ちらつきを防ぐ。window_count/window_startは、履歴リングバッファのうち
 * 表示対象分を指す。 */
static void glcdc_draw_channel_panel_dynamic(int ch, uint16_t panel_x1, uint16_t panel_w, uint32_t color,
                                              uint16_t window_count, uint16_t window_start)
{
    uint16_t panel_x2 = (uint16_t)(panel_x1 + panel_w);
    uint16_t graph_left = (uint16_t)(panel_x1 + GAS_Y_AXIS_LABEL_W);
    uint16_t graph_right = panel_x2;
    uint16_t graph_top = GLCDC_PANEL_GRAPH_TOP;
    uint16_t graph_bottom = (uint16_t)(GLCDC_PANEL_GRAPH_TOP + GLCDC_PANEL_GRAPH_HEIGHT);
    uint16_t axis_label_top = (uint16_t)(graph_bottom + 6);

    int32_t min_v, max_v;
    glcdc_compute_minmax(ch, window_count, window_start, &min_v, &max_v);

    /* Python版(gas_audio_realtime_plot_v8.py)の
     *   margin = max((max_v - min_v) * 0.1, 5)
     *   ax.set_ylim(max(0, min_v - margin), max_v + margin)
     * と同じロジック。急激な変化(スパイク)がグラフ枠の上端/下端に
     * ぴったり張り付いて見えてしまうのを防ぐため、実際の最小/最大値の
     * 外側に必ず一定の余白を確保する(変動幅が小さい場合は余白の下限
     * GAS_MIN_DISPLAY_RANGE/2でノイズが誇張されすぎるのも防ぐ)。 */
    {
        int32_t margin = ((max_v - min_v) * 10) / 100;
        if (margin < (GAS_MIN_DISPLAY_RANGE / 2))
        {
            margin = GAS_MIN_DISPLAY_RANGE / 2;
        }
        min_v -= margin;
        max_v += margin;
        if (min_v < 0)
        {
            min_v = 0;
        }
    }
    int32_t range = max_v - min_v;

    /* 縦軸の目盛り数値: 前回描画時と5個すべて一致していれば何もしない
     * (ちらつき防止)。1つでも変化していれば、まとめてクリア&再描画する。
     * 数値の文字はtick_yを中心に上下に(半文字高ぶん)はみ出して描画されるため、
     * 最上段・最下段の数値がgraph_top/graph_bottomちょうどで欠けてクリアされず、
     * 前フレームの文字と重なって滲んで見えてしまう。その分だけクリア範囲を広げる。 */
    {
        static int32_t s_prev_tick_v[GAS_CH_COUNT_DISPLAY][GAS_TICK_COUNT];
        static uint8_t s_prev_tick_v_valid[GAS_CH_COUNT_DISPLAY];

        int32_t tick_v[GAS_TICK_COUNT];
        uint8_t changed = !s_prev_tick_v_valid[ch];
        for (int t = 0; t < GAS_TICK_COUNT; t++)
        {
            tick_v[t] = min_v + (int32_t)((int64_t)range * t / (GAS_TICK_COUNT - 1));
            if (tick_v[t] != s_prev_tick_v[ch][t])
            {
                changed = 1;
            }
        }

        if (changed)
        {
            s_prev_tick_v_valid[ch] = 1;

            uint16_t half_h = (uint16_t)(7 * GAS_TICK_FONT_SCALE / 2);
            uint16_t y_clear_top = (uint16_t)((graph_top > half_h) ? (graph_top - half_h) : 0);
            uint16_t y_clear_bottom = (uint16_t)(graph_bottom + half_h);
            glcdc_fill_rect(panel_x1, y_clear_top, (uint16_t)(graph_left - GAS_TICK_MARK_LEN), y_clear_bottom, GLCDC_COLOR_WHITE);

            for (int t = 0; t < GAS_TICK_COUNT; t++)
            {
                s_prev_tick_v[ch][t] = tick_v[t];

                uint16_t tick_y = (uint16_t)(graph_bottom - (uint32_t)(GLCDC_PANEL_GRAPH_HEIGHT * t) / (GAS_TICK_COUNT - 1));
                uint16_t text_y = (uint16_t)((tick_y > (7 * GAS_TICK_FONT_SCALE / 2)) ? (tick_y - (7 * GAS_TICK_FONT_SCALE / 2)) : 0);

                char numbuf[12];
                glcdc_itoa(tick_v[t], numbuf);
                glcdc_draw_text_s(panel_x1, text_y, numbuf, GLCDC_COLOR_BLACK, GAS_TICK_FONT_SCALE);
            }
        }
    }

    if (window_count < 2)
    {
        return;
    }

    /* 横軸(経過秒数)の目盛り数値: 表示中の時間幅(span_dsec)が前回と同じなら
     * 3つの数値もすべて同じになるため、span_dsecだけ比較すればよい
     * (マーク・タイトルは静的描画済みで触らない)。常時監視用途のため絶対経過時間
     * (起動からの秒数)ではなく、「現在表示中のウィンドウの先頭からの相対秒数」で
     * 表示する(左端は常に"0"固定、右端は表示中の時間幅そのもの)。
     * 小数点第一位まで表示する。 */
    {
        static uint32_t s_prev_span_dsec[GAS_CH_COUNT_DISPLAY] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };

        uint32_t span_dsec = (uint32_t)((uint64_t)window_count * GAS_SAMPLE_INTERVAL_MS / 100);

        if (span_dsec != s_prev_span_dsec[ch])
        {
            s_prev_span_dsec[ch] = span_dsec;

            glcdc_fill_rect(graph_left, axis_label_top, graph_right,
                             (uint16_t)(axis_label_top + 7 * GAS_TICK_FONT_SCALE), GLCDC_COLOR_WHITE);

            uint16_t graph_span_w = (uint16_t)(graph_right - graph_left);

            for (int k = 0; k < 3; k++)
            {
                uint32_t dsec = (uint32_t)((uint64_t)span_dsec * k / 2);
                uint16_t gx = (uint16_t)(graph_left + (uint32_t)graph_span_w * k / 2);

                char numbuf[12];
                glcdc_format_decisec(dsec, numbuf);
                uint32_t label_len = 0;
                for (const char *p = numbuf; *p; p++) { label_len++; }
                uint16_t label_w = (uint16_t)(label_len * (5 * GAS_TICK_FONT_SCALE + GAS_TICK_FONT_SCALE));

                uint16_t label_x;
                if (k == 0)
                {
                    label_x = graph_left;
                }
                else if (k == 2)
                {
                    label_x = (uint16_t)((graph_right > label_w) ? (graph_right - label_w) : graph_left);
                }
                else
                {
                    label_x = (uint16_t)((gx > label_w / 2) ? (gx - label_w / 2) : graph_left);
                }
                glcdc_draw_text_s(label_x, axis_label_top, numbuf, GLCDC_COLOR_BLACK, GAS_TICK_FONT_SCALE);
            }
        }
    }

    /* 折れ線グラフ本体: 「現在たまっているサンプル数(window_count)」を
     * グラフ領域(graph_left〜graph_right)の時間軸(x)に均等割り付けし、
     * 隣り合うサンプル同士を線でつなぐ。起動直後でサンプルがまだ少ない間は、
     * その時点での経過時間(window_count分)だけでグラフ幅いっぱいに描画される
     * (PC側と同じ、経過時間に応じて横軸が自動的に拡大していく表示)。
     * ウィンドウが満杯(GAS_WINDOW_SAMPLES)になった後は、直近GAS_WINDOW_MS分の
     * 固定幅スライディングウィンドウとして描画される。
     *
     * グラフ内部を毎フレーム白でクリアしてから描き直すと、クリアした瞬間の
     * "白フラッシュ"がちらつきの原因になる。そのため、前フレームで実際に
     * 線が通っていた座標を記憶しておき、まずそこだけを白で塗って消してから、
     * 新しい線を描く(触っていない部分は一切いじらない)。 */
    {
        static uint16_t s_prev_px[GAS_CH_COUNT_DISPLAY][GAS_WINDOW_SAMPLES];
        static int32_t  s_prev_py[GAS_CH_COUNT_DISPLAY][GAS_WINDOW_SAMPLES];
        static uint16_t s_prev_count[GAS_CH_COUNT_DISPLAY];

        for (uint16_t i = 1; i < s_prev_count[ch]; i++)
        {
            glcdc_draw_line(s_prev_px[ch][i - 1], s_prev_py[ch][i - 1],
                             s_prev_px[ch][i], s_prev_py[ch][i], GLCDC_COLOR_WHITE);
        }

        uint16_t graph_w = (uint16_t)(graph_right - graph_left);
        uint16_t prev_x = graph_left;
        int32_t prev_y = 0;
        for (uint16_t i = 0; i < window_count; i++)
        {
            uint16_t idx = (uint16_t)((window_start + i) % GAS_HISTORY_LEN);
            int32_t v = g_gas_history[ch][idx];
            uint16_t px = (uint16_t)(graph_left + (uint32_t)i * (graph_w - 1) / (window_count - 1));
            int32_t py = graph_bottom - (int32_t)((uint32_t)(v - min_v) * GLCDC_PANEL_GRAPH_HEIGHT / (uint32_t)range);

            if (i > 0)
            {
                glcdc_draw_line(prev_x, prev_y, px, py, color);
            }
            prev_x = px;
            prev_y = py;

            s_prev_px[ch][i] = px;
            s_prev_py[ch][i] = py;
        }
        s_prev_count[ch] = window_count;
    }

    /* 折れ線は太さ(GAS_LINE_THICK)ぶんオーバーシュートして塗られるため、
     * 直近サンプル(グラフ右端付近)の描画で外枠(特に右の縦線)にチャンネルの色が
     * 被ってしまうことがある。外枠を黒で描き直して常に上に来るようにする
     * (位置・色は毎回同じなので、この上書き自体はちらつきの原因にはならない)。 */
    glcdc_draw_border(graph_left, graph_top, graph_right, graph_bottom, GLCDC_COLOR_BLACK);
}

#define GAS_STATUS_FONT_SCALE (5)
#define GAS_STATUS_TOP         (30)

/* NETステータス(ESP-01S/ntfy.sh)表示用。1行目(READY/WAIT)と同じ行の右端に
 * 小さく表示する。デバッグ用UARTを持たないCPU1にとって、AT疎通状況を
 * 目視確認できる唯一の手段を兼ねる。 */
#define GAS_NET_FONT_SCALE (2)
#define GAS_NET_TEXT_W      (200)

#define GAS_LINE2_FONT_SCALE  (3)
#define GAS_LINE2_TOP         (uint16_t)(GAS_STATUS_TOP + 7 * GAS_STATUS_FONT_SCALE + 6)
#define GAS_LINE3_TOP         (uint16_t)(GAS_LINE2_TOP + 7 * GAS_LINE2_FONT_SCALE + 4)
#define GAS_STATUS_BLOCK_BOTTOM (uint16_t)(GAS_LINE3_TOP + 7 * GAS_LINE2_FONT_SCALE + 4)

/* GAS_LINE2_FONT_SCALEで中央寄せした1行を描画する共通ヘルパー。
 * out_x/out_wにNULL以外を渡すと、キラキラアイコン等の追加装飾を配置できるよう
 * 実際に描画したテキストの左端x座標・幅を書き戻す。 */
static void glcdc_draw_centered_line_ex(uint16_t y, const char *text, uint32_t color,
                                         uint16_t *out_x, uint16_t *out_w)
{
    uint32_t len = 0;
    for (const char *p = text; *p; p++) { len++; }
    uint16_t char_step = (uint16_t)(5 * GAS_LINE2_FONT_SCALE + GAS_LINE2_FONT_SCALE);
    uint16_t text_w = (uint16_t)(len * char_step);
    uint16_t text_x = (uint16_t)((g_lcd_hsize > text_w) ? (g_lcd_hsize - text_w) / 2 : 0);
    glcdc_draw_text_s(text_x, y, text, color, GAS_LINE2_FONT_SCALE);
    if (out_x != NULL) { *out_x = text_x; }
    if (out_w != NULL) { *out_w = text_w; }
}

static void glcdc_draw_centered_line(uint16_t y, const char *text, uint32_t color)
{
    glcdc_draw_centered_line_ex(y, text, color, NULL, NULL);
}

/* 絵文字的な「キラキラ」アイコンを(cx,cy)を中心に描く(十字+斜め十字の8方向)。
 * アルコール検出+スプレー検出が同時に成立した、一番目立たせたい瞬間の演出用。 */
static void glcdc_draw_sparkle(uint16_t cx, int32_t cy, uint16_t r, uint32_t color)
{
    glcdc_fill_rect((uint16_t)(cx - r), (uint16_t)(cy - 1), (uint16_t)(cx + r), (uint16_t)(cy + 2), color);
    glcdc_fill_rect((uint16_t)(cx - 1), (uint16_t)(cy - r), (uint16_t)(cx + 2), (uint16_t)(cy + r), color);

    uint16_t d = (uint16_t)((uint32_t)r * 707 / 1000); /* r * cos45° */
    glcdc_draw_line((uint16_t)(cx - d), cy - (int32_t)d, (uint16_t)(cx + d), cy + (int32_t)d, color);
    glcdc_draw_line((uint16_t)(cx - d), cy + (int32_t)d, (uint16_t)(cx + d), cy - (int32_t)d, color);
}

/* 画面上部に3行のステータスを表示する(参照PC側 gas_audio_realtime_plot_v8.py の
 * ready_text/gas_text_obj/audio_text_objに相当):
 *   1行目: いずれかのチャンネルが立上り中か(READY/WAIT)
 *   2行目: ガス推論の進行状況(収集中の残り秒数、または直近の結果を一定時間表示)
 *   3行目: 音響(スプレー音)推論の進行状況・結果 */
static void glcdc_draw_status_bar(void)
{
    /* 3行それぞれ、前回描画時から内容が変わった行だけをクリア&再描画する
     * (READY/WAIT等、値が変わらない間はそのまま触らないことで、ちらつきを防ぐ)。
     * 初回呼び出し時は必ず全行描画されるよう、あり得ない値をセンチネルにする。 */
    static uint32_t s_prev_any_rising    = 0xFFFFFFFFu;
    static uint32_t s_prev_line2_state   = 0xFFFFFFFFu;
    static uint32_t s_prev_line2_seconds = 0xFFFFFFFFu;
    static uint32_t s_prev_line3_state   = 0xFFFFFFFFu;
    static uint32_t s_prev_line3_percent = 0xFFFFFFFFu;
    static uint32_t s_prev_warming_up    = 0xFFFFFFFFu;
    static uint32_t s_prev_net_status    = 0xFFFFFFFFu;
    static uint8_t  s_blink_phase        = 0;

    uint32_t any_rising = g_gas_ipc.any_rising;
    uint8_t  line1_redrawn = 0;

    /* ガス側=ALCOHOL DETECTED(state 2)と音響側=SPRAY DETECTED(state 2)が
     * 同時に成立している間(=どちらの根拠からも「本物」らしいと言える一番
     * 目立たせたい瞬間)。1行目(READY/WAIT)は状態表示そのものなので触らず、
     * 実際に検出結果を表している2・3行目の方を派手に演出する。 */
    uint8_t combo_alert = (g_gas_ipc.gas_line2_state == 2) && (g_gas_ipc.audio_state == 2);

    if (any_rising != s_prev_any_rising)
    {
        s_prev_any_rising = any_rising;
        line1_redrawn = 1;

        glcdc_fill_rect(0, 0, g_lcd_hsize, GAS_LINE2_TOP, GLCDC_COLOR_WHITE);

        const char *text = any_rising ? "WAIT" : "READY";
        uint32_t color = any_rising ? GLCDC_COLOR_ORANGE : GLCDC_COLOR_GREEN_DEEP;
        uint32_t len = 0;
        for (const char *p = text; *p; p++) { len++; }
        uint16_t char_step = (uint16_t)(5 * GAS_STATUS_FONT_SCALE + GAS_STATUS_FONT_SCALE);
        uint16_t text_w = (uint16_t)(len * char_step);
        uint16_t text_x = (uint16_t)((g_lcd_hsize > text_w) ? (g_lcd_hsize - text_w) / 2 : 0);
        glcdc_draw_text_s(text_x, GAS_STATUS_TOP, text, color, GAS_STATUS_FONT_SCALE);
    }

    /* NETステータス(1行目と同じ行の右端に小さく表示)。1行目自体が
     * 再描画された時(=その行全体が白で塗りつぶされた時)は、変化が
     * なくても一緒に描き直さないと消えたままになる。 */
    uint32_t net_status = (uint32_t)esp_notify_get_status();
    if (line1_redrawn || net_status != s_prev_net_status)
    {
        s_prev_net_status = net_status;

        /* esp_notify.hのesp_notify_status_tと順序を一致させること。 */
        static const char * const net_labels[] = {
            "NET: BOOT", "NET: AT FAIL", "NET: JOINING", "NET: READY",
            "NET: SENDING", "NET: SENT", "NET: JOIN FAIL", "NET: SEND FAIL"
        };
        static const uint32_t net_colors[] = {
            GLCDC_COLOR_BLACK, GLCDC_COLOR_RED, GLCDC_COLOR_ORANGE, GLCDC_COLOR_GREEN_DEEP,
            GLCDC_COLOR_BLACK, GLCDC_COLOR_GREEN_DEEP, GLCDC_COLOR_RED, GLCDC_COLOR_RED
        };
        uint32_t idx = (net_status < 8) ? net_status : 1;
        uint16_t net_x = (uint16_t)(g_lcd_hsize - GAS_NET_TEXT_W);
        uint16_t net_y_bottom = (uint16_t)(GAS_STATUS_TOP + 7 * GAS_NET_FONT_SCALE + 2);

        glcdc_fill_rect(net_x, GAS_STATUS_TOP, g_lcd_hsize, net_y_bottom, GLCDC_COLOR_WHITE);
        glcdc_draw_text_s(net_x, GAS_STATUS_TOP, net_labels[idx], net_colors[idx], GAS_NET_FONT_SCALE);
    }

    uint32_t line2_state = g_gas_ipc.gas_line2_state;
    uint32_t line2_seconds = g_gas_ipc.gas_line2_seconds;
    uint32_t warming_up = g_gas_ipc.warming_up;
    if (combo_alert || line2_state != s_prev_line2_state || line2_seconds != s_prev_line2_seconds
        || warming_up != s_prev_warming_up)
    {
        s_prev_line2_state = line2_state;
        s_prev_line2_seconds = line2_seconds;
        s_prev_warming_up = warming_up;

        uint32_t bg_color = GLCDC_COLOR_WHITE;
        if (combo_alert)
        {
            s_blink_phase = (uint8_t)(s_blink_phase ^ 1);
            bg_color = s_blink_phase ? GLCDC_COLOR_YELLOW : GLCDC_COLOR_WHITE;
        }
        glcdc_fill_rect(0, GAS_LINE2_TOP, g_lcd_hsize, GAS_LINE3_TOP, bg_color);

        char line[32];
        uint32_t pos = 0;
        uint32_t color = GLCDC_COLOR_BLACK;

        if (warming_up)
        {
            /* センサーのヒーターがまだウォームアップ中。起動直後はgas_line2_stateが
             * どうせIDLEなので、この行を丸ごと専有して表示する。 */
            static const char txt[] = "GAS SENSOR HEATING...";
            for (uint32_t i = 0; txt[i] != '\0'; i++) { line[pos++] = txt[i]; }
            color = GLCDC_COLOR_ORANGE;
        }
        else
        {
            if (line2_state != 0) /* GAS_LINE2_IDLE以外は先頭に区分タグを付ける */
            {
                static const char tag[] = "[GAS] ";
                for (uint32_t i = 0; tag[i] != '\0'; i++) { line[pos++] = tag[i]; }
            }

            if (line2_state == 4) /* GAS_LINE2_RISING_DETECTED */
            {
                static const char txt[] = "RISING DETECTED";
                for (uint32_t i = 0; txt[i] != '\0'; i++) { line[pos++] = txt[i]; }
                color = GLCDC_COLOR_RED;
            }
            else if (line2_state == 1) /* GAS_LINE2_COLLECTING */
            {
                static const char prefix[] = "ANALYZING.. (";
                for (uint32_t i = 0; prefix[i] != '\0'; i++) { line[pos++] = prefix[i]; }
                char numbuf[12];
                glcdc_itoa((int32_t)line2_seconds, numbuf);
                for (const char *p = numbuf; *p; p++) { line[pos++] = *p; }
                static const char suffix[] = "S LEFT)";
                for (uint32_t i = 0; suffix[i] != '\0'; i++) { line[pos++] = suffix[i]; }
                color = GLCDC_COLOR_BLACK;
            }
            else if (line2_state == 2) /* GAS_LINE2_RESULT_ALCOHOL */
            {
                static const char txt[] = "ALCOHOL DETECTED";
                for (uint32_t i = 0; txt[i] != '\0'; i++) { line[pos++] = txt[i]; }
                color = GLCDC_COLOR_GREEN_DEEP;
            }
            else if (line2_state == 3) /* GAS_LINE2_RESULT_CLEAR */
            {
                static const char txt[] = "NOT ALCOHOL";
                for (uint32_t i = 0; txt[i] != '\0'; i++) { line[pos++] = txt[i]; }
                color = GLCDC_COLOR_ORANGE;
            }
        } /* !warming_up */

        line[pos] = '\0';
        if (pos > 0)
        {
            uint16_t text_x, text_w;
            glcdc_draw_centered_line_ex(GAS_LINE2_TOP, line, color, &text_x, &text_w);

            if (combo_alert)
            {
                int32_t icon_cy = GAS_LINE2_TOP + 7 * GAS_LINE2_FONT_SCALE / 2;
                uint16_t icon_r = 10;
                uint16_t icon_gap = 18;
                if (text_x > (uint16_t)(icon_r + icon_gap))
                {
                    glcdc_draw_sparkle((uint16_t)(text_x - icon_gap), icon_cy, icon_r, GLCDC_COLOR_RED);
                }
                glcdc_draw_sparkle((uint16_t)(text_x + text_w + icon_gap), icon_cy, icon_r, GLCDC_COLOR_RED);
            }
        }
    }

    uint32_t line3_state = g_gas_ipc.audio_state;
    uint32_t line3_percent = g_gas_ipc.audio_percent;
    if (combo_alert || line3_state != s_prev_line3_state || line3_percent != s_prev_line3_percent)
    {
        s_prev_line3_state = line3_state;
        s_prev_line3_percent = line3_percent;

        uint32_t bg_color3 = GLCDC_COLOR_WHITE;
        if (combo_alert)
        {
            bg_color3 = s_blink_phase ? GLCDC_COLOR_YELLOW : GLCDC_COLOR_WHITE;
        }
        glcdc_fill_rect(0, GAS_LINE3_TOP, g_lcd_hsize, GAS_STATUS_BLOCK_BOTTOM, bg_color3);

        char line[32];
        uint32_t pos = 0;
        uint32_t color = GLCDC_COLOR_BLACK;

        if (line3_state != 0) /* AUDIO_STATUS_IDLE以外は先頭に区分タグを付ける */
        {
            static const char tag[] = "[AUDIO] ";
            for (uint32_t i = 0; tag[i] != '\0'; i++) { line[pos++] = tag[i]; }
        }

        if (line3_state == 1) /* AUDIO_STATUS_ANALYZING */
        {
            static const char txt[] = "ANALYZING...";
            for (uint32_t i = 0; txt[i] != '\0'; i++) { line[pos++] = txt[i]; }
            color = GLCDC_COLOR_BLACK;
        }
        else if (line3_state == 2) /* AUDIO_STATUS_RESULT_SPRAY */
        {
            static const char prefix[] = "SPRAY DETECTED ";
            for (uint32_t i = 0; prefix[i] != '\0'; i++) { line[pos++] = prefix[i]; }
            char numbuf[12];
            glcdc_itoa((int32_t)line3_percent, numbuf);
            for (const char *p = numbuf; *p; p++) { line[pos++] = *p; }
            line[pos++] = '%';
            color = GLCDC_COLOR_GREEN_DEEP;
        }
        else if (line3_state == 3) /* AUDIO_STATUS_RESULT_NONE */
        {
            static const char prefix[] = "NO SPRAY ";
            for (uint32_t i = 0; prefix[i] != '\0'; i++) { line[pos++] = prefix[i]; }
            char numbuf[12];
            glcdc_itoa((int32_t)line3_percent, numbuf);
            for (const char *p = numbuf; *p; p++) { line[pos++] = *p; }
            line[pos++] = '%';
            color = GLCDC_COLOR_ORANGE;
        }
        line[pos] = '\0';
        if (pos > 0)
        {
            uint16_t text_x, text_w;
            glcdc_draw_centered_line_ex(GAS_LINE3_TOP, line, color, &text_x, &text_w);

            if (combo_alert)
            {
                int32_t icon_cy = GAS_LINE3_TOP + 7 * GAS_LINE2_FONT_SCALE / 2;
                uint16_t icon_r = 10;
                uint16_t icon_gap = 18;
                if (text_x > (uint16_t)(icon_r + icon_gap))
                {
                    glcdc_draw_sparkle((uint16_t)(text_x - icon_gap), icon_cy, icon_r, GLCDC_COLOR_RED);
                }
                glcdc_draw_sparkle((uint16_t)(text_x + text_w + icon_gap), icon_cy, icon_r, GLCDC_COLOR_RED);
            }
        }
    }
}

/* 4パネル分の静的要素(チャンネル名・外枠・目盛りマーク・軸タイトル)を1回だけ描画する。
 * task_display起動時に、データの有無に関わらず(=最初のガス値が届く前でも)
 * すぐに表示されるよう、メインループに入る前に1回だけ呼び出す。 */
static void glcdc_draw_all_panels_static(void)
{
    static const char *names[GAS_CH_COUNT_DISPLAY] = { "NO2", "C2H5CH", "VOC", "CO" };
    uint16_t usable_w = (uint16_t)(g_lcd_hsize - GAS_BAR_LEFT_MARGIN - GAS_BAR_RIGHT_MARGIN
                                    - (GLCDC_PANEL_COUNT - 1) * GLCDC_PANEL_GAP);
    uint16_t panel_w = (uint16_t)(usable_w / GLCDC_PANEL_COUNT);

    for (int ch = 0; ch < GAS_CH_COUNT_DISPLAY; ch++)
    {
        uint16_t panel_x1 = (uint16_t)(GAS_BAR_LEFT_MARGIN + ch * (panel_w + GLCDC_PANEL_GAP));
        glcdc_draw_channel_panel_static(panel_x1, panel_w, names[ch]);
    }
}

/* CPU0から共有メモリ経由で受け取った実データで、4パネルを描画する(1フレーム分)。
 * CPU0のI2Cに対して、GLCDCの継続的なフレームバッファ書き込み+コア間共有メモリの
 * 読み書き自体が影響するかを検証する(疑似データでの検証はこの前段で完了済み)。 */
static void glcdc_draw_gas_frame(void)
{
    static const uint32_t colors[GAS_CH_COUNT_DISPLAY] = {
        GLCDC_COLOR_RED, GLCDC_COLOR_GREEN_DEEP, GLCDC_COLOR_BLUE, GLCDC_COLOR_ORANGE,
    };

    glcdc_draw_status_bar();

    glcdc_sample_gas_history();

    if (g_gas_history_count == 0)
    {
        return;
    }

    /* 直近GAS_WINDOW_MS分(GAS_WINDOW_SAMPLES件)だけを表示対象にする */
    uint16_t window_count = (g_gas_history_count < GAS_WINDOW_SAMPLES) ? g_gas_history_count : GAS_WINDOW_SAMPLES;
    uint16_t window_start = (uint16_t)((g_gas_history_idx + GAS_HISTORY_LEN - window_count) % GAS_HISTORY_LEN);

    uint16_t usable_w = (uint16_t)(g_lcd_hsize - GAS_BAR_LEFT_MARGIN - GAS_BAR_RIGHT_MARGIN
                                    - (GLCDC_PANEL_COUNT - 1) * GLCDC_PANEL_GAP);
    uint16_t panel_w = (uint16_t)(usable_w / GLCDC_PANEL_COUNT);

    for (int ch = 0; ch < GAS_CH_COUNT_DISPLAY; ch++)
    {
        uint16_t panel_x1 = (uint16_t)(GAS_BAR_LEFT_MARGIN + ch * (panel_w + GLCDC_PANEL_GAP));
        glcdc_draw_channel_panel_dynamic(ch, panel_x1, panel_w, colors[ch], window_count, window_start);
    }
}

static void glcdc_display_init(void)
{
    /* DISP_RESETをHIGH(リセット解除)にする前に、電源/パネル側が安定するまで
     * 少し待つ。CPU0側の静的設定(LOW)とCPU1側のこの上書き(HIGH)の相対
     * タイミングは、電源の入れ方(コールドブート/デバッガ経由の起動)によって
     * ブレることがあり、これが原因でパネルのリセットが正しく解除されず
     * 白画面のままになるケースがあるための保険。 */
    R_BSP_SoftwareDelay(50, BSP_DELAY_UNITS_MILLISECONDS);

    /* DISP_RESETをHIGH(リセット解除)にする。CPU0側の静的設定(LOW)を
     * ここで明示的に上書きする。 */
    R_BSP_PinAccessEnable();
    R_IOPORT_PinCfg(&g_ioport_ctrl, GLCDC_DISP_RESET_PIN,
                     (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT | (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH);
    R_BSP_PinAccessDisable();

    /* GLCDCが安定するまでバックライトはOFFにしておく。
     * リセット解除後、実際に映像を出し始めるまでの待ち時間を200ms→500msに
     * 延ばし、パネル側の安定待ちマージンを増やす。 */
    R_BSP_PinAccessEnable();
    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_DISP_BLEN_PIN, BSP_IO_LEVEL_LOW);
    R_BSP_PinAccessDisable();
    R_BSP_SoftwareDelay(500, BSP_DELAY_UNITS_MILLISECONDS);

    g_lcd_hsize = (uint16_t)g_display0_cfg.input[0].hsize;
    g_lcd_vsize = (uint16_t)g_display0_cfg.input[0].vsize;

    /* r_glcdc.c内部では input[0].hstride を「ピクセル単位」として扱い、
     * (hstride * bit_size) / 8 で実バイトストライドを自前計算している
     * (r_glcdc.c 1468行目等で確認済み)。そのため g_display0_cfg.input[0].hstride
     * (FSP生成値、ピクセル単位)はそのままR_GLCDC_Openに渡してよく、パッチ不要。
     * 一方、自前のfill_rectはバイト単位でアドレス計算するので、
     * ここではhstride(ピクセル)×バイト/ピクセルで実バイトストライドに変換して使う。 */
    g_lcd_hstride = (uint32_t)g_display0_cfg.input[0].hstride * GLCDC_BYTES_PER_PIXEL;
    g_lcd_buffer_size = (uint32_t)(g_lcd_hstride * g_lcd_vsize);
    g_lcd_single_buf = (uint8_t *)g_display0_cfg.input[0].p_base;
    g_lcd_double_buf = g_lcd_single_buf + g_lcd_buffer_size;

    fsp_err_t err = R_GLCDC_Open(&g_display0_ctrl, &g_display0_cfg);
    if (err != FSP_SUCCESS)
    {
        while(1) { R_BSP_SoftwareDelay(1000, BSP_DELAY_UNITS_MILLISECONDS); }
    }

    err = R_GLCDC_Start(&g_display0_ctrl);
    if (err != FSP_SUCCESS)
    {
        while(1) { R_BSP_SoftwareDelay(1000, BSP_DELAY_UNITS_MILLISECONDS); }
    }

    glcdc_color_band_display();

    /* GLCDCが数フレーム分安定して描画してから、ようやくバックライトをONにする */
    R_BSP_SoftwareDelay(1000, BSP_DELAY_UNITS_MILLISECONDS);

    R_BSP_PinAccessEnable();
    R_IOPORT_PinWrite(&g_ioport_ctrl, GLCDC_DISP_BLEN_PIN, BSP_IO_LEVEL_HIGH);
    R_BSP_PinAccessDisable();
}

/* GLCDC初期化+継続描画ループをμT-Kernelタスクとして行う(Phase 2-B)。 */
/* 基板上のユーザーLED3個を状態表示に使う。LED1=P600=青、LED2=P303=緑、LED3=PA07=赤
 * (ピン番号はra/board/ra8p1_ek/board_leds.cのg_bsp_prv_leds[]と同じもの。
 * このプロジェクトの include パスには ra/board/ra8p1_ek が含まれておらず
 * board_leds.h をインクルードできないため、ピン定数を直接使う)。
 *   WAIT(any_rising)  : 青点灯
 *   READY              : 緑点灯
 *   アルコール確定+スプレー音検知(コンボ): 赤点滅、他は消灯
 *
 * 実機の配線極性(アクティブHIGH/LOW)を確認していないため、点灯方向が
 * 逆だった場合はLED_ACTIVE_LEVELをBSP_IO_LEVEL_LOWに変更するだけでよい。 */
#define LED_ACTIVE_LEVEL BSP_IO_LEVEL_HIGH
#define LED_BLUE  BSP_IO_PORT_06_PIN_00
#define LED_GREEN BSP_IO_PORT_03_PIN_03
#define LED_RED   BSP_IO_PORT_10_PIN_07

static void led_init(void)
{
    R_BSP_PinAccessEnable();
    R_IOPORT_PinCfg(&g_ioport_ctrl, LED_BLUE,  IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    R_IOPORT_PinCfg(&g_ioport_ctrl, LED_GREEN, IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    R_IOPORT_PinCfg(&g_ioport_ctrl, LED_RED,   IOPORT_CFG_PORT_DIRECTION_OUTPUT | IOPORT_CFG_PORT_OUTPUT_LOW);
    R_BSP_PinAccessDisable();
}

static void led_set(bsp_io_port_pin_t led, uint8_t on)
{
    bsp_io_level_t off_level = (LED_ACTIVE_LEVEL == BSP_IO_LEVEL_HIGH) ? BSP_IO_LEVEL_LOW : BSP_IO_LEVEL_HIGH;
    bsp_io_level_t level = on ? LED_ACTIVE_LEVEL : off_level;
    R_IOPORT_PinWrite(&g_ioport_ctrl, led, level);
}

/* task_displayのループから毎フレーム(200ms周期)呼ぶ。赤点滅はこの呼び出し周期
 * そのものを点滅周期(400ms)として使う。 */
static void led_update(void)
{
    static uint8_t s_blink_phase = 0;
    uint8_t combo_alert = (g_gas_ipc.gas_line2_state == 2) && (g_gas_ipc.audio_state == 2);

    if (combo_alert)
    {
        s_blink_phase = (uint8_t)(s_blink_phase ^ 1);
        led_set(LED_RED, s_blink_phase);
        led_set(LED_GREEN, 0);
        led_set(LED_BLUE, 0);
    }
    else if (g_gas_ipc.any_rising)
    {
        s_blink_phase = 0;
        led_set(LED_RED, 0);
        led_set(LED_GREEN, 0);
        led_set(LED_BLUE, 1);
    }
    else
    {
        s_blink_phase = 0;
        led_set(LED_RED, 0);
        led_set(LED_BLUE, 0);
        led_set(LED_GREEN, 1);
    }
}

LOCAL void task_display(INT stacd, void *exinf)
{
    glcdc_display_init();
    led_init();

    /* 動作確認用の縞模様を消して、画面全体を黒にリセットする */
    glcdc_fill_rect(0, 0, g_lcd_hsize, g_lcd_vsize, GLCDC_COLOR_WHITE);

    /* データに依存しない静的要素(チャンネル名・外枠・目盛りマーク・軸タイトル)は
     * ここで1回だけ描画する。毎フレーム消して描き直さないことで、
     * 変化していない要素がちらつくのを防ぐ。 */
    glcdc_draw_all_panels_static();

    /* CPU0が共有メモリ(g_gas_ipc)へ書き込む実データで、4パネルのグラフを
     * 継続的に再描画する。 */
    while (1)
    {
        glcdc_draw_gas_frame();
        led_update();
        tk_dly_tsk(GAS_SAMPLE_INTERVAL_MS);
    }
}

LOCAL T_CTSK ctsk_display =
{
    .itskpri = 12,
    .stksz   = 4096,
    .task    = task_display,
    .tskatr  = TA_HLNG | TA_RNG3,
};

/* app_main.c の usermain() から呼ばれる、表示タスクの起動用エントリ。 */
EXPORT void display_task_start(void)
{
    tk_sta_tsk(tk_cre_tsk(&ctsk_display), 0);
}

/* ESP-01S経由でntfy.shへプッシュ通知を送るタスク(ベストエフォート、
 * task_displayより優先度を低くして描画タイミングに影響させない)。
 *
 * ガス側ALCOHOL確定(gas_line2_state==2)の立ち上がりで「音声側の結果待ち」
 * 状態に入り、音声側が結果確定(RESULT_SPRAY=2またはRESULT_NONE=3)するまで
 * 待ってから、スプレー音だったかどうかを本文に含めて1回だけ送信する。
 *
 * gas_line2_state自体は結果表示後5秒でIDLEに戻ってしまう(音声側の解析の方が
 * 時間がかかることがある)ため、「待っている」という事実自体を別途ラッチして
 * 保持している(gas_line2_stateがIDLEに戻ってしまっても待ち続ける)。
 *
 * (以前はANALYZING(1)を実際に観測してから結果を受理する安全策を入れていたが、
 * 音声解析が300msのポーリング間隔より速く終わることがあり、その一瞬を
 * 見逃すと二度と条件が満たされなくなるバグがあったため削除した。
 * 前回の結果を誤認するリスクは、ガス側のクールダウン(10秒、
 * GAS_COOLDOWN_SAMPLES)が音声側の結果表示時間より十分長いため
 * 実質的に発生しない。) */
LOCAL void task_net(INT stacd, void *exinf)
{
    esp_notify_init();

    uint8_t prev_gas_alcohol = 0;
    uint8_t waiting_for_audio = 0;

    while (1)
    {
        uint8_t gas_alcohol = (g_gas_ipc.gas_line2_state == 2);

        if (gas_alcohol && !prev_gas_alcohol)
        {
            waiting_for_audio = 1;
        }
        prev_gas_alcohol = gas_alcohol;

        if (waiting_for_audio)
        {
            uint32_t audio_state = g_gas_ipc.audio_state;

            if (audio_state == 2 || audio_state == 3)
            {
                const char *msg = (audio_state == 2) /* AUDIO_STATUS_RESULT_SPRAY */
                    ? "Scentext: ALCOHOL DETECTED (spray sound confirmed)"
                    : "Scentext: ALCOHOL DETECTED (spray sound NOT confirmed)";
                esp_notify_send(msg);
                waiting_for_audio = 0;
            }
        }

        esp_notify_tick();

        tk_dly_tsk(300);
    }
}

LOCAL T_CTSK ctsk_net =
{
    .itskpri = 13,
    .stksz   = 4096,
    .task    = task_net,
    .tskatr  = TA_HLNG | TA_RNG3,
};

/* app_main.c の usermain() から呼ばれる、通信タスクの起動用エントリ。 */
EXPORT void net_task_start(void)
{
    tk_sta_tsk(tk_cre_tsk(&ctsk_net), 0);
}

/*******************************************************************************************************************//**
 * main() is generated by the RA Configuration editor and is used to generate threads if an RTOS is used.  This function
 * is called by main() when no RTOS is used.
 **********************************************************************************************************************/
void hal_entry(void) {
	/* Wake up 2nd core if this is first core and we are inside a multicore project. */
#if (0 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && !BSP_TZ_NONSECURE_BUILD

#if BSP_TZ_SECURE_BUILD
    /* Take semaphore so 2nd core can clear it */
    R_BSP_IpcSemaphoreTake(&g_core_start_semaphore);
#endif

    R_BSP_SecondaryCoreStart();

#if BSP_TZ_SECURE_BUILD
    /* Wait for 2nd core to start and clear semaphore */
    while(FSP_ERR_IN_USE == R_BSP_IpcSemaphoreTake(&g_core_start_semaphore))
    {
        ;
    }
#endif
#endif

#if (1 == _RA_CORE) && (1 == BSP_MULTICORE_PROJECT) && BSP_TZ_SECURE_BUILD
    /* Signal to 1st core that 2nd core has started */
    R_BSP_IpcSemaphoreGive(&g_core_start_semaphore);
#endif

#if BSP_TZ_SECURE_BUILD
    /* Enter non-secure code */
    R_BSP_NonSecureEnter();
#endif

    /* μT-Kernel 3.0を起動する(CPU1自身の独立したカーネルインスタンス)。
     * knl_start_mtkernel()は戻らない。
     * Phase 1-B(骨格起動確認)では usermain() は仮の1タスクのみを起動する。
     * 実際のGLCDC表示(glcdc_display_init()+描画ループ)はPhase 2-Bでタスク化して
     * usermain()から起動する。 */
#if (1 == _RA_CORE)
    void knl_start_mtkernel(void);
    knl_start_mtkernel();
#endif
}

#if BSP_TZ_SECURE_BUILD

FSP_CPP_HEADER
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ();

/* Trustzone Secure Projects require at least one nonsecure callable function in order to build (Remove this if it is not required to build). */
BSP_CMSE_NONSECURE_ENTRY void template_nonsecure_callable ()
{

}
FSP_CPP_FOOTER

#endif

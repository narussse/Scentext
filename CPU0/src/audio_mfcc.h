/*
 * audio_mfcc.h
 *
 *  Created on: 2026/08/02
 *      Author: narus
 */

#ifndef AUDIO_MFCC_H_
#define AUDIO_MFCC_H_

// モデルの入力窓(TOTAL_SAMPLES、MODEL_WINDOW_SECONDS秒分)の生音声バッファから
// CMSIS互換MFCCを計算・推論し、確率(0.0〜1.0)を返す
float run_audio_mfcc_process(int16_t *input_raw_audio);

#endif /* AUDIO_MFCC_H_ */

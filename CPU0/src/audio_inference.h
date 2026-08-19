/*
 * audio_inference.h
 *
 *  Created on: 2026/08/02
 *      Author: narus
 */

#ifndef AUDIO_INFERENCE_H_
#define AUDIO_INFERENCE_H_

#include <stdint.h>

// AIモデルの初期化（起動時に1回呼ぶ）
void audio_inference_init(void);

// MFCCの計算結果（311フレーム × 10次元）を受け取って推論を実行する関数
float audio_run_inference(float (*mfcc_matrix)[311]);

#endif /* AUDIO_INFERENCE_H_ */

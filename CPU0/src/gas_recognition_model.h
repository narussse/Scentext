#ifndef GAS_RECOGNITION_MODEL_H
#define GAS_RECOGNITION_MODEL_H

/*
 * convert_gas_model_to_c.py により自動生成。
 * 元データ: gas_recognition_model.json
 * 学習サンプル数: 46 (アルコール32, 非アルコール14)
 *
 * 特徴量の並び順 (GAS_MODEL_N_FEATURES次元):
 *   [0] NO2_share
 *   [1] C2H5CH_share
 *   [2] VOC_share
 *   [3] NO2_time_rel_CO
 *   [4] C2H5CH_time_rel_CO
 *   [5] VOC_time_rel_CO
 *   [6] log_total_rise
 *
 * 判定方法: gas_model_logit()でlogitを計算し、
 *   logit > 0 なら「アルコール」、そうでなければ「非アルコール」と判定する。
 * (非アルコールの学習データは呼気・コーヒーのみ。センサーへの物理的な
 *  衝撃など未知の外乱は未対応 -> 運用上の回避で対応すること)
 *
 * 特徴量計算の条件(学習時と同じにすること):
 *   立上り検知(onset)から GAS_MODEL_POST_WINDOW_SEC 秒間のデータでピーク値を求める
 *   ベースラインは onset の直前 GAS_MODEL_BASE_WINDOW_SEC 秒間の平均
 */

#define GAS_MODEL_N_FEATURES 7
#define GAS_MODEL_POST_WINDOW_SEC 5.0f
#define GAS_MODEL_BASE_WINDOW_SEC 10.0f

static const float GAS_MODEL_BIAS = -7.1663221891f;

static const float gas_model_weights[GAS_MODEL_N_FEATURES] = {
    -25.7490502611f, -1.6147329815f, 7.2877071825f, -0.7166011378f, 0.6324567298f, 0.9709566592f, 1.8674098590f
};

/* logit = weights・x + bias */
static inline float gas_model_logit(const float x[GAS_MODEL_N_FEATURES]) {
    float logit = GAS_MODEL_BIAS;
    for (int i = 0; i < GAS_MODEL_N_FEATURES; i++) {
        logit += gas_model_weights[i] * x[i];
    }
    return logit;
}

/* logit > 0 なら1(アルコール)、そうでなければ0(非アルコール) */
static inline int gas_model_is_alcohol(const float x[GAS_MODEL_N_FEATURES]) {
    return gas_model_logit(x) > 0.0f;
}

#endif /* GAS_RECOGNITION_MODEL_H */

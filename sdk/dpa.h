/**
 * Dewpoint Advance SDK
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once
#include <stdint.h>

typedef struct {
    int board_id;   // ボードID
    int rank;       // 順位
    int score;      // スコア
    int is_my_rank; // 0: 他人のエントリ, not 0: 自分のエントリ
    char name[16];  // 名前 (注意: ASCII コード以外は '?' になる, 16文字以上は省略される)
} LeaderboardEntry;

/**
 * @brief アチーブメントをアンロック
 * @param id アチーブメントID (\0終端のテキスト)
 */
void dpa_achievement_unlock(const char* id);

/**
 * @brief スコアを送信
 * @param board_id スコアボードID (0~15)
 * @param score スコア
 * @param ugc 0: UGCデータを送信しない, not 0: 現在記録されているUGCデータを送信
 */
void dpa_leaderboard_send(int board_id, int32_t score, int ugc);

/**
 * @brief リーダーボード・エントリを取得可能かチェック
 * @param board_id スコアボードID (0~15)
 * @return 0: Not Ready, not 0: Ready
 */
int dpa_leaderboard_ready(int board_id);

/**
 * @brief リーダーボードの **Top100** エントリを取得
 * @param board_id スコアボードID (0~15)
 * @param index 0〜99の範囲で指定できる（小さい = 高ランク）
 * @param entry エントリー情報（呼び出し元で領域確保）
 * @return 0: 正常なエントリを取得, -1: 取得失敗（エントリが存在しない or not ready）
 */
int dpa_leaderboard_get(int board_id, int index, LeaderboardEntry* entry);

/**
 * @brief リーダーボードの **自分** のエントリを取得
 * @param board_id スコアボードID (0~15)
 * @param entry エントリー情報（呼び出し元で領域確保）
 * @return 0: 正常なエントリを取得, -1: 取得失敗（エントリが存在しない or not ready）
 */
int dpa_leaderboard_getm(int board_id, LeaderboardEntry* entry);

/**
 * @brief UGCデータをクリア
 */
void dpa_ugc_clear(void);

/**
 * @brief UGCデータを追加
 * @param data 追加するデータ（32bit）
 */
void dpa_ugc_append(int32_t data);

/**
 * @brief UGCデータをダウンロード開始リクエスト
 * @param entry ダウンロード対象のリーダーボード・エントリ
 */
void dpa_ugc_download(LeaderboardEntry* entry);

/**
 * @brief UGCデータのサイズチェック
 * @return 0: empty or downloading, 0<: available, -1: error
 * @remark ダウンロード・リクエストをすると 0 になり、完了するとダウンロードサイズが取得できる
 */
int dpa_ugc_size(void);

/**
 * @brief UGCデータを読み込む
 * @return UGCデータ (指定indexにデータが無い場合は -1 を返す)
 */
int32_t dpa_ugc_read(int index);

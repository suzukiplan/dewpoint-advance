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
 * @brief Dewpoint Advance SDK が利用可能かチェックする
 * @return 0: 利用不可, not 0; 利用可
 */
int dpa_is_enabled(void);

/**
 * @brief アプリケーションバージョン文字列（package.confのAppVersion）を取得
 * @param buf 文字列を格納するのに十分なサイズ（strlen(AppVersion)+1）が確保されたバッファ（RAM）
 * @return -1: 失敗, 0: 成功
 */
int dpa_get_app_version(char* buf);

/**
 * @brief アチーブメントをアンロック
 * @param id アチーブメントID (\0終端のテキスト)
 */
void dpa_achievement_unlock(const char* id);

/**
 * @brief スコアを送信
 * @param board_id スコアボードID (0~15)
 * @param score スコア
 * @param ugc 0: UGCデータを送信しない, not 0: 共通UGCバッファのデータを送信
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
 * @brief 共通UGCバッファをクリア
 */
void dpa_ugc_clear(void);

/**
 * @brief 共通UGCバッファにデータを追加
 * @param data 追加するデータ（32bit）
 */
void dpa_ugc_append(int32_t data);

/**
 * @brief UGCデータを共通UGCバッファへダウンロード開始リクエスト
 * @param entry ダウンロード対象のリーダーボード・エントリ
 */
void dpa_ugc_download(LeaderboardEntry* entry);

/**
 * @brief 共通UGCバッファのサイズチェック
 * @return 0: empty or downloading, 0<: available, -1: error
 * @remark ダウンロード・リクエストをすると共通UGCバッファがクリアされ、完了するとダウンロードサイズが取得できる
 */
int dpa_ugc_size(void);

/**
 * @brief 共通UGCバッファからデータを読み込む
 * @return UGCデータ (指定indexにデータが無い場合は -1 を返す)
 */
int32_t dpa_ugc_read(int index);

/**
 * @brief プロセス終了（実機の場合はハングアップ）
 * @param exit_code 終了コード
 */
void dpa_exit(int exit_code);

/**
 * @brief フルスクリーン/Windowの切り替え
 * @param full_screen 0: Window, not 0: Full Screen
 * @return 0: Window, not 0: Full Screen
 */
int dpa_fullscreen_set(int full_screen);

/**
 * @brief フルスクリーン/Windowの状態取得
 * @return 0: Window, not 0: Full Screen
 */
int dpa_fullscreen_get(void);

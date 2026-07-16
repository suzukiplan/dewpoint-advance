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
#include "dpa.h"

#define DPMID (('D' << 24) | ('E' << 16) | ('W' << 8) | 'P')

enum DpaIndex {
    DpaIndexId = 0,        // [I] 有効な場合 DPMID を返す
    DpaIndexAchievement,   // [O] アンロックするアチーブメントのテキストを送信 (\0 を送信時に処理実行)
    DpaIndexSetBoardId,    // [O] スコアを送受信するボードIDを設定
    DpaIndexSetUgcOption,  // [O] スコアを送信時のUGCデータ送信オプション（0: 送信しない, 1: 送信する）
    DpaIndexSendScore,     // [O] スコア送信
    DpaIndexBoardReady,    // [I] リーダーボードが読み込み可能かチェック
    DpaIndexBoardEntry,    // [O] リーダーボード取得領域の先頭ポインタ (RAM)
    DpaIndexBoardEntryGet, // [O] リーダーボード取得 (0〜99: top100, -1: my entry)
    DpaIndexUgcClear,      // [O] UGC データの消去リクエスト
    DpaIndexUgcAppend,     // [O] UGC データの追記リクエスト
    DpaIndexUgcDownload,   // [O] UGC データのダウンロードリクエスト
    DpaIndexUgcSize,       // [I] UGC データのサイズを取得
    DpaIndexUgcReadPtr,    // [O] UGC データの読み込み位置を設定
    DpaIndexUgcRead,       // [I] UGC データの読み込み
    DpaIndexExit,          // [O] プロセス停止 (exit)
    DpaIndexFullScreen,    // [I/O] フルスクリーン (0: Window / not 0: Full Screen)
};

static volatile uint32_t* _dpa = (volatile uint32_t*)0x04801000;

static inline int dpa_is_enabled_internal(void)
{
    return _dpa[DpaIndexId] == DPMID;
}

int dpa_is_enabled(void)
{
    return dpa_is_enabled_internal();
}

void dpa_achievement_unlock(const char* id)
{
    if (!dpa_is_enabled_internal() || !id || 0 == id[0]) {
        return;
    }
    while (*id) {
        _dpa[DpaIndexAchievement] = *id;
        id++;
    }
    _dpa[DpaIndexAchievement] = 0;
}

void dpa_leaderboard_send(int board_id, int32_t score, int ugc)
{
    if (!dpa_is_enabled_internal()) {
        return;
    }
    _dpa[DpaIndexSetBoardId] = (uint32_t)board_id;
    _dpa[DpaIndexSetUgcOption] = ugc ? 1 : 0;
    _dpa[DpaIndexSendScore] = (uint32_t)score;
}

int dpa_leaderboard_ready(int board_id)
{
    if (!dpa_is_enabled_internal()) {
        return 0;
    }
    _dpa[DpaIndexSetBoardId] = (uint32_t)board_id;
    return _dpa[DpaIndexBoardReady] ? 1 : 0;
}

int dpa_leaderboard_get(int board_id, int index, LeaderboardEntry* entry)
{
    if (!dpa_leaderboard_ready(board_id) || !entry) {
        return -1;
    }
    _dpa[DpaIndexBoardEntry] = (uint32_t)entry;
    entry->board_id = -2; // 取得が成功したら board_id と同じ値が設定される
    _dpa[DpaIndexBoardEntryGet] = index;
    return entry->board_id == board_id ? 0 : -1;
}

int dpa_leaderboard_getm(int board_id, LeaderboardEntry* entry)
{
    return dpa_leaderboard_get(board_id, -1, entry);
}

void dpa_ugc_clear(void)
{
    if (!dpa_is_enabled_internal()) {
        return;
    }
    _dpa[DpaIndexUgcClear] = 1;
}

void dpa_ugc_append(int32_t data)
{
    if (!dpa_is_enabled_internal()) {
        return;
    }
    _dpa[DpaIndexUgcAppend] = (uint32_t)data;
}

void dpa_ugc_download(LeaderboardEntry* entry)
{
    if (!dpa_is_enabled_internal()) {
        return;
    }
    _dpa[DpaIndexSetBoardId] = (uint32_t)entry->board_id;
    _dpa[DpaIndexBoardEntry] = (uint32_t)entry;
    _dpa[DpaIndexUgcDownload] = 1;
}

int dpa_ugc_size(void)
{
    if (!dpa_is_enabled_internal()) {
        return -1;
    }
    return (int)_dpa[DpaIndexUgcSize];
}

int32_t dpa_ugc_read(int index)
{
    if (!dpa_is_enabled_internal()) {
        return -1;
    }
    _dpa[DpaIndexUgcReadPtr] = (uint32_t)index;
    return (int32_t)_dpa[DpaIndexUgcRead];
}

void dpa_exit(int exit_code)
{
    if (dpa_is_enabled_internal()) {
        _dpa[DpaIndexExit] = (uint32_t)exit_code;
    }
    for (;;) {
    }
}

int dpa_fullscreen_set(int full_screen)
{
    if (!dpa_is_enabled_internal()) {
        return 0;
    }
    _dpa[DpaIndexFullScreen] = full_screen ? 1 : 0;
    return dpa_fullscreen_get();
}

int dpa_fullscreen_get(void)
{
    if (!dpa_is_enabled_internal()) {
        return 0;
    }
    return _dpa[DpaIndexFullScreen] ? 1 : 0;
}

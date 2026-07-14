# Steam Advance SDK **<WIP>**

このリポジトリは、自作の GBA ソフトを Steam で頒布する目的に特化したエミュレータフロントエンドです。

エミュレータコアには mGBA を用いています。

そして、GBA ソフトで Steamworks SDK の Leaderboard と Achievement やアプリ終了機能のインタフェースを提供する SDK を提供します。

> なお、本SDKのAPIを用いた GBA ソフトは実機 GBA 上では動作できません。

**「既存GBAソフトを動かす」のではなく「新規で開発するGBAソフト」がターゲット** です。

## WIP status

- [x] macOS build (macOS + SDL2 で GBA のゲームを動かす)
- [ ] Linux build (Linux + SDL2 で GBA のゲームを動かす)
- [ ] Windows build (Windows + DirectX で GBA のゲームを動かす)
- [ ] Replay API (GBA上で利用できるリプレイデータを保持/読み込みできるAPI)
- [ ] Steam Achievement API for GBA (GBA上で利用できるアチーブメント・アンロックAPI)
- [ ] Steam Leaderboard API for GBA (GBA上で利用できるリーダーボード送信/受信API)
- [ ] ライセンス精査

## How to Test

### Build

```bash
git clone https://github.com/suzukiplan/mgba-steam
cd mgba-steam
make
```

### Execute

```bash
./game /path/to/game.gba
```

- カーソルキー: D-pad
- Z: Bボタン
- X: Aボタン
- A: Lボタン
- S: Rボタン
- Esc: Selectボタン
- Space: Startボタン
- ⌘+R: リセット
- ⌘+Q: 電源OFF

## How to make the Package

todo

## OSS Licenses

[./src](./src) ディレクトリ以下のソースコードは [MIT](./LICENSE.txt) ですが、最終的な成果物には次のライセンスが含まれます。

- mGBA: [Mozilla Public License Version 2.0](./mgba/LICENSE)
- [inih](https://github.com/benhoyt/inih)
  - Copyright © 2009 – 2020 Ben Hoyt
  - License: BSD 3-clause
- [SDL2](https://www.libsdl.org/)
  - Copyright (C) 1997-2025 Sam Lantinga slouken@libsdl.org
  - License: ZLIB
- [Steam Advance SDK](https://github.com/suzukiplan/mgba-steam)
  - Copyright © 2026 SUZUKI PLAN
  - License: [MIT](./LICENSE.txt)

mGBA 作者からソースコードの開示を求められた場合、[本リポジトリ](https://github.com/suzukiplan/mgba-steam)を案内してください。

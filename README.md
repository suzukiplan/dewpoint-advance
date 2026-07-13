# Steam Advance SDK **<WIP>**

このリポジトリは、自作の GBA ソフトを Steam で頒布する目的に特化したエミュレータフロントエンドです。

エミュレータコアには mGBA を用いています。

そして、GBA ソフトで Steamworks SDK の Leaderboard と Achievement やアプリ終了機能のインタフェースを提供する API を提供します。

> なお、本APIを用いた GBA ソフトは実機 GBA 上では動作できません。

**「既存GBAソフトを動かす」のではなく「新規で開発するGBAソフト」がターゲット** です。

## How to Build

> 現在、暫定的に macOS でのみビルドできます。
>
> ※ Linux と Windows のビルドにも対応予定

```bash
git clone https://github.com/suzukiplan/mgba-steam
cd mgba-steam
make
```

## How to Use

```bash
./sdlmain /path/to/game.gba
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

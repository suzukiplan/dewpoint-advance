# **WIP:** Dewpoint Advance

Dewpoint Advance (DPA) は、**自作の GBA ソフト** を Steam で頒布する目的に特化したエミュレータフロントエンドです。

エミュレータコアには mGBA を用いています。

そして、GBA ソフトで Steamworks SDK（Leaderboard や Achievement など）と連携する機能、アプリ制御機能のインタフェースを提供する SDK を提供します。

現状、Steam上では過去のコンシューマゲーム機向けソフトをエミュレータで動かすものが多くありますが、それらは in-game と UI を分離する実装になっていることが多いです。しかし、DPAを用いることで「in-game のみでシームレスなプラットフォーム連携」が実現できます。（out-game UI を実装する必要がなくなり生産性とUXに寄与）

```mermaid
flowchart
    subgraph GBA["GBA ROM（Closed Source）"]
        GAME["GBA Game"]
        SDK["Dewpoint Advance SDK<br/>《component》"]

        GAME -->|"Achievement / Leaderboard API"| SDK
    end

    subgraph HOST["Host Application"]
        RUNTIME["Dewpoint Runtime<br/>《component》"]
        MGBA["mGBA<br/>《component》"]
        STEAMWORKS["Steamworks SDK<br/>《external component》"]

        RUNTIME -->|"Emulation API"| MGBA
        RUNTIME -->|"Steam API calls"| STEAMWORKS
    end

    STEAM["Steam Services<br/>《external system》"]

    SDK -.->|"Dewpoint Bridge<br/>Custom GBA Interface"| RUNTIME
    MGBA -->|"Loads and executes"| GAME
    STEAMWORKS -->|"Achievements / Leaderboards"| STEAM
```

なお、本SDKのAPIを用いた GBA ソフトは実機 GBA 上では動作できません。

**本SDKは「既存GBAソフトを動かすこと」が目的ではありません。**

**新規で開発するGBAソフト** をSteamで手軽に配信したいデベロッパー/パブリッシャー向けの SDK です。

**重要な補足事項:**

- GBA、Game Boy および Game Boy Advance は任天堂の日本またはその他地域における登録商標です。ゲーム名に「for XXX」等をつけたい場合は任天堂から許諾を得てください。（商品名には他者/他社の登録商標を無許諾でつけることができないのでご注意ください）
- Game Boy または Game Boy Advance の BIOS 機能（MP2k等）は使用しないでください。
- ROM ファイルに Game Boy または Game Boy Advance のヘッダー画像に任天堂が商標権や意匠権を有するデータは配信データ（埋め込み用データ）生成時にゼロ化しています。

## WIP status

- [x] macOS Runtime (macOS + SDL2 で GBA のゲームを動かす)
- [x] Linux Runtime (Linux + SDL2 で GBA のゲームを動かす)
- [x] Windows Runtime (Windows + DirectX で GBA のゲームを動かす)
- [x] SDK: Replay API for GBA (GBA上で利用できるリプレイデータを保持/読み込みできるAPI)
- [x] SDK: Achievement API for GBA (GBA上で利用できるアチーブメント・アンロックAPI)
- [x] SDK: Leaderboard API for GBA (GBA上で利用できるリーダーボード送信/受信API)
- [x] パッケージ作成手順の実装 (Windows)
- [x] パッケージ作成手順の実装 (macOS)
- [x] パッケージ作成手順の実装 (Linux)
- [x] ライセンス精査
- [x] リポジトリのpublic化
- [ ] システムテスト: Battle Marine Advance (Windows/macOS/Linux) を Steam で配信

## How to Use

### Prerequisites

1. Valve と Steam 配信契約を締結
2. Steamworks SDK を入手
3. [./steamworks](./steamworks/) ディレクトリ以下に `public` および `redistributable_bin` ディレクトリを配置

### Build for Test (macOS/Linux)

```bash
git clone https://github.com/suzukiplan/dewpoint-advance
cd dewpoint-advance
cp package.conf.model package.conf
make
```

### Build for Test (Windows)

Windows では Visual Studio の **x86 Native Tools Command Prompt** で次を実行します。
`make.bat` は tools を CL/NMAKE でビルドし、設定ファイルと
`Makefile.Windows` を生成してから、残りの引数を NMAKE に渡します。

```bat
git clone https://github.com/suzukiplan/dewpoint-advance
cd dewpoint-advance
copy package.conf.model package.conf
make.bat
```

クリーンビルドと配布用 zip の作成は次のとおりです。

```bat
make.bat clean all
make.bat package
```

Windows ランタイムは Direct3D 9 と DirectSound 8 を使用し、SDL2 には依存しません。
実行ログは標準出力へは出さず、Steam経由ではインストール先、それ以外ではカレントフォルダの `log.txt` に記録します。

### Execute

- Windows: `game.exe`
- macOS: `AppName.app`
- Linux: `game`

キーボード操作の割り当ては次の通りです:

- カーソルキー: D-pad
- Z: Bボタン
- X: Aボタン
- A: Lボタン
- S: Rボタン
- Esc: Selectボタン
- Space: Startボタン
- macOS/Linux: ⌘+R でリセット、⌘+Q で電源OFF
- Windows: Ctrl+R でリセット、Ctrl+P で一時停止、F11 または Alt+Enter でフルスクリーン切り替え

## Dewpoint SDK

- [devkitPro](https://github.com/devkitPro/) で作成したGBAのプロジェクトに利用できます。
- ソースコードディレクトリに [./sdk/](./sdk/) 以下のファイルをコピーしてください。
- `#include "dpa.h"` で使用できます。

| API | Description |
|:----|:------------|
| `dpa_is_enabled` | Dewpoint Advance SDK が利用できるかチェック |
| `dpa_get_app_version` | アプリバージョン（AppVersion）の文字列を取得 |
| `dpa_button_a` | Aボタンの文字コード (PC: `'X'`, XB/SW: `'A'`, PS: `'X'`) |
| `dpa_button_b` | Bボタンの文字コード (PC: `'Z'`, XB/SW: `'B'`, PS: `'O'`) |
| `dpa_achievement_unlock` | アチーブメントをアンロック |
| `dpa_leaderboard_send` | スコアを送信 |
| `dpa_leaderboard_ready` | リーダーボードからエントリが取得可能か確認 |
| `dpa_leaderboard_get` | リーダーボードからTop100のエントリを取得 |
| `dpa_leaderboard_getm` | リーダーボードから自分のエントリを取得 |
| `dpa_ugc_clear` | 共通 UGC バッファをクリア |
| `dpa_ugc_append` | 共通 UGC バッファに 4bytes のデータを追加 |
| `dpa_ugc_download` | 共通 UGC バッファへのダウンロードを開始 |
| `dpa_ugc_size` | 共通 UGC バッファのサイズを取得 |
| `dpa_ugc_read` | 共通 UGC バッファから 4bytes 読み込む |
| `dpa_fullscreen_set` | フルスクリーン / ウィンドウの切り替え|
| `dpa_fullscreen_get` | フルスクリーン / ウィンドウの状態取得|
| `dpa_exit` | プロセス停止（実機ではハングアップ）|

詳細な仕様は [./sdk/dpa.h](./sdk/dpa.h) の実装をチェックしてください。

> devkitPro を用いた GBA ソフト開発に便利な [SDK](https://github.com/suzukiplan/gbasdk) も別途公開しています。

## Steamworks Settings

### Steam Cloud

Steamworks 設定の「アプリケーション」→「Steam クラウド」に次の設定をしてください:

__Steam クラウド設定:__

- ユーザーごとのバイトクォータ: `4194304`
- ユーザーごとに許可されるファイル数: `32`

上記設定基準は参考です。

- Dewpoint Advance では、save.dat（SRAM/Flash/EEPROM）、config.dat（ウィンドウ状態）、リプレイx16ボードで最大18ファイルをSteamクラウドに保存しますが、余裕をもって32個に設定しておけば安心です。
- リプレイのデータサイズは 1フレーム 4bytes で 60分（216,000フレーム）記録する場合、最大 864,000 バイトと計算できます。（※Dewpoint Advance SDKのリプレイAPIはネイティブメモリに記録されるためGBA側のRAMを使いません）

__Steam 自動クラウド設定:__

- `save.dat` (SRAM/Flash/EEPROM)
  - ルート: `アプリのインストールディレクトリ`
  - サブディレクトリ: `save`
  - パターン: `save.dat`
  - OS: `全てのOS`
- `config.dat` (ウィンドウモード、ウィンドウサイズ、ウィンドウ位置など)
  - ルート: `アプリのインストールディレクトリ`
  - サブディレクトリ: `save`
  - パターン: `config.dat`
  - OS: `全てのOS`

### Steam Input

Steamworks 設定の「アプリケーション」→「Steam 入力」に次の設定をしてください:

- コントローラにSteam入力を選択: `Xbox`, `PlayStation`, `Nintendo Switch` をチェック
- Steam入力デフォルトコントローラ設定: `カスタム設定`
- マニフェストファイルパス: `action_manifest.vdf`

Game Boy Advance の各ボタン（d-pad, A, B, Start, Select, L, R）の役割は package.conf を適宜編集してください。

なお、Xbox, PlayStation, Nintendo Switch のボタンアサインは次のように割り当てられます。

| Xbox | PlayStation | Nintendo Switch | GBA          |
|:----:|:-----------:|:---------------:|:------------:|
| d-pad| d-pad       | d-pad           | d-pad        |
| A    | ×           | A               | A            |
| B    | ○           | B               | B            |
| X    | ◻︎           | X               | B            |
| Y    | △           | Y               | A            |
| Menu | Menu        | plus            | Start        |
| View | Share       | minus           | Select       |
| LB   | L1          | L               | L            |
| RB   | R1          | R               | R            |
| LT   | L2          | ZL              | L            |
| RT   | R2          | ZR              | R            |
| LS   | L3          | L3              | L            |
| RS   | R3          | R3              | R            |

### Steam Leaderboard

Steamworks 設定の「データ＆実績」→「ランキング」に次のボードを追加してください:

- 名前: `board0`, `board1`, `board2` ... `board15` (最大16ボード)
  - 単一のリーダーボードしか使用しない場合は `board0` だけでも問題ありません
- ユーザーの前後の範囲: `0`
- グローバルランキング上限: `100`

上記以外の設定は任意です。

Dewpoint Advance SDK では、Top 100 のランキングデータと自分のランキングデータを取得できます。（※自分の周辺ランキングを取得するインタフェースはありません）

> _NOTE: Dewpoint SDK で Leaderboard を使用しない場合、この設定は不要です。_

なお、ランキングのレンジ仕様については、私が把握している各プラットフォーム仕様の最小公約数をハードリミットに設定していますが、保証はありません。

また、Dewpoint Advance の UGC データ（スコアランキング添付データ）は、Steam では標準サポートされていますが、一部のコンソール（任天堂スイッチなど）のネットワーク機能では提供されていません。

その他のプラットフォームへ Dewpoint Runtime を移植する際は、各プラットフォームの規定に従って適宜仕様を調整してください。

### Steam Achievement

Steamworks 設定の「データ＆実績」→「実績」に `dpa_achievement_unlock` の引数に指定するテキストと一致する API名 で実績を登録してください。

## How to make the Package

パッケージを作成するには [./package.conf.model](./package.conf.model) からコピーして作成した package.conf ファイルに配信情報を設定する必要があります。

| Settings | Description |
|:---------|:------------|
| `AppName` | ウィンドウタイトルや .app 名、plistなどに設定するアプリ名 |
| `AppVersion` | アプリバージョン |
| `ExeName` | 実行ファイル名（Windowsの場合は .exe が付与されます）|
| `IconFile` | アプリアイコンの格納パスを設定 |
| `RomFile` | ROMファイル (.gba) の格納パスを設定 |
| `ReleaseZip*` | 各OSの Steamworks アップロード用の zip ファイル名 |
| `ButtonDesc*` | 各ボタンの役割を記述（SteamInputの設定にこれが表示されます）|
| `macOS App Settings` | macOS 版アプリの公証付与関連の設定群 |

必要な設定が完了後 `make package` を実行すれば `ReleaseZip*` が生成されます。

### Windows Build Environment

- ビルドには Visual Studio Build ToolsのC++ツールセットとWindows SDK (NMAKE/CL/RC/LINKなど) が必要です。
- Visual Studio の IDE は使用しません。
- Visual Studio 2022 Community/Professional のコマンドライン環境で正常にコンパイルできることを確認しています。

### macOS Build Environment

- macOS版は公証を付与した `.app` 形式のアプリケーションを生成します。
- 公証の付与には Apple Developer Program の契約が必要です。
- `AC_PASSWORD` という Keychain profile を作成する必要があります。

### Linux Build Environment (Docker)

- Linux のビルドを行う場合、GCCのABI互換性の関係で **可能な限り古い Linux 環境でのビルド** が推奨されます。
- Steam でのリリースビルド時は、本リポジトリで提供している [./Dockerfile](./Dockerfile) を用いて構築した Docker イメージ上でビルドすることを推奨します。

### Recommended Repository Structure

例えば、以下のように dewpoint-advance をサブモジュールとして追加しつつ package.conf や GBA プロジェクトを構成管理する構成を推奨します。

```
+- [dir] Your Game App (repo)
    |
    +-- [dir] dewpoint-advance (submodule)
    |
    +-- [dir] Your GBA devkitPro Project
    |
    +-- [file] package.conf
    |
    +-- [file] build.sh / build.bat (package.conf を dewpoint-advance へコピーして `cd dewpoint-advance && make package` を実行)
```

## OSS Licenses

[./src](./src) ディレクトリ以下のソースコードは [MIT](./LICENSE.txt) ですが、最終的な成果物には次のライセンスが含まれます。

- [mGBA](https://mgba.io/)
  - Copyright © 2013–2026 Vicki Pfau.
  - License: [Mozilla Public License Version 2.0](./LICENSE_mGBA.txt)
- [inih](https://github.com/benhoyt/inih)
  - Copyright © 2009 – 2020 Ben Hoyt
  - [License: BSD 3-clause](./LICENSE_inih.txt)
- [SDL2](https://www.libsdl.org/)
  - Copyright © 1997-2025 Sam Lantinga slouken@libsdl.org
  - License: [ZLIB](./LICENSE_SDL2.txt)
- [Dewpoint Advance](https://github.com/suzukiplan/dewpoint-advance)
  - Copyright © 2026 SUZUKI PLAN
  - License: [MIT](./LICENSE_DPA.txt)

ユーザーが確認可能なドキュメント（ストア説明文）に必ず以下の内容を記載してください。

- Dewpoint Advance を利用している旨
- Dewpoint Advance には MPL 2.0 ライセンスのコード（mGBA）が含まれる旨
- MPL 2.0 ランセンスに基づく開示コードは GitHub の `suzukiplan/dewpoint-advance` で入手できる旨

**（記載例）**

```
## License

- This application was developed using Dewpoint Advance.
- Dewpoint Advance includes mGBA source code licensed under the Mozilla Public License 2.0 (MPL 2.0).
- The source code covered by MPL 2.0 is available from suzukiplan/dewpoint-advance on GitHub.
```

mGBA本体の実装をカスタマイズする必要がある場合、例えばそのカスタマイズ実装が含まれる devpoint-advance の fork リポジトリを public で公開して案内するなど、確実に MPL 2.0 のライセンス要件を満たせる状態にしなければなりません。

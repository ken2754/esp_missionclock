# ESP32 CYD NASA Apollo Mission Clock (`esp_missionclock`)

**1960〜70年代のNASA管制室CRT端末スタイルで、アポロ計画の史実無線交信ログをリアルタイムの時間間隔で追体験するESP32 CYD（Cheap Yellow Display）向け時計アプリ。**

Recreating historical NASA Apollo radio transcripts utterance-by-utterance in authentic historical cadence, styled as a 1960s–1970s Apollo Mission Control phosphor CRT terminal on the ESP32 Cheap Yellow Display (ESP32-2432S028, 320×240).

<p align="center">
  <img src="docs/images/cyd_hardware_showcase.jpg" width="680" alt="ESP32 CYD Apollo Mission Clock Hardware Showcase">
</p>

---

## 📸 Screenshots

| Apollo 17 カウントダウン＆保留 (`[GET]` 史実モード) | Apollo 11 月面着陸＆正時イースターエッグ (`[LOC]` 実時刻モード) |
| :---: | :---: |
| ![Apollo 17 GET Mode](docs/images/screenshot_a17_get.png) | ![Apollo 11 LOC Mode](docs/images/screenshot_a11_loc.png) |

| ミッション完了と1時間インターミッション (`WAIT` 待機モード) |
| :---: |
| ![Mission Complete & Recovery](docs/images/screenshot_a17_complete.png) |

---

## ✨ 主な特徴 (Features)

1. **史実の時間間隔を厳格に再現（リアルタイム追体験）**
   - 人工的な早送りや沈黙カットを行わず、宇宙空間の数分〜数十分の沈黙もそのまま待機。打ち上げカウントダウンや月面着陸時の張り詰めた緊張感をリアルタイムに体感できます。
   - 起動時の実時刻（SNTP同期）とミッション開始時刻のオフセットを固定保持して進行します。

2. **1960〜70年代のGlass CRT（陰極線管）ターミナル美学**
   - **完全漆黒背景**（`#000000`）に、高輝度リン光グリーン（`#00ff41`）とディムグリーン（`#00aa33` / `#007722`）の2色構成。
   - Linuxカーネル由来のピュア 1bpp 8×16 ドットモノスペースフォント（1pxストローク）。
   - **12行構成レイアウト**: スクロールする無線交信ログ 11行 ＋ 水平区切り線 ＋ 最下部ステータスバー 1行。
   - 複数行にわたる長い発言は、2文字インデント（`  `）による美しい折り返し描画。

3. **タイプライター演出 & 1秒点滅ブロックカーソル**
   - 新たな発言は、通信士や管制官がキーボードを叩いているかのような人間らしい揺らぎ（タイピング遅延）を伴って1文字ずつタイピング表示。
   - 時計の秒針（1秒）と美しく同期した **1秒周期（1s点灯 / 1s消灯）の塗りつぶし長方形ブロックカーソル（`█`）**。沈黙期間中も静かに呼吸するように点滅待機します。

4. **画面上下分割タップ操作（輝度調整 ＆ 時刻モード切替）**
   - **画面上半分タップ**: バックライト輝度を **0%（消灯） → 20%（夜間/薄暗） → 100%（全開） → 0%** の順序で瞬時にトグル巡回（NVSに自動保存）。消灯時（0%）は画面のどこをタップしても20%に復帰します。
   - **画面下半分タップ**: タイムスタンプ表示モードを瞬時に切り替え：
     - **`[LOC]`（実時刻モード）**: 起動時を起点にオフセットされた現在の時計時刻（例: ` 21:49:50 CDR ...`）
     - **`[GET]`（史実モード）**: 打ち上げを基準とするGround Elapsed Time（例: `-02:26:14 PAO ...` / `+00:01:23 CDR ...`）
   - コロン付き9文字幅統一表記により、モードを切り替えても話者名や本文の横位置が一切左右にブレない設計。

5. **全4ミッション対応 & 完了時の1時間インターミッション自動巡回**
   - **アポロ17号 (主役)**: T-30秒での自動停止と約1時間14分のカウントダウン保留、推進薬タンク加圧、そして夜間打ち上げの咆哮から月面最後のEVAまで。
   - **アポロ11号**: 月面降下、1202警報、静かの海への着陸（*"The Eagle has landed."*）、人類最初の一歩。
   - **アポロ13号**: 打ち上げ、酸素タンク爆発事故（*"Houston, we've had a problem."*）、月着陸船での生還。
   - **アポロ8号**: 人類初の月周回軌道進入、クリスマスイブの創世記朗読、無事の帰還。
   - **1時間インターミッション**: ひとつのミッションが完了すると完了バナーを掲示し、1時間のリカバリー待機を挟んで次のアポロミッション（8 → 11 → 13 → 17 → 8...）へと自動で巡回。待機中はジョークを抑制して静かに時を待ちます。

6. **毎正時のジョーク＆シュール会話イースターエッグ (:00〜:01)**
   - 毎時00分〜01分の間に史実無線交信がない場合に限り、宇宙飛行士がヒューストンに現在時刻を尋ね、CAPCOMが現在時刻をアナウンス（*"It's four o'clock"* 等）。
   - ココアビーチのピザ出前を頼もうとしたり、AGCのDSIKYでSiriを呼んでしまったりするシュールなやり取りがランダムで発生。
   - **白色文字（`#ffffff`）** で表示され、史実の無線記録と明確に区別されます。

7. **Web設定ポータル (Wi-Fi Captive Portal)**
   - 初回起動時または設定時は SoftAP `CYD-MissionClock-Setup` に接続、またはブラウザで取得IPアドレスにアクセスするだけで、Wi-Fi設定、ミッション選択、画面輝度、タイムゾーン設定が可能。

---

## 🛠 ハードウェア仕様とピンアサイン

対象デバイス: **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028, 2.8インチ 320×240 TFT LCD + XPT2046 タッチ)**

| 機能 | 信号名 | ESP32 GPIO | 備考 |
| :--- | :--- | :--- | :--- |
| **LCD ディスプレイ (SPI2)** | SCK | GPIO 14 | 40MHz SPI |
| | MOSI | GPIO 13 | |
| | MISO | GPIO 12 | |
| | DC | GPIO 2 | コマンド/データ選択 |
| | CS | GPIO 15 | チップセレクト |
| | Backlight | GPIO 21 | PWM 調光対応 |
| **タッチスクリーン (SPI3)** | CLK | GPIO 25 | XPT2046 |
| | MOSI | GPIO 32 | |
| | MISO | GPIO 39 | |
| | CS | GPIO 33 | |
| | IRQ | GPIO 36 | 割込み検知 |
| **オンボード RGB LED** | RED / GREEN / BLUE | GPIO 4 / 16 / 17 | Active LOW |

---

## 🚀 ビルドと書き込み (Build & Flash)

本プロジェクトは **ESP-IDF v5.3** にて開発・検証されています。

### 1. ESP-IDF 環境のセットアップ
```bash
# ESP-IDF 環境変数の読み込み
source /path/to/esp-idf/export.sh
```

### 2. リポジトリのクローンとビルド
```bash
git clone https://github.com/<your-username>/esp_missionclock.git
cd esp_missionclock

# ビルド実行
idf.py build
```

### 3. デバイスへの書き込み
```bash
# CYDをUSB接続してFlash書き込み & シリアルモニタ起動
idf.py -p /dev/ttyUSB0 flash monitor
```

---

## ⚙️ 初回設定とWebポータル

1. デバイスが未接続のWi-Fi環境で起動すると、自動的に SoftAP **`CYD-MissionClock-Setup`** が起動します。
2. スマートフォンやPCからこのWi-Fiに接続し、ブラウザで `http://192.168.4.1` にアクセスします。
3. 自宅のWi-Fi SSID、パスワード、初期ミッション、タイムゾーン等を入力して「SAVE & APPLY」を押下します。
4. Wi-Fi接続後はSNTPにより正確なJST時刻を取得し、最下部ステータスバーに取得したローカルIPアドレス（例: `192.168.0.232`）が表示されます。以降はそのIPアドレスにブラウザからアクセスすることでいつでも設定変更が可能です。

---

## 📁 ディレクトリ構成

```text
esp_missionclock/
├── CMakeLists.txt              # プロジェクトトップ CMake
├── sdkconfig.defaults          # Flash 4MB, O3最適化設定
├── partitions.csv              # 4MB Flash パーティション構成
├── main/
│   ├── main.c                  # アプリケーションエントリポイント
│   ├── config.h                # ピンアサイン、色定義、グリッド定数
│   ├── term_font_8x16.c/.h     # Linux VGA 8x16 1bpp フォント (ブロックカーソル含む)
│   ├── terminal_ui.c/.h        # 12行レトロCRTターミナルUI & タイプライター演出
│   ├── mission_engine.c/.h     # 史実タイムライン進行 & 1hインターミッション
│   ├── mission_data.c/.h       # フラッシュ埋め込みミッションバイナリ直接参照
│   ├── settings_mgr.c/.h       # NVS設定管理
│   ├── display_driver.c/.h     # ST7789 / ILI9341 LCDドライバ
│   ├── touch_driver.c/.h       # XPT2046 タッチドライバ
│   ├── wifi_portal.c/.h        # SoftAP & Web設定ポータル
│   ├── sntp_time.c/.h          # SNTP時刻同期
│   └── data/                   # 生成されたミッションバイナリ (.bin)
├── tools/
│   ├── fetch_and_pack_missions.py # 史実トランスクリプトの取得・クリーン分割・圧縮
│   ├── generate_font_8x16.py      # Linuxカーネルフォント抽出・Cソース生成
│   └── render_screenshot.py       # 実フォントビットマップを用いたPNG画像レンダラ
└── docs/
    └── images/                 # スクリーンショット画像
```

---

## 📜 謝辞・クレジット (Acknowledgements)

- **NASA Apollo Mission Transcripts**:
  - [Spacelog](https://github.com/Spacelog/Spacelog) (Apollo 8, Apollo 11, Apollo 13)
  - [Ben Feist's Apollo 17 in Real-Time](https://apolloinrealtime.org/17/) (Apollo 17 Master TEC & PAO transcripts)
- **Reference Project**:
  - `esp_consoleclock` (Wi-Fiポータル、ディスプレイドライバ、SNTP基盤設計)

---

## 📄 ライセンス

MIT License.

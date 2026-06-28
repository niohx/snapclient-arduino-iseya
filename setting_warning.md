# XIAO ESP32-S3 Plus + PCM5102 DAC 構成のセットアップ注意点

M5Stack Basic + ES8388 構成での経験をもとに、つまりそうなポイントをまとめます。

## 1. ライブラリバージョン (最重要)

### arduino-snapclient は main ブランチを使う
- **リリース版 (v0.2.0) は使えない** — audio-tools v1.2.x と互換性がない
- `AudioConfig.h` が見つからないエラーになる
- GitHub の `Code → Download ZIP` で **main ブランチ** をダウンロードすること

### arduino-audio-tools のバージョン
- ライブラリマネージャーには出てこない場合がある — ZIP インストール推奨
- `audio-tools` と `arduino-audio-tools` が両方あると競合する。1つだけにすること

## 2. CONFIG_SNAPCAST_SERVER_HOST の定義位置

```cpp
// NG: #include の後に書くと無視される
#include "SnapClient.h"
#define CONFIG_SNAPCAST_SERVER_HOST "192.168.86.84"  // ← 効かない！

// OK: #include の前に書く
#define CONFIG_SNAPCAST_SERVER_HOST "192.168.86.84"
#include "SnapClient.h"
```

または `setServerIP()` を `begin()` の前に呼ぶ方法でもOK:
```cpp
IPAddress serverIP;
serverIP.fromString("192.168.86.84");
snapClient.setServerIP(serverIP);
snapClient.begin(synch);
```

## 3. I2S ピン配線 (PCM5102 固有の注意)

### PCM5102 は MCLK 不要
- PCM5102 は内蔵 PLL があるため **MCLK 接続は不要**
- I2S 設定で `cfg.pin_mck = -1` にするか、MCLK ピンを指定しないこと
- M5Stack では MCLK (GPIO 0) が必要だったが、PCM5102 では不要

### PCM5102 の制御ピン (Adafruit ADA-6250)
基板上のパッドやピンの設定に注意:
| ピン | 設定 | 説明 |
|------|------|------|
| FMT | GND (Low) | I2S フォーマット (Low = I2S standard) |
| XSMT | 3.3V (High) | ソフトミュート解除 (Low = ミュート) |
| SCK | GND | システムクロック (内蔵PLL使用時は GND) |

**XSMT が Low のままだと音が出ない。** Adafruit の基板ではデフォルトでプルアップされている場合があるが、要確認。

### XIAO ESP32-S3 の I2S ピン選択
- ESP32-S3 はほぼ全ての GPIO を I2S に使える（GPIO マトリクス）
- ただし **GPIO 0 はストラッピングピン** なので避けること
- 推奨例:

| 信号 | XIAO ESP32-S3 GPIO | PCM5102 ピン |
|------|---------------------|-------------|
| BCK | GPIO 7 (D9) | BCK |
| WS/LRCK | GPIO 8 (D10) | LCK |
| DIN | GPIO 9 (D11) | DIN |

**ピン番号はシルク印刷 (D0-D10) と GPIO 番号が異なる。** 必ずデータシートで対応を確認すること。

## 4. パーティションスキーム

- Opus デコーダーを含むとスケッチサイズが 1.3MB を超える
- **デフォルトのパーティションでは入らない**
- Arduino IDE: `ツール → Partition Scheme → Huge APP (3MB No OTA/1MB SPIFFS)`
- XIAO ESP32-S3 Plus は 16MB フラッシュなので余裕があるが、ボード定義のデフォルトを確認すること

## 5. Snapcast サーバーのコーデック設定

- サーバーのデフォルトコーデックは **FLAC**
- arduino-snapclient で FLAC を使うには追加ライブラリ (libflac) が必要
- **Opus に変更するのが最も簡単**

```ini
# /etc/snapserver.conf
source = tcp://0.0.0.0:4953?name=Spotify&codec=opus
```

- ストリームごとに `&codec=opus` を付ける必要がある
- サーバー再起動を忘れないこと (WSL の場合 `sudo killall snapserver` してから再起動)

## 6. デバッグログによる音切れ

- `CORE_DEBUG_LEVEL` を `5` (Verbose) や `4` (Info) にすると **シリアル出力がCPUを占有して音が切れ切れになる**
- デバッグ時は Verbose でもOKだが、**動作確認後は必ず `1` (Error) に戻すこと**
- `AudioLogger` も同様: `AudioLogger::Warning` 以上は音切れの原因になる

```cpp
#define CORE_DEBUG_LEVEL 1  // Error only
AudioLogger::instance().begin(Serial, AudioLogger::Error);
```

## 7. mDNS 自動検出は当てにしない

- `setupMDNS(): SNAPCAST server not found` が頻繁に発生する
- **サーバー IP を .env で直接指定する方が確実**
- mDNS タイムアウトで約3秒のロスが発生する

## 8. ESP32-S3 固有の注意

### ボード定義
- Arduino IDE のボードマネージャーで **Seeed Studio XIAO ESP32-S3** を選択
- `esp32` パッケージが必要 (Espressif)

### USB CDC
- XIAO ESP32-S3 は USB-CDC 経由でシリアル通信する
- `Serial` が USB CDC を指す場合、`Serial.begin()` の後に `delay(1000)` を入れないとログが欠ける
- スケッチアップロード時にブートモードに入れる方法を確認しておくこと

### I2S ドライバ
- ESP32-S3 の Arduino Core 3.x は新しい I2S ドライバ (ESP-IDF v5.x) を使用
- audio-tools の `I2SStream` は対応しているが、GPIO レポート (`perimanSetPinBus`) に I2S ピンが表示されないことがある（動作には影響なし）

### PSRAM
- XIAO ESP32-S3 Plus は PSRAM 搭載
- Opus デコーダーのバッファを PSRAM に置けるとメモリに余裕が出る
- Arduino IDE: `ツール → PSRAM → OPI PSRAM`

## 9. PCM5102 は I2C 不要 (ES8388 との大きな違い)

- M5Stack Module Audio では ES8388 を I2C で初期化 + STM32 でヘッドホンモード設定が必要だった
- **PCM5102 は設定不要** — I2S 信号を繋ぐだけで音が出る
- `AudioI2c` / `ES8388` クラスのコードは全て不要
- ミュート解除 (XSMT ピン) だけ注意

## 10. 電源

- XIAO ESP32-S3 は 3.3V 出力
- PCM5102 は 3.3V 駆動可能 (Adafruit 基板は 3.3V / 5V 両対応)
- WiFi + I2S + DAC の同時動作時に電流不足にならないか注意
- USB 給電で通常は十分だが、長いケーブルや USB ハブ経由だと不安定になることがある

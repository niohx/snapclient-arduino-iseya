# snapclient-arduino-iseya

M5Stack + M5Stack オーディオモジュール（ES8388）を使った [Snapcast](https://github.com/badaix/snapcast) クライアントのArduinoスケッチです。  
[arduino-snapclient](https://github.com/pschatzmann/arduino-snapclient) ライブラリをベースに、Wi-Fi経由でSnapcastサーバーと同期再生します。

---

## ハードウェア

| 部品 | 製品 |
|------|------|
| マイコン | [M5Stack Core](https://docs.m5stack.com/) シリーズ（ESP32） |
| オーディオモジュール | [M5Stack オーディオモジュール（STM32G030）](https://ssci.to/10417) |

### 接続

M5Stack オーディオモジュールをM5Stackの底面モジュールポートに装着します。  
I2S（オーディオ）とI2C（ES8388コーデック制御）の接続はM-Bus経由で自動的に行われます。

| 信号 | GPIO |
|------|------|
| I2S MCLK | GPIO 0 |
| I2S BCLK | GPIO 12 |
| I2S WS (LRCK) | GPIO 13 |
| I2S DOUT | GPIO 2 |
| I2C SDA | GPIO 21 |
| I2C SCL | GPIO 22 |

> **Note:** 使用するM5Stackモデルによってピン番号が異なる場合があります。M5Stackの公式ドキュメントで確認してください。

---

## 必要なライブラリ

Arduino IDEのライブラリマネージャーまたは以下のリポジトリからインストールしてください。

| ライブラリ | 入手先 | 必須/任意 |
|-----------|--------|-----------|
| arduino-snapclient | https://github.com/pschatzmann/arduino-snapclient | 必須 |
| arduino-audio-tools | https://github.com/pschatzmann/arduino-audio-tools | 必須 |
| arduino-libopus | https://github.com/pschatzmann/arduino-libopus | 任意（Opus使用時） |

---

## インストール手順

### 1. Arduino IDE の準備

Arduino IDE 2.x をインストールし、ESP32ボードパッケージを追加します。

1. **環境設定** → 追加のボードマネージャーURLに以下を追加:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **ボードマネージャー** で `esp32 by Espressif Systems` をインストール

### 2. ライブラリのインストール

ライブラリはZIPダウンロードまたは `git clone` でインストールします。  
Arduino のライブラリフォルダ（通常 `~/Documents/Arduino/libraries/`）で以下を実行:

```bash
# 必須
git clone https://github.com/pschatzmann/arduino-snapclient.git
git clone https://github.com/pschatzmann/arduino-audio-tools.git

# 任意（Opusコーデックを使う場合）
git clone https://github.com/pschatzmann/arduino-libopus.git
```

または Arduino IDE の **スケッチ → ライブラリをインクルード → .ZIP形式のライブラリをインストール** からZIPを指定してもOKです。

### 3. スケッチの設定

`SnapConfig.h` またはスケッチ内で以下を設定します:

```cpp
// Wi-Fi 設定
#define CONFIG_WIFI_SSID     "your_ssid"
#define CONFIG_WIFI_PASSWORD "your_password"

// SnapcastサーバーのIPアドレス（フォールバック用）
// デフォルトでmDNSにより自動検出されるため、指定不要な場合がほとんど
#define CONFIG_SNAPCAST_SERVER_HOST "192.168.x.x"
```

### 4. ボードの選択と書き込み

Arduino IDEで次の設定を行ってから書き込みます:

- **ボード:** `M5Stack-Core-ESP32`（または使用モデルに合わせて選択）
- **パーティションスキーム:** `Huge APP (3MB No OTA/1MB SPIFFS)` ※ライブラリサイズが大きいため推奨
- **ポート:** 接続したCOMポート

---

## スケッチ例

スケッチは `snapclient-iseya/snapclient-iseya.ino` に収録しています。

M5Stack の画面遷移を含む完全な実装です:

```
起動画面 → WiFi接続中 → WiFi接続完了 → サーバー検索中 → 接続完了 → ステータス画面
```

**ステータス画面の内容:**
- WiFi SSID / IP アドレス
- Snapserver 接続先
- 再生状態・コーデック
- 稼働時間（右上、1秒更新）
- WiFi 電波強度バー（RSSI）

**ボタン操作:**
| ボタン | 動作 |
|--------|------|
| A | 輝度切替（160 ↔ 80）|
| C | Snapserver 再接続 |

---

## Snapcastサーバー側の設定

`/etc/snapserver.conf` でコーデックを Opus に設定して再起動します:

```ini
[stream]
codec = opus
```

```bash
sudo systemctl restart snapserver
```

動作確認用のテストストリーム再生例:

```bash
ffmpeg -i http://stream.example.com/audio.mp3 \
  -f s16le -ar 48000 -ac 2 /tmp/snapfifo
```

---

## トラブルシューティング

| 症状 | 確認ポイント |
|------|-------------|
| コンパイルエラー | ライブラリが正しくインストールされているか確認 |
| Wi-Fi に繋がらない | SSID / パスワードを確認 |
| サーバーに繋がらない | snapserver のIPアドレスとポート（デフォルト: 1704）を確認 |
| 音が出ない | I2S のピン番号とM5Stackモデルが合っているか確認 |
| 音が途切れる | `cfg.buffer_size` / `cfg.buffer_count` を増やしてみる |

---

## 参考リンク

- [arduino-snapclient](https://github.com/pschatzmann/arduino-snapclient)
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
- [Snapcast (サーバー)](https://github.com/badaix/snapcast)
- [M5Stack オーディオモジュール（スイッチサイエンス）](https://ssci.to/10417)

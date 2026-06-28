# Snapclient Iseya

M5Stack Basic + M5Stack Module Audio (ES8388) で動作する Snapcast クライアント。

Snapcast サーバーから Opus ストリームを受信し、ES8388 DAC 経由でヘッドホン出力します。

## ハードウェア

| 部品 | 製品 |
|------|------|
| マイコン | [M5Stack Basic](https://docs.m5stack.com/en/core/basic) (v2.7+ / ESP32) |
| オーディオモジュール | [M5Stack Module Audio (M144)](https://ssci.to/10417) (ES8388 + STM32G030) |
| 出力 | ヘッドホン (モジュールの TRRS ジャック接続) |

M5Stack オーディオモジュールを M5Stack の底面モジュールポートに装着します。
I2S / I2C の接続は M-Bus 経由で自動的に行われます。

### I2S ピン配置 (M5Stack Basic + Module Audio)

| 信号 | GPIO |
|------|------|
| MCLK | 0 |
| BCLK (SCLK) | 13 |
| WS (LRCK) | 12 |
| DOUT | 15 |
| DIN | 34 |
| I2C SDA | 21 |
| I2C SCL | 22 |

> Core2 / CoreS3 ではピン番号が異なります。M5Stack 公式ドキュメントを確認してください。

## セットアップ

### 1. Arduino IDE の準備

Arduino IDE 2.x に M5Stack ボードパッケージをインストールします。

1. **環境設定** → 追加のボードマネージャー URL に M5Stack の URL を追加
2. **ボードマネージャー** で `M5Stack` をインストール

### 2. ライブラリのインストール

以下のライブラリを全てインストールしてください。

| ライブラリ | インストール方法 | 備考 |
|---|---|---|
| [M5Unified](https://github.com/m5stack/M5Unified) | ライブラリマネージャー | |
| [M5GFX](https://github.com/m5stack/M5GFX) | ライブラリマネージャー | |
| [M5Module-Audio](https://github.com/m5stack/M5Module-Audio) | ZIP インストール | |
| [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) | ZIP インストール | |
| [arduino-snapclient](https://github.com/pschatzmann/arduino-snapclient) | ZIP インストール | **main ブランチ必須** |
| [arduino-libopus](https://github.com/pschatzmann/arduino-libopus) | ZIP インストール | |

> **重要**: arduino-snapclient は **main ブランチの ZIP** (`Code → Download ZIP`) を使用してください。
> リリース版 (v0.2.0) は audio-tools v1.2.x と互換性がありません。

### 3. LittleFS Upload プラグイン

Arduino IDE 2.x で LittleFS にファイルを書き込むためのプラグインが必要です。

1. [arduino-littlefs-upload](https://github.com/earlephilhower/arduino-littlefs-upload/releases) から `.vsix` をダウンロード
2. `~/.arduinoIDE/plugins/` にコピー
3. Arduino IDE を再起動

### 4. 設定ファイルの準備

`snapclient-iseya/data/.env.example` を `snapclient-iseya/data/.env` にコピーして編集:

```env
WIFI_SSID=your-wifi-ssid
WIFI_PASSWORD=your-wifi-password
SNAPCAST_HOST=192.168.x.x
```

| キー | 説明 |
|---|---|
| `WIFI_SSID` | WiFi の SSID |
| `WIFI_PASSWORD` | WiFi のパスワード |
| `SNAPCAST_HOST` | Snapcast サーバーの IP アドレス |

`.env` は `.gitignore` に含まれているためコミットされません。

### 5. Arduino IDE のボード設定

| 設定項目 | 値 |
|---|---|
| ボード | **M5Stack-Core** |
| Partition Scheme | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| Core Debug Level | **Error** |
| Upload Speed | 1500000 |

### 6. 書き込み

1. **LittleFS 書き込み**: `Ctrl + Shift + P` → **Upload LittleFS to Pico/ESP8266/ESP32**
2. **スケッチ書き込み**: 通常通りアップロード

## Snapcast サーバーの設定

### コーデック

サーバー側でストリームのコーデックを Opus に設定してください。

`/etc/snapserver.conf`:

```ini
[stream]
source = tcp://0.0.0.0:4953?name=Spotify&codec=opus
```

設定変更後にサーバーを再起動:

```bash
sudo systemctl restart snapserver
```

WSL 環境の場合:

```bash
sudo killall snapserver
snapserver -d -c /etc/snapserver.conf
```

### クライアントのグループ割り当て

Snapcast 管理画面 (`http://<server-ip>:1780`) で、**arduino-snapclient** を目的のストリームグループ (例: Spotify) に割り当ててください。

## 画面遷移

```
起動画面 → WiFi接続中 → WiFi接続完了 → サーバー接続完了 → ステータス画面
```

ステータス画面には WiFi SSID / IP、サーバー IP、再生状態が表示されます。

## トラブルシューティング

| 症状 | 対処 |
|------|------|
| コンパイルエラー: スケッチが大きすぎる | Partition Scheme を **Huge APP** に変更 |
| WiFi に接続できない | `data/.env` を LittleFS にアップロードしたか確認 |
| サーバーに接続できない | `.env` の `SNAPCAST_HOST` が正しいか確認 |
| 音が出ない | ヘッドホンがモジュールの **TRRS ジャック** に接続されているか確認 |
| 音が出ない | Snapcast 管理画面でクライアントが正しいグループに入っているか確認 |
| 音が出ない | サーバーのコーデックが `opus` になっているか確認 |

## 参考リンク

- [arduino-snapclient](https://github.com/pschatzmann/arduino-snapclient)
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
- [Snapcast](https://github.com/badaix/snapcast)
- [M5Stack Module Audio (スイッチサイエンス)](https://ssci.to/10417)
- [M5Stack Module Audio ドキュメント](https://docs.m5stack.com/en/module/Module-Audio)

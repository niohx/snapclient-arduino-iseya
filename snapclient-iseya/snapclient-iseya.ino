/**
 * snapclient-iseya.ino
 *
 * M5Stack + M5Stack Audio Module (ES8388) 用 Snapcast クライアント
 *
 * 画面遷移:
 *   起動 → WiFi接続中 → WiFi接続完了 → サーバー検索中 → サーバー接続完了 → ステータス
 *
 * 依存ライブラリ:
 *   - arduino-snapclient  https://github.com/pschatzmann/arduino-snapclient
 *   - arduino-audio-tools https://github.com/pschatzmann/arduino-audio-tools
 *   - arduino-libopus     https://github.com/pschatzmann/arduino-libopus
 *   - M5Stack             (ボードマネージャー経由)
 */

// loop() タスクのスタックサイズ拡張（AudioTools が必要）
#define ARDUINO_LOOP_STACK_SIZE (10 * 1024)

#include <M5Stack.h>
#include <Wire.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "AudioTools.h"
#include "SnapClient.h"
#include "AudioTools/AudioCodecs/CodecOpus.h"

// ═══════════════════════════════════════════════════════════════
//  設定 — data/.env に WIFI_SSID / WIFI_PASSWORD を記入してください
//         Arduino IDE「ESP32 LittleFS Data Upload」で書き込み後に使用
// ═══════════════════════════════════════════════════════════════

// Snapcast サーバーは mDNS で自動検出（デフォルト）
// 手動指定したい場合は以下のコメントを外して IP を入力
// #define CONFIG_SNAPCAST_SERVER_HOST "192.168.1.33"

// ─── I2S ピン (M5Stack Core + M5Stack Audio Module) ───────────
#define PIN_MCLK   0
#define PIN_BCLK  12
#define PIN_WS    13
#define PIN_DATA   2

// ─── ES8388 ───────────────────────────────────────────────────
#define ES8388_ADDR  0x10
#define I2C_SDA      21
#define I2C_SCL      22

// ═══════════════════════════════════════════════════════════════
//  カラー定義 (RGB565)
// ═══════════════════════════════════════════════════════════════
#define C_BG      TFT_BLACK
#define C_WHITE   TFT_WHITE
#define C_GREEN   0x07E0
#define C_YELLOW  TFT_YELLOW
#define C_RED     TFT_RED
#define C_CYAN    0x07FF
#define C_GRAY    0x8410
#define C_DGRAY   0x4208   // ダークグレー
#define C_PANEL   0x1082   // ヘッダー・パネル背景

// ═══════════════════════════════════════════════════════════════
//  アプリケーション状態
// ═══════════════════════════════════════════════════════════════
enum AppState {
  STATE_BOOT,
  STATE_WIFI_CONNECTING,
  STATE_WIFI_CONNECTED,
  STATE_SERVER_SEARCHING,
  STATE_SERVER_CONNECTED,
  STATE_STATUS
};

AppState     appState   = STATE_BOOT;
String       wifiSSID   = "";
String       wifiPassword = "";
String       localIP    = "";
unsigned long stateTimer = 0;
unsigned long animTimer  = 0;
unsigned long clockTimer = 0;
int          animStep   = 0;
bool         dimmed     = false;   // 輝度トグル用

// ═══════════════════════════════════════════════════════════════
//  オーディオ
// ═══════════════════════════════════════════════════════════════
I2SStream          i2sOut;
OpusAudioDecoder   opusDecoder;
WiFiClient         wifiClient;
SnapTimeSyncDynamic synch(172, 10);
SnapClient         snapClient(wifiClient, i2sOut, opusDecoder);

// ═══════════════════════════════════════════════════════════════
//  .env 読み込み (LittleFS)
// ═══════════════════════════════════════════════════════════════

// src 内から "KEY=VALUE" 形式の値を返す（# 行はスキップ）
static String envGet(const String& src, const String& key) {
  String prefix = key + "=";
  int pos = 0;
  while (pos < (int)src.length()) {
    int nl = src.indexOf('\n', pos);
    if (nl < 0) nl = src.length();
    String line = src.substring(pos, nl);
    line.trim();
    if (line.length() > 0 && line[0] != '#' && line.startsWith(prefix)) {
      String val = line.substring(prefix.length());
      val.trim();
      return val;
    }
    pos = nl + 1;
  }
  return "";
}

// LittleFS の /.env を読み込んで wifiSSID / wifiPassword を設定
static void loadEnv() {
  if (!LittleFS.begin(false)) {
    Serial.println("[env] LittleFS mount failed — data/ フォルダをアップロードしてください");
    return;
  }
  File f = LittleFS.open("/.env", "r");
  if (!f) {
    Serial.println("[env] /.env が見つかりません — data/ フォルダをアップロードしてください");
    LittleFS.end();
    return;
  }
  String src = f.readString();
  f.close();
  LittleFS.end();

  wifiSSID     = envGet(src, "WIFI_SSID");
  wifiPassword = envGet(src, "WIFI_PASSWORD");
  Serial.printf("[env] SSID=%s\n", wifiSSID.c_str());
}

// ═══════════════════════════════════════════════════════════════
//  ES8388 初期化
// ═══════════════════════════════════════════════════════════════
static void esWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8388_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void initES8388() {
  Wire.begin(I2C_SDA, I2C_SCL);
  delay(20);

  esWrite(0x00, 0x80);  // チップリセット
  delay(50);
  esWrite(0x00, 0x00);  // 通常モード復帰

  esWrite(0x01, 0x50);  // CHIPPOWER: VREF・IREF オン
  esWrite(0x08, 0x00);  // MASTERMODE: スレーブ (ESP32 が I2S マスター)

  // ADC をパワーダウン、DAC をパワーアップ
  esWrite(0x03, 0xFF);  // ADCPOWER: 全オフ
  esWrite(0x04, 0xC0);  // DACPOWER: LOUT/ROUT はまだオフ

  // DAC I2S: I2S フォーマット、16bit
  esWrite(0x17, 0x18);  // DACCONTROL1: I2S, 16-bit
  esWrite(0x18, 0x02);  // DACCONTROL2: 標準速度
  esWrite(0x19, 0x22);  // DACCONTROL3: ミュート解除

  // DAC ボリューム: 0dB
  esWrite(0x1A, 0x00);
  esWrite(0x1B, 0x00);

  // 出力ミキサー: DAC → LOUT1 / ROUT1
  esWrite(0x27, 0xB8);  // 左ミキサー
  esWrite(0x2A, 0xB8);  // 右ミキサー

  // 出力ボリューム (0–33, 33 = 最大)
  esWrite(0x2E, 28);    // LOUT1
  esWrite(0x2F, 28);    // ROUT1

  // DAC 出力段を有効化
  esWrite(0x04, 0x3C);  // DACPOWER: LOUT1/ROUT1 オン
  esWrite(0x01, 0x00);  // CHIPPOWER: フルパワーアップ
  delay(50);
}

// ═══════════════════════════════════════════════════════════════
//  共通 UI パーツ
// ═══════════════════════════════════════════════════════════════

// ヘッダーバー描画
static void drawHeader(const char* title, uint16_t titleColor) {
  M5.Lcd.fillRect(0, 0, 320, 26, C_PANEL);
  M5.Lcd.drawFastHLine(0, 26, 320, C_DGRAY);
  M5.Lcd.setTextColor(titleColor);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 5);
  M5.Lcd.print(title);
}

// 水平区切り線
static void drawHLine(int y) {
  M5.Lcd.drawFastHLine(0, y, 320, C_DGRAY);
}

// ラベル + 値 の1行
static void drawKV(int x, int y, const char* label, const char* value,
                   uint16_t labelColor, uint16_t valueColor) {
  M5.Lcd.setTextSize(1);
  M5.Lcd.setTextColor(labelColor);
  M5.Lcd.setCursor(x, y);
  M5.Lcd.print(label);
  M5.Lcd.setTextColor(valueColor);
  M5.Lcd.print(value);
}

// ═══════════════════════════════════════════════════════════════
//  画面: 起動
// ═══════════════════════════════════════════════════════════════
void drawBootScreen() {
  M5.Lcd.fillScreen(C_BG);

  // ロゴ
  M5.Lcd.setTextColor(C_CYAN);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(16, 35);
  M5.Lcd.print("Snapclient");

  M5.Lcd.setTextColor(C_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(74, 78);
  M5.Lcd.print("for  Iseya");

  drawHLine(112);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(68, 122);
  M5.Lcd.print("M5Stack + ES8388 Audio");
  M5.Lcd.setCursor(54, 138);
  M5.Lcd.print("Powered by arduino-snapclient");

  // 起動プログレスバー (演出)
  M5.Lcd.setTextColor(C_DGRAY);
  M5.Lcd.setCursor(6, 218);
  M5.Lcd.print("[A] Brightness  [C] Reconnect");

  for (int i = 0; i < 6; i++) {
    M5.Lcd.fillRect(32 + i * 44, 180, 36, 10, C_CYAN);
    delay(120);
  }
}

// ═══════════════════════════════════════════════════════════════
//  画面: WiFi 接続中
// ═══════════════════════════════════════════════════════════════
void drawWifiConnecting() {
  M5.Lcd.fillScreen(C_BG);
  drawHeader("[WiFi] Connecting...", C_YELLOW);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 42);
  M5.Lcd.print("SSID");
  M5.Lcd.setTextColor(C_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 55);
  M5.Lcd.print(wifiSSID);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 108);
  M5.Lcd.print("Connecting to network...");
}

// スピナー更新
void updateWifiAnim() {
  static const char* frames[] = {"|", "/", "-", "\\"};
  M5.Lcd.fillRect(136, 124, 48, 36, C_BG);
  M5.Lcd.setTextColor(C_YELLOW);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(136, 124);
  M5.Lcd.print(frames[animStep & 3]);
}

// ═══════════════════════════════════════════════════════════════
//  画面: WiFi 接続完了
// ═══════════════════════════════════════════════════════════════
void drawWifiConnected() {
  M5.Lcd.fillScreen(C_BG);
  drawHeader("[WiFi] Connected!", C_GREEN);

  // 大きい OK 表示
  M5.Lcd.setTextColor(C_GREEN);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(16, 38);
  M5.Lcd.print("OK!");

  drawHLine(100);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 108);
  M5.Lcd.print("SSID");
  M5.Lcd.setTextColor(C_WHITE);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print(wifiSSID);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 154);
  M5.Lcd.print("IP Address");
  M5.Lcd.setTextColor(C_CYAN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 166);
  M5.Lcd.print(localIP);
}

// ═══════════════════════════════════════════════════════════════
//  画面: サーバー検索中
// ═══════════════════════════════════════════════════════════════
void drawServerSearching() {
  M5.Lcd.fillScreen(C_BG);
  drawHeader("[Server] Searching...", C_YELLOW);

  M5.Lcd.setTextColor(C_WHITE);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 44);
  M5.Lcd.print("mDNS でサーバーを自動検索中...");

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setCursor(10, 60);
  M5.Lcd.print("見つからない場合は IP を手動設定してください");
}

// スキャンバー更新
void updateServerAnim() {
  const int BAR_X = 30;
  const int BAR_Y = 100;
  const int CELLS = 10;
  const int CW    = 24;

  M5.Lcd.fillRect(BAR_X, BAR_Y, CELLS * CW + 2, 22, C_BG);

  for (int i = 0; i < CELLS; i++) {
    bool lit = (i == (animStep % CELLS));
    M5.Lcd.fillRect(BAR_X + i * CW + 1, BAR_Y + 2, CW - 3, 18,
                    lit ? C_YELLOW : C_DGRAY);
  }

  // "Searching..." テキスト
  M5.Lcd.fillRect(10, 136, 200, 14, C_BG);
  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 136);
  M5.Lcd.print("Searching");
  for (int i = 0; i < (animStep % 4); i++) M5.Lcd.print(".");
}

// ═══════════════════════════════════════════════════════════════
//  画面: サーバー接続完了
// ═══════════════════════════════════════════════════════════════
void drawServerConnected() {
  M5.Lcd.fillScreen(C_BG);
  drawHeader("[Server] Connected!", C_GREEN);

  M5.Lcd.setTextColor(C_GREEN);
  M5.Lcd.setTextSize(3);
  M5.Lcd.setCursor(12, 36);
  M5.Lcd.print("Found!");

  drawHLine(100);

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 108);
  M5.Lcd.print("Server");
  M5.Lcd.setTextColor(C_CYAN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(10, 120);
  M5.Lcd.print("Auto (mDNS)");

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(10, 155);
  M5.Lcd.print("Port : 1704");

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setCursor(10, 174);
  M5.Lcd.print("Audio stream starting...");
}

// ═══════════════════════════════════════════════════════════════
//  画面: ステータス (メイン画面)
// ═══════════════════════════════════════════════════════════════
void drawStatusScreen() {
  M5.Lcd.fillScreen(C_BG);

  // ── ヘッダー ──────────────────────────────────────────
  M5.Lcd.fillRect(0, 0, 320, 22, C_PANEL);
  M5.Lcd.setTextColor(C_CYAN);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(6, 7);
  M5.Lcd.print(">> Snapclient Iseya");
  drawHLine(22);

  // ── WiFi / サーバー情報 ──────────────────────────────
  const int ROW1 = 30, ROW2 = 46, ROW3 = 62;
  const int COL_L = 6, COL_V = 56;

  M5.Lcd.setTextSize(1);
  drawKV(COL_L, ROW1, "WiFi  :", wifiSSID.c_str(),     C_GRAY, C_WHITE);
  drawKV(COL_L, ROW2, "IP    :", localIP.c_str(),       C_GRAY, C_WHITE);
  drawKV(COL_L, ROW3, "Server:", "Auto (mDNS) :1704",   C_GRAY, C_CYAN);

  drawHLine(76);

  // ── 再生ステータス ───────────────────────────────────
  M5.Lcd.fillRect(0, 77, 320, 56, C_PANEL);

  M5.Lcd.setTextColor(C_GREEN);
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(8, 84);
  M5.Lcd.print("> PLAYING");

  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(8, 110);
  M5.Lcd.print("Codec: Opus   Sync: Active");

  drawHLine(133);

  // ── ボタンヒント ────────────────────────────────────
  M5.Lcd.setTextColor(C_DGRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(6, 140);
  M5.Lcd.print("[A] Brightness");
  M5.Lcd.setCursor(160, 140);
  M5.Lcd.print("[C] Reconnect");

  drawHLine(152);

  // ── シグナルバー (WiFi RSSI) ──────────────────────────
  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(6, 160);
  M5.Lcd.print("WiFi Signal:");
}

// シグナルバー & 稼働時間を1秒毎に更新
void updateStatusLive() {
  // 稼働時間 (右上)
  unsigned long s = millis() / 1000;
  unsigned long m = s / 60; s %= 60;
  unsigned long h = m / 60; m %= 60;
  char tbuf[10];
  snprintf(tbuf, sizeof(tbuf), "%02lu:%02lu:%02lu", h, m, s);
  M5.Lcd.fillRect(210, 5, 104, 14, C_PANEL);
  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(212, 8);
  M5.Lcd.print(tbuf);

  // WiFi RSSI バー (右下エリア)
  int32_t rssi = WiFi.RSSI();
  int strength = map(constrain(rssi, -90, -40), -90, -40, 0, 5); // 0-5本
  M5.Lcd.fillRect(100, 158, 210, 14, C_BG);
  for (int i = 0; i < 5; i++) {
    uint16_t c = (i < strength) ? C_CYAN : C_DGRAY;
    int bh = 4 + i * 2;
    M5.Lcd.fillRect(100 + i * 22, 172 - bh, 18, bh, c);
  }

  // RSSI 数値
  M5.Lcd.setTextColor(C_GRAY);
  M5.Lcd.setTextSize(1);
  M5.Lcd.setCursor(220, 160);
  char rbuf[12];
  snprintf(rbuf, sizeof(rbuf), "%d dBm", (int)rssi);
  M5.Lcd.print(rbuf);
}

// ═══════════════════════════════════════════════════════════════
//  setup
// ═══════════════════════════════════════════════════════════════
void setup() {
  M5.begin();
  Serial.begin(115200);
  loadEnv();
  M5.Lcd.fillScreen(C_BG);
  M5.Lcd.setBrightness(160);

  // ① 起動画面
  appState = STATE_BOOT;
  drawBootScreen();
  delay(2200);

  // ② WiFi 接続
  appState = STATE_WIFI_CONNECTING;
  animStep = 0;
  drawWifiConnecting();
  animTimer = millis();

  WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    if (millis() - animTimer > 200) {
      animStep++;
      updateWifiAnim();
      animTimer = millis();
    }
  }

  // ③ WiFi 接続完了
  localIP = WiFi.localIP().toString();
  appState = STATE_WIFI_CONNECTED;
  drawWifiConnected();
  delay(1800);

  // ④ ES8388 + I2S 初期化
  initES8388();

  auto cfg      = i2sOut.defaultConfig();
  cfg.pin_mck   = PIN_MCLK;
  cfg.pin_bck   = PIN_BCLK;
  cfg.pin_ws    = PIN_WS;
  cfg.pin_data  = PIN_DATA;
  i2sOut.begin(cfg);

  // ⑤ Snapclient 開始 (mDNS 自動検出)
  appState  = STATE_SERVER_SEARCHING;
  animStep  = 0;
  stateTimer = millis();
  animTimer  = millis();
  drawServerSearching();

  snapClient.begin(synch);
  // 以降は loop() で doLoop() を呼ぶ
}

// ═══════════════════════════════════════════════════════════════
//  loop
// ═══════════════════════════════════════════════════════════════
void loop() {
  M5.update();
  snapClient.doLoop();

  switch (appState) {

    // ── サーバー検索中 ──────────────────────────────────────
    case STATE_SERVER_SEARCHING:
      if (millis() - animTimer > 200) {
        animStep++;
        updateServerAnim();
        animTimer = millis();
      }
      // 3 秒後に接続完了画面へ
      // (実際の接続は doLoop() 内で非同期に処理される)
      if (millis() - stateTimer > 3000) {
        appState   = STATE_SERVER_CONNECTED;
        stateTimer = millis();
        drawServerConnected();
      }
      break;

    // ── サーバー接続完了 ────────────────────────────────────
    case STATE_SERVER_CONNECTED:
      if (millis() - stateTimer > 2000) {
        appState = STATE_STATUS;
        drawStatusScreen();
        clockTimer = millis();
      }
      break;

    // ── ステータス画面 ──────────────────────────────────────
    case STATE_STATUS:
      // 1 秒毎に稼働時間・シグナルバーを更新
      if (millis() - clockTimer > 1000) {
        updateStatusLive();
        clockTimer = millis();
      }

      // ボタン A: 輝度切替 (160 ↔ 255)
      if (M5.BtnA.wasPressed()) {
        dimmed = !dimmed;
        M5.Lcd.setBrightness(dimmed ? 80 : 160);
      }

      // ボタン C: Snapserver 再接続
      if (M5.BtnC.wasPressed()) {
        appState   = STATE_SERVER_SEARCHING;
        animStep   = 0;
        stateTimer = millis();
        animTimer  = millis();
        drawServerSearching();
        snapClient.begin(synch);
      }
      break;

    default:
      break;
  }
}

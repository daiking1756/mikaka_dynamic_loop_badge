/**
 * NTT ダイナミックループ アニメーション + 心拍数連動
 * ATOM Matrix (M5Stack) 5×5 LED Matrix
 *
 * 形状:
 *   . X X X .   row 0
 *   X X . X X   row 1  ← (1,1)(3,1) = 内側
 *   X . X . X   row 2  ← (2,2)      = 中心
 *   X . . . X   row 3
 *   . X X X .   row 4
 *
 * ─────────────────────────────────────────────────────
 * 一筆書き経路の設計 ― 小ループでグルっと一回転 (図8の交差あり)
 * ─────────────────────────────────────────────────────
 *
 * (2,0) を図8の「交差点」として2回通過することで、
 * 内側ピクセルを時計回りに ↘↙↖↗ と完全に一周する。
 *
 *   step 0: (1,0) → step 1: (2,0)   ← 上辺を右へ / 交差点 進入
 *   step 2: (3,1) → step 3: (2,2) → step 4: (1,1)
 *                                      ↑ ↘↙↖ (時計回りに内側をくぐる)
 *   step 5: (2,0)  ← ↗ で交差点に戻る = 小ループ一周完了!
 *   step 6: (3,0)  ← 交差点から上右へ抜ける
 *
 * ステップ番号マップ ((2,0) は [1,5] と表記):
 *   .   [0]  [1,5]  [6]   .
 *   [15] [4]   .    [2]   [7]
 *   [14]  .   [3]    .    [8]
 *   [13]  .    .     .    [9]
 *    .  [12] [11]  [10]   .
 *
 * ─────────────────────────────────────────────────────
 * 心拍数連動 + キャリブレーション
 * ─────────────────────────────────────────────────────
 *
 * BLE で Xiaomi Smart Band 10 の心拍数共有 (標準 Heart Rate Profile) を受信。
 * g_heartRate が 0 (未接続/未取得) のときはボタンで速度切り替え (従来動作)。
 * g_heartRate > 0 のときは心拍数でアニメーション速度・色を自動制御。
 *
 * ステージ判定:
 *   キャリブレーション未設定: 固定 BPM 閾値 (<70 / 70-89 / 90-109 / 110+)
 *   キャリブレーション設定後: 基準値からの上昇率で判定
 *     stage 0 (青)  :  0〜+14%   安静
 *     stage 1 (シアン): +15〜+34%  軽運動
 *     stage 2 (橙)  : +35〜+54%  中強度
 *     stage 3 (赤)  : +55%〜     高強度
 *
 * ボタン:
 *   BLE 接続中  → 現在の心拍数をキャリブレーション基準値として記録
 *                 (LEDが白く点滅してフィードバック)
 *   BLE 未接続時 → 手動速度切り替え
 */

#include "M5Atom.h"
#include <NimBLEDevice.h>
#include "config.h"  // BAND_BLE_ADDR を定義 (GitHub 非公開)

// ============================================================
// ---- BLE 設定 -----------------------------------------------
// ============================================================

// 標準 Heart Rate Profile (Bluetooth SIG 標準 UUID)
#define HR_SERVICE_UUID  "0000180d-0000-1000-8000-00805f9b34fb"
#define HR_CHAR_UUID     "00002a37-0000-1000-8000-00805f9b34fb"

// Xiaomi Smart Band 10 の BLE 広告アドレス (config.h で定義)
// ※ BLE では Static Random Address (type=1) で広告するため nRF Toolbox 表示値を使用
// v2.x: コンストラクタは (std::string, uint8_t type) の2引数が必要
static const NimBLEAddress TARGET_ADDR(std::string(BAND_BLE_ADDR), 1);

// 他タスク (BLE コールバック) から書き込まれるため volatile
static volatile int  g_heartRate  = 0;   // bpm (0 = 未取得)
static volatile bool g_connected  = false;
static volatile int  g_hrBaseline = 0;   // キャリブレーション基準値 (0 = 未設定)

static bool          g_doConnect  = false;
static NimBLEAddress g_foundAddr;         // スキャンで実際に見つけたアドレス (type 込み)
static NimBLEClient* g_client     = nullptr;

// ---- HR 通知コールバック ----------------------------------------
// Heart Rate Measurement 特性 (0x2A37) のフォーマット:
//   byte 0: Flags  bit0=0 → HR は uint8、bit0=1 → HR は uint16
//   byte 1 (or 1-2): HR 値
static void hrNotifyCallback(NimBLERemoteCharacteristic*, uint8_t* data, size_t len, bool) {
  if (len < 2) return;
  g_heartRate = (data[0] & 0x01) ? (int)(data[1] | (data[2] << 8)) : (int)data[1];
  Serial.print("[HR] ");
  Serial.print(g_heartRate);
  Serial.println(" bpm");
}

// ---- BLE 接続状態コールバック (NimBLE-Arduino v2.x API) ----------
class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient* client) override {
    g_connected = true;
    Serial.print("[BLE] Connected: ");
    Serial.println(client->getPeerAddress().toString().c_str());
  }
  // v2.x: 切断理由コード (reason) が追加された
  void onDisconnect(NimBLEClient* client, int reason) override {
    g_connected  = false;
    g_heartRate  = 0;
    g_doConnect  = false;
    Serial.print("[BLE] Disconnected, reason=");
    Serial.println(reason);
    NimBLEDevice::getScan()->start(0);
  }
};

// ---- BLE スキャンコールバック (NimBLE-Arduino v2.x API) ----------
class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    // 全デバイスを表示: Smart Band 10 の MAC と address type を確認する
    Serial.print("[SCAN] addr=");
    Serial.print(dev->getAddress().toString().c_str());
    Serial.print(" type=");
    Serial.print(dev->getAddress().getType());
    Serial.print(" rssi=");
    Serial.println(dev->getRSSI());

    // アドレス文字列だけで比較 (type を無視)
    if (dev->getAddress().toString() == std::string(BAND_BLE_ADDR)) {
      Serial.print("[BLE] Smart Band 10 found! type=");
      Serial.println(dev->getAddress().getType());
      g_foundAddr = dev->getAddress();  // type 込みで保存
      NimBLEDevice::getScan()->stop();
      g_doConnect = true;
    }
  }
};

// ---- デバイスへの接続 -------------------------------------------
static void connectToHRDevice() {
  Serial.println("[BLE] Connecting to Smart Band 10...");

  g_client = NimBLEDevice::createClient();
  g_client->setClientCallbacks(new ClientCB(), false);

  if (!g_client->connect(g_foundAddr)) {
    Serial.println("[BLE] Connect failed, restarting scan");
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    NimBLEDevice::getScan()->start(0);
    return;
  }

  auto* svc = g_client->getService(HR_SERVICE_UUID);
  if (!svc) {
    Serial.println("[BLE] HR service not found");
    g_client->disconnect();
    return;
  }

  auto* chr = svc->getCharacteristic(HR_CHAR_UUID);
  if (!chr || !chr->canNotify()) {
    Serial.println("[BLE] HR characteristic not found or not notifiable");
    g_client->disconnect();
    return;
  }

  chr->subscribe(true, hrNotifyCallback);
  Serial.println("[BLE] Subscribed to HR notifications");
}

// ---- BLE 初期化 -------------------------------------------------
static void bleSetup() {
  NimBLEDevice::init("");
  Serial.println("[BLE] Scanning for HR device...");

  auto* scan = NimBLEDevice::getScan();
  // v2.x: setAdvertisedDeviceCallbacks → setScanCallbacks
  scan->setScanCallbacks(new ScanCB());
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(15);
  scan->start(0);  // 0 = 無限スキャン
}

// ============================================================
// ---- LED アニメーション定数 -----------------------------------
// ============================================================

static constexpr int PATH_LEN = 16;
static constexpr int TAIL_LEN = 7;

// ボタンで切り替える手動速度 (BLE 未接続時のフォールバック)
static const int SPEEDS[]  = {200, 130, 80, 50};
static const int SPEED_NUM = 4;

// ---- 一筆書き経路 -----------------------------------------------

static const int8_t PATH[PATH_LEN][2] = {
  {1,0},               // step  0: 上左 ← スタート
  {2,0},               // step  1: 上中央 (交差点 進入)
  {3,1},{2,2},{1,1},   // step  2- 4: 内側弧 ↘↙↖ (時計回り)
  {2,0},               // step  5: 上中央 ↗ (交差点 再通過 = 小ループ一周!)
  {3,0},               // step  6: 上右 →
  {4,1},{4,2},{4,3},   // step  7- 9: 右辺 ↓
  {3,4},{2,4},{1,4},   // step 10-12: 下辺 ←
  {0,3},{0,2},{0,1},   // step 13-15: 左辺 ↑
};

// ---- カラーパレット (心拍数 4段階: ブルー → シアン → オレンジ → レッド) ----

struct HRPalette {
  CRGB head;
  CRGB tail[TAIL_LEN];
  CRGB bg;
};

static const HRPalette PALETTES[4] = {
  { // stage 0: 安静 < 70 bpm → ブルー (NTT カラー)
    CRGB(220, 245, 255),
    { CRGB(  0, 190, 255), CRGB(  0, 140, 230), CRGB(  0,  90, 190),
      CRGB(  0,  50, 140), CRGB(  0,  22,  80), CRGB(  0,   8,  30),
      CRGB(  0,   2,   8) },
    CRGB(  0,   0,  12),
  },
  { // stage 1: 軽運動 70-89 bpm → シアン
    CRGB(180, 255, 220),
    { CRGB(  0, 230, 160), CRGB(  0, 180, 120), CRGB(  0, 120,  80),
      CRGB(  0,  65,  40), CRGB(  0,  28,  16), CRGB(  0,  10,   5),
      CRGB(  0,   3,   1) },
    CRGB(  0,  10,   5),
  },
  { // stage 2: 運動 90-109 bpm → オレンジ
    CRGB(255, 200,  50),
    { CRGB(255, 110,   0), CRGB(220,  75,   0), CRGB(170,  45,   0),
      CRGB(110,  22,   0), CRGB( 60,   8,   0), CRGB( 22,   2,   0),
      CRGB(  6,   0,   0) },
    CRGB( 10,   4,   0),
  },
  { // stage 3: 高強度 110+ bpm → レッド
    CRGB(255, 100,  60),
    { CRGB(255,  30,   0), CRGB(210,  12,   0), CRGB(160,   4,   0),
      CRGB(100,   0,   0), CRGB( 55,   0,   0), CRGB( 20,   0,   0),
      CRGB(  6,   0,   0) },
    CRGB( 10,   0,   0),
  },
};

// ============================================================
// ---- 心拍数 → ステージ変換 (速度・色を一括管理) ---------------
// ============================================================

// 戻り値 0-3 がそのまま PALETTES と SPEEDS のインデックスになる
static int hrToStage(int bpm) {
  if (g_hrBaseline > 0) {
    // キャリブレーション済み: 基準値からの上昇率 (%) で判定
    int pct = (bpm - g_hrBaseline) * 100 / g_hrBaseline;
    if (pct < 15) return 0;
    if (pct < 35) return 1;
    if (pct < 55) return 2;
    return 3;
  }
  // 未キャリブレーション: 固定 BPM 閾値
  if (bpm <  70) return 0;
  if (bpm <  90) return 1;
  if (bpm < 110) return 2;
  return 3;
}

// ============================================================
// ---- ユーティリティ / 描画 ------------------------------------
// ============================================================

static inline int ledIdx(int x, int y) { return y * 5 + x; }

static void setPixel(int x, int y, CRGB color) {
  M5.dis.drawpix(ledIdx(x, y), color);
}

static void drawComet(int head, int stage) {
  const HRPalette& pal = PALETTES[stage];
  for (int t = TAIL_LEN; t >= 1; t--) {
    int s = (head - t + PATH_LEN) % PATH_LEN;
    setPixel(PATH[s][0], PATH[s][1], pal.tail[t - 1]);
  }
  setPixel(PATH[head][0], PATH[head][1], pal.head);
}

// ============================================================
// ---- グローバル状態 -------------------------------------------
// ============================================================

static int step     = 0;
static int speedIdx = 1;

// ============================================================
// ---- Arduino エントリポイント ---------------------------------
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(1500);  // シリアルモニタが接続されるのを待つ
  Serial.println("=== Dynamic Loop Badge starting ===");

  M5.begin(true, false, true);
  delay(50);
  M5.dis.setBrightness(50);
  bleSetup();
}

void loop() {
  M5.update();

  // BLE 接続要求の処理 (スキャンコールバックから設定される)
  if (g_doConnect) {
    g_doConnect = false;
    connectToHRDevice();
  }

  // ボタン: BLE 接続中 → キャリブレーション、未接続時 → 手動速度切り替え
  if (M5.Btn.wasPressed()) {
    if (g_connected && g_heartRate > 0) {
      g_hrBaseline = g_heartRate;
      Serial.print("[CAL] Baseline set to ");
      Serial.print(g_hrBaseline);
      Serial.println(" bpm");
      // 白点滅でキャリブレーション完了をフィードバック
      for (int i = 0; i < 25; i++) M5.dis.drawpix(i, CRGB::White);
      delay(200);
    } else if (!g_connected) {
      speedIdx = (speedIdx + 1) % SPEED_NUM;
    }
  }

  // ステージ決定: 心拍数あり → HR 連動、なし → 手動速度に対応するステージ
  int stage = (g_heartRate > 0) ? hrToStage(g_heartRate) : speedIdx;
  const HRPalette& pal = PALETTES[stage];
  int frameDelay = SPEEDS[stage];

  // 1. 全消灯
  for (int i = 0; i < 25; i++) {
    M5.dis.drawpix(i, CRGB::Black);
  }

  // 2. 形状の輪郭を背景色で常時表示
  for (int i = 0; i < PATH_LEN; i++) {
    setPixel(PATH[i][0], PATH[i][1], pal.bg);
  }

  // 3. 2個のコメットが追走 (offset = PATH_LEN/2 = 8)
  drawComet((step + 8) % PATH_LEN, stage);
  drawComet(step % PATH_LEN, stage);

  step++;
  delay(frameDelay);
}

/**
 * NTT ダイナミックループ アニメーション
 * ATOM Matrix (M5Stack) 5×5 LED Matrix
 *
 * LED 座標系 (x, y):  (0,0)=左上, (4,4)=右下
 *
 * 形状 (ユーザー指定):
 *   . X X X .   row 0
 *   X X . X X   row 1  ← (1,1)(3,1) = 内側ピクセル
 *   X . X . X   row 2  ← (2,2)      = 中心ピクセル
 *   X . . . X   row 3
 *   . X X X .   row 4
 *
 * ループ構成 (figure-8):
 *   上ループ(時計回り): 上辺 → (4,1) → 内側対角(3,1)(2,2)(1,1) → (0,1)
 *   下ループ(反時計回り): (0,2) → 左側↓ → 下辺→ → 右側↑ → (4,1)
 *   交差点: (4,1) = 両ループが同フレームに到着
 *
 * ボタン: 速度切り替え
 */

#include "M5Atom.h"

// ---- 定数 ----------------------------------------------------------------

static constexpr int LOOP_LEN = 8;
static constexpr int TAIL_LEN = 3;

static const int SPEEDS[]  = {160, 100, 65, 40};
static const int SPEED_NUM = 4;

// ---- 形状・経路定義 -------------------------------------------------------

// 全ピクセル (15個)
static const int8_t SHAPE[15][2] = {
  {1,0},{2,0},{3,0},           // row 0
  {0,1},{1,1},{3,1},{4,1},     // row 1
  {0,2},{2,2},{4,2},           // row 2
  {0,3},{4,3},                 // row 3
  {1,4},{2,4},{3,4}            // row 4
};

/**
 * 上ループ経路 (時計回り, 8ステップ)
 *
 *   . 0→1→2 .
 *   7       3
 *   . 6↖ . ↘4 .   ← 内側対角を通る
 *   .       .
 *   .       .
 *
 * (4,1) = step 3 が下ループとの交差点
 */
static const int8_t TOP_LOOP[LOOP_LEN][2] = {
  {1,0},{2,0},{3,0},{4,1},   // 上辺→右 (step 0-3)
  {3,1},{2,2},{1,1},{0,1}    // 内側対角←左 (step 4-7)
};

/**
 * 下ループ経路 (反時計回り, 8ステップ)
 *
 *   .       .
 *   0       7 ← (4,1) = step 7 が上ループとの交差点
 *   1       6
 *   2       5
 *   . 3→4→5→ .
 *
 * 上ループ step 3 と同フレームに step 7=(4,1) に到達
 */
static const int8_t BOT_LOOP[LOOP_LEN][2] = {
  {0,2},{0,3},{1,4},{2,4},   // 左側↓→下辺 (step 0-3)
  {3,4},{4,3},{4,2},{4,1}    // 下辺→右側↑ (step 4-7)
};

// ---- カラーパレット --------------------------------------------------------

static const CRGB COL_HEAD    = CRGB(210, 240, 255);
static const CRGB COL_TAIL[TAIL_LEN] = {
  CRGB(  0, 150, 255),
  CRGB(  0,  55, 180),
  CRGB(  0,  10,  50),
};
static const CRGB COL_BG = CRGB(0, 0, 18);

// ---- ユーティリティ -------------------------------------------------------

static inline int ledIdx(int x, int y) { return y * 5 + x; }

static void setPixel(int x, int y, CRGB color) {
  M5.dis.drawpix(ledIdx(x, y), color);
}

// ---- コメット描画 ---------------------------------------------------------

static void drawComet(const int8_t path[][2], int head) {
  for (int t = TAIL_LEN; t >= 1; t--) {
    int s = (head - t + LOOP_LEN) % LOOP_LEN;
    setPixel(path[s][0], path[s][1], COL_TAIL[t - 1]);
  }
  setPixel(path[head][0], path[head][1], COL_HEAD);
}

// ---- グローバル状態 -------------------------------------------------------

static int step     = 0;
static int speedIdx = 1;

// ---- Arduino エントリポイント --------------------------------------------

void setup() {
  M5.begin(true, false, true);
  delay(50);
  M5.dis.setBrightness(50);
}

void loop() {
  M5.update();

  if (M5.Btn.wasPressed()) {
    speedIdx = (speedIdx + 1) % SPEED_NUM;
  }

  // 1. 全消灯
  for (int i = 0; i < 25; i++) {
    M5.dis.drawpix(i, CRGB::Black);
  }

  // 2. 形状を背景色で表示
  for (int i = 0; i < 15; i++) {
    setPixel(SHAPE[i][0], SHAPE[i][1], COL_BG);
  }

  // 3. 下ループのコメットを先に描画 (上ループが交差点で上書き)
  drawComet(BOT_LOOP, (step + 4) % LOOP_LEN);

  // 4. 上ループのコメットを後から描画
  //    offset=4: 上 step3=(4,1) と 下 step7=(4,1) が同フレームに交差
  drawComet(TOP_LOOP, step % LOOP_LEN);

  step++;
  delay(SPEEDS[speedIdx]);
}

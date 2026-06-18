/**
 * NTT ダイナミックループ アニメーション
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
 * step 5 (2,0) に HEAD が到達した瞬間、
 * TAIL=7 なら HEAD+TAIL の 8ピクセルが小ループを完全に照らす:
 *   (0,2)-(0,3)-(1,0)-(2,0)-(3,1)-(2,2)-(1,1)-(2,0)
 *   ※(2,0) は HEAD と tail-4 が重なり → HEAD色で最高輝度に点灯 (交差の光)
 *
 * その後 step 6〜15 で外周 (上右→右辺↓→下辺←→左辺↑) を反時計回り。
 *
 * ステップ番号マップ ((2,0) は [1,5] と表記):
 *   .   [0]  [1,5]  [6]   .
 *   [15] [4]   .    [2]   [7]
 *   [14]  .   [3]    .    [8]
 *   [13]  .    .     .    [9]
 *    .  [12] [11]  [10]   .
 *
 * ボタン: 速度切り替え
 */

#include "M5Atom.h"

// ---- 定数 ----------------------------------------------------------------

static constexpr int PATH_LEN = 16;
static constexpr int TAIL_LEN = 7;

static const int SPEEDS[]  = {200, 130, 80, 50};
static const int SPEED_NUM = 4;

// ---- 一筆書き経路 ----------------------------------------------------------

/**
 * 図8経路: PATH_LEN=16 ((2,0) を交差点として2回通過)
 *
 * 内側を時計回りに ↘↙↖↗ と一周:
 *   step1=(2,0) → ↘ → step2=(3,1) → ↙ → step3=(2,2)
 *               → ↖ → step4=(1,1) → ↗ → step5=(2,0) [交差点に戻る]
 *
 * 隣接チェック (距離 ≤ √2):
 *   (1,0)↔(2,0)=1,  (2,0)↔(3,1)=√2, (3,1)↔(2,2)=√2, (2,2)↔(1,1)=√2,
 *   (1,1)↔(2,0)=√2, (2,0)↔(3,0)=1,  (3,0)↔(4,1)=√2, (4,1)↔(4,2)=1,
 *   (4,2)↔(4,3)=1,  (4,3)↔(3,4)=√2, (3,4)↔(2,4)=1,  (2,4)↔(1,4)=1,
 *   (1,4)↔(0,3)=√2, (0,3)↔(0,2)=1,  (0,2)↔(0,1)=1,  (0,1)↔(1,0)=√2 ← 閉じる ✓
 */
static const int8_t PATH[PATH_LEN][2] = {
  {1,0},               // step  0: 上左 ← スタート
  {2,0},               // step  1: 上中央 (交差点 進入)
  {3,1},{2,2},{1,1},   // step  2- 4: 内側弧 ↘↙↖ (時計回り)
  {2,0},               // step  5: 上中央 ↗ (交差点 再通過 = 小ループ一周!)
  {3,0},               // step  6: 上右 →
  {4,1},{4,2},{4,3},   // step  7- 9: 右辺 ↓
  {3,4},{2,4},{1,4},   // step 10-12: 下辺 ←
  {0,3},{0,2},{0,1},   // step 13-15: 左辺 ↑
  // → (1,0) へ戻る (距離 √2)
};

// ---- カラーパレット (NTT ブルー系) ----------------------------------------

static const CRGB COL_HEAD    = CRGB(220, 245, 255);
static const CRGB COL_TAIL[TAIL_LEN] = {
  CRGB(  0, 190, 255),   // tail-1
  CRGB(  0, 140, 230),   // tail-2
  CRGB(  0,  90, 190),   // tail-3
  CRGB(  0,  50, 140),   // tail-4
  CRGB(  0,  22,  80),   // tail-5
  CRGB(  0,   8,  30),   // tail-6
  CRGB(  0,   2,   8),   // tail-7
};
static const CRGB COL_BG = CRGB(0, 0, 12);

// ---- ユーティリティ -------------------------------------------------------

static inline int ledIdx(int x, int y) { return y * 5 + x; }

static void setPixel(int x, int y, CRGB color) {
  M5.dis.drawpix(ledIdx(x, y), color);
}

// ---- コメット描画 ---------------------------------------------------------

static void drawComet(int head) {
  for (int t = TAIL_LEN; t >= 1; t--) {
    int s = (head - t + PATH_LEN) % PATH_LEN;
    setPixel(PATH[s][0], PATH[s][1], COL_TAIL[t - 1]);
  }
  setPixel(PATH[head][0], PATH[head][1], COL_HEAD);
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

  // 2. 形状の輪郭を背景色で常時表示
  for (int i = 0; i < PATH_LEN; i++) {
    setPixel(PATH[i][0], PATH[i][1], COL_BG);
  }

  // 3. 2個のコメットが一筆書き経路を追走 (offset = PATH_LEN/2 = 8)
  //    各コメットが head+tail7 の8ステップを占め、全16ステップをピッタリ分担
  drawComet((step + 8) % PATH_LEN);  // 先行コメット (先に描画 = 後続に上書きされる)
  drawComet(step % PATH_LEN);         // 後続コメット

  step++;
  delay(SPEEDS[speedIdx]);
}

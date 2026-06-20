# Dynamic Loop Badge

ATOM Matrix (M5Stack) の 5×5 LED マトリクスで NTT ダイナミックループを表現するバッジです。  
Xiaomi Smart Band 10 から BLE で心拍数を取得し、アニメーションの速度と色をリアルタイムに変化させます。

## ハードウェア

- [ATOM Matrix](https://docs.m5stack.com/en/core/atom_matrix) (M5Stack)
- Xiaomi Smart Band 10（心拍数共有をオン）

## 開発環境

- Arduino IDE
- ライブラリ（Arduino IDE のライブラリマネージャからインストール）
  - M5Atom
  - FastLED
  - NimBLE-Arduino

## セットアップ

```bash
cp src/dynamic-loop/config.h.example src/dynamic-loop/config.h
```

`config.h` を開き、`BAND_BLE_ADDR` に Xiaomi Smart Band 10 の BLE 広告アドレスを設定してください。  
アドレスは nRF Toolbox アプリのスキャン画面で確認できます（Mi Fitness アプリの表示とは異なります）。

```cpp
#define BAND_BLE_ADDR  "xx:xx:xx:xx:xx:xx"
```

## 機能

### アニメーション

ダイナミックループ形状を 2 つのコメットが一筆書きで追走します。  
内側の小ループで時計回りに一回転する経路（16 ステップ）を採用しています。

### 心拍数連動（4 段階）

| ステージ | 色 | 固定閾値（未設定時） | キャリブレーション後 |
|---------|-----|-------------------|-----------------|
| 0 | 青 | ＜ 70 bpm | ＋ 0〜14% |
| 1 | シアン | 70〜89 bpm | ＋15〜34% |
| 2 | オレンジ | 90〜109 bpm | ＋35〜54% |
| 3 | 赤 | 110 bpm〜 | ＋55%〜 |

### ボタン操作

| 状態 | ボタン押下 |
|------|----------|
| BLE 接続中 | 現在の心拍数をキャリブレーション基準値に設定（全 LED 白点滅でフィードバック） |
| BLE 未接続時 | アニメーション速度を手動切り替え |

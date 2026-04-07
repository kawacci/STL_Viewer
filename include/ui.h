#ifndef UI_H
#define UI_H

#include <M5Unified.h>
#include <vector>
#include <map>
#include "model.h"

// ColorOption構造体の定義
struct ColorOption
{
    uint16_t color;
    int x;
};

// カラーパレット用の色と位置
extern ColorOption colorPalette[6];

// UI関連関数の宣言
void drawLauncher();     // ランチャー画面を描画する関数
String selectModel();    // モデル選択のためのランチャーを表示し、選択されたファイルのフルパスを返す関数
void drawUI_Ver2();      // モデル描画エリアとUIを更新する関数
void IMUupdate();        // IMUセンサーの値をチェックして画面の回転を更新する関数
void colorchange(int i); // カラーパレットの色が選択されたときにモデルの色を変更してUIを更新する関数
void touchLCD();         // タッチ操作を処理する関数
void readIMG(int index); // 画像ファイルを読み込んで描画する関数

// 外部参照するグローバル変数
extern int currentRotation;
extern String currentFileName;
extern std::vector<Triangle> model;
extern uint16_t baseColor;
extern bool isAutoMode;
extern uint32_t lastTouchTime;
extern const uint32_t AUTO_RETURN_MS;
extern m5::touch_point_t tp[5];
extern int prev_touch_count;
extern float prev_pinch_dist;
extern int last_x, last_y;
extern int localX, localY;
extern float modelScale;
extern float baseScale;
extern float mat[3][3];
extern LGFX_Sprite canvasUI;
extern uint8_t gIMGBuf[16384];
extern const char *imagePaths[2];
extern long img_buffer;
extern int currentImageIndex;
extern const uint16_t rotaionXY[4][5];
extern const uint16_t rotaionQR[4][3];
extern const int BtnshiftRange[4][2];
extern const int AutoshiftRange[4][2];

#endif
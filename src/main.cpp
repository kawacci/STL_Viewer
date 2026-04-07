#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <math.h>
#include <map>
#include "ui.h"
#include "model.h"

// 追加: STL/OBJのグローバル変数定義
std::vector<Triangle> model;           // 読み込んだ三角形リスト
std::map<String, uint16_t> materialMap; // MTL材料マップ
uint16_t currentMeshColor = 0xCCCC;    // OBJマテリアルのデフォルト色

#define SD_SPI_CS_PIN 42
#define SD_SPI_SCK_PIN 43
#define SD_SPI_MOSI_PIN 44
#define SD_SPI_MISO_PIN 39

// 画面レイアウト: 上部720x720がモデル表示領域、下部720x560がUI領域
#define STL_OFFSET_Y 0
#define UI_OFFSET_Y 720
#define TILE_W 720
#define TILE_H 80
#define TILE_COUNT 9

// UIスプライトの回転時に使う座標と回転角度
const uint16_t rotaionXY[4][5] = {
    {360, 820, 360, 1180, 0},    // 0度
    {180, 1000, 540, 1000, 270}, // 90度
    {360, 1180, 360, 820, 180},  // 180度
    {540, 1000, 180, 1000, 90}   // 270度
};

// QRコード表示用の回転座標
const uint16_t rotaionQR[4][3] = {
    {360, 1040, 0},   // 0度
    {400, 1000, 270}, // 90度
    {360, 960, 180},  // 180度
    {320, 1000, 90}   // 270度
};

int currentRotation = 0; // 0=正立, 1=右向き, 2=逆さま, 3=左向き

// UIエリア上のタッチ座標を回転表示に合わせて変換するためのシフト量
const int BtnshiftRange[4][2] = {{480, 80}, {560, -560}, {80, -640}, {160, 0}};
const int AutoshiftRange[4][2] = {{545, 635}, {5, 95}, {85, 175}, {465, 555}};
int localX, localY; // 回転後のUI領域内ローカル座標

LGFX_Sprite canvas(&M5.Display);   // 3Dモデル描画用スプライト
LGFX_Sprite canvasUI(&M5.Display); // UI描画用スプライト

float baseScale = 3500.0f; // 読み込み時に使う基準スケール

// QRコードなどの画像ファイルパス128x128pxの画像を用意しておいてください。例: "/x_wb_128.png", "/github_wb_128.png"
const char *imagePaths[] = {
    "/x_wb_128.png",
    "/github_wb_128.png",
};
long img_buffer = 16384;   // 画像読み込み用バッファサイズ
int currentImageIndex = 0; // 表示イメージの番号

String currentFilePath; // 読み込むファイルパス
String currentFileName = "";           // 画面に表示するファイル名

float mat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // モデルの回転行列
float modelScale = 3500.0f;                          // モデル描画スケール
float offsetX, offsetY, offsetZ;                     // モデル中心座標オフセット

bool isAutoMode = true;                // 自動回転モード
uint32_t lastTouchTime = 0;            // 最後にタッチがあった時刻
const uint32_t AUTO_RETURN_MS = 10000; // タッチなしで自動回転に戻る時間

uint16_t baseColor = TFT_WHITE; // STL色変更用

// カラーパレット用の色と位置
ColorOption colorPalette[] = {
    {TFT_WHITE, 40},
    {TFT_SILVER, 115},
    {TFT_ORANGE, 190},
    {TFT_YELLOW, 265},
    {TFT_GREENYELLOW, 340},
    {TFT_CYAN, 415},
};

m5::touch_point_t tp[5]; // タッチ入力の記録(最大5点)
int prev_touch_count = 0; // 前回のタッチ点数
float prev_pinch_dist = 0; // 前回のピンチ距離
int last_x, last_y;       // 前回タッチ位置

const float lightDir[3] = {0.577f, 0.577f, 0.577f}; // 擬似光源方向

std::vector<String> fileList; // SDカード内のSTL/OBJファイル一覧

void setup()
{
    // シリアル通信の開始（M5.beginより先に呼ぶのが確実です）
    Serial.begin(115200);
    // USB CDCがPCに認識されるまで最大0.5秒待機
    uint32_t start = millis();
    while (!Serial && (millis() - start) < 500)
    {
        delay(10);
    }

    Serial.println("\n--- Serial Started ---");

    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(0); // 向きを 0 に固定
    M5.Display.fillScreen(TFT_BLACK);

    // SDカードの初期化
    Serial.println("Initializing SD Card...");
    
    // M5Tab5用のピン設定で明示的に初期化
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000))  // 速度を25MHzから20MHzに低下
    {
        Serial.println("SD Card Error!");
        M5.Display.setCursor(10, 10);
        M5.Display.print("SD Card Error");
    }
    else
    {
        Serial.println("SD Card OK");
    }

    // 1. ファイルリストを取得（実装済みの関数）
    Serial.println("=== Calling updateFileList() ===");
    updateFileList();
    Serial.printf("After updateFileList: fileList.size() = %d\n", (int)fileList.size());

    // 2. 起動時にランチャーでファイルを選択させる
    String selected = selectModel();
    currentFilePath = selected; // updateFileList()がフルパスを返すようになったため、そのまま使用

    // モデルの読み込み（loadSTL/loadOBJを自動判別）
    Serial.printf("Loading model: %s\n", currentFilePath.c_str());
    if (loadModel(currentFilePath.c_str()))
    {
        // 成功：loadOBJ内で計算された modelScale がそのまま使われます
        Serial.printf("Model Loaded Success. Final Scale: %f\n", modelScale);
    }
    else
    {
        // 失敗：デフォルト値をセット
        Serial.println("Model Load Failed. Using default scale.");
        modelScale = 3500.0f;
    }

    // UI用スプライトの設定
    canvasUI.setColorDepth(16);
    canvasUI.createSprite(560, 200);
    drawUI_Ver2();

    // 描画用スプライトの設定
    canvas.setColorDepth(16);
    canvas.createSprite(TILE_W, TILE_H);

    lastTouchTime = millis();
    Serial.println("Setup Complete.");
}

void loop()
{
    M5.update();

    touchLCD();  // タッチと自動回転の両方をこの関数内で処理
    IMUupdate(); // IMUの更新と自動回転の判定もこの関数内で処理

    if (isAutoMode)
    {
        float dx = 0.008f, dy = 0.005f;
        float rotX[3][3] = {{1, 0, 0}, {0, cos(dy), -sin(dy)}, {0, sin(dy), cos(dy)}};
        float rotY[3][3] = {{cos(dx), 0, sin(dx)}, {0, 1, 0}, {-sin(dx), 0, cos(dx)}};
        float tempRot[3][3];
        matMultiply(rotX, rotY, tempRot);
        matMultiply(tempRot, mat, mat);
    }

    // レンダリング処理
    struct DrawTri
    {
        int px[3], py[3], minY, maxY;
        float avgZ;
        uint16_t color;
    };
    std::vector<DrawTri> drawList;

    // ライトの向きの事前計算（ mat を考慮した方向ベクトル）
    float lx = 0.0f * mat[0][0] + 0.0f * mat[1][0] + 1.0f * mat[2][0];
    float ly = 0.0f * mat[0][1] + 0.0f * mat[1][1] + 1.0f * mat[2][1];
    float lz = 0.0f * mat[0][2] + 0.0f * mat[1][2] + 1.0f * mat[2][2];

    drawList.reserve(model.size());

    for (const auto &tri : model)
    {
        // 1. 法線を現在の回転行列で回転させる
        float rnx = tri.normal[0] * mat[0][0] + tri.normal[1] * mat[0][1] + tri.normal[2] * mat[0][2];
        float rny = tri.normal[0] * mat[1][0] + tri.normal[1] * mat[1][1] + tri.normal[2] * mat[1][2];
        float rnz = tri.normal[0] * mat[2][0] + tri.normal[1] * mat[2][1] + tri.normal[2] * mat[2][2];

        // 2. 輝度(dot)計算
        // カメラの正面(Z方向)からの光をベースにする
        float dot = fabsf(rnz) * 0.8f + fabsf(rnx) * 0.2f;
        float intensity = std::max(0.15f, dot); // 環境光 0.15f

        // 変更点：モデルごとの tri.color を使って色情報を反映
        uint16_t baseCol = tri.color;

        // RGB565(16bit) を分解
        uint8_t r8 = ((baseCol >> 11) & 0x1F) << 3;
        uint8_t g8 = ((baseCol >> 5) & 0x3F) << 2;
        uint8_t b8 = (baseCol & 0x1F) << 3;

        // 明るさを適用して再合成
        uint16_t faceColor = M5.Display.color565(
            (uint8_t)(r8 * intensity),
            (uint8_t)(g8 * intensity),
            (uint8_t)(b8 * intensity));

        DrawTri dt;
        float sumZ = 0;
        for (int i = 0; i < 3; i++)
        {
            // オフセット（モデルの中心）を引いて回転
            float vx = tri.v[i][0] - offsetX;
            float vy = tri.v[i][1] - offsetY;
            float vz = tri.v[i][2] - offsetZ;

            float rx = vx * mat[0][0] + vy * mat[0][1] + vz * mat[0][2];
            float ry = vx * mat[1][0] + vy * mat[1][1] + vz * mat[1][2];
            float rz = vx * mat[2][0] + vy * mat[2][1] + vz * mat[2][2];

            // 透視投影
            float s = modelScale / (rz + 800.0f);
            dt.px[i] = (int)(rx * s) + 360; // 720pxの半分
            dt.py[i] = (int)(ry * s) + 360;
            sumZ += rz;
        }

        // 背面カリング（表裏判定）
        if (((long)(dt.px[1] - dt.px[0]) * (dt.py[2] - dt.py[0]) - (long)(dt.py[1] - dt.py[0]) * (dt.px[2] - dt.px[0])) > 0)
            continue;

        dt.avgZ = sumZ / 3.0f;
        dt.color = faceColor;
        dt.minY = std::min({dt.py[0], dt.py[1], dt.py[2]});
        dt.maxY = std::max({dt.py[0], dt.py[1], dt.py[2]});
        drawList.push_back(dt);
    }

    // Zソート（奥から順に描画）
    std::sort(drawList.begin(), drawList.end(), [](const DrawTri &a, const DrawTri &b)
              { return a.avgZ < b.avgZ; });

    // タイル分割描画（メモリ節約用）
    for (int i = 0; i < TILE_COUNT; i++)
    {
        int y_start = i * TILE_H;
        canvas.fillSprite(TFT_BLACK);
        for (const auto &dt : drawList)
        {
            // タイルの描画範囲内にある三角形のみ描画
            if (dt.maxY >= y_start && dt.minY < y_start + TILE_H)
            {
                canvas.fillTriangle(
                    dt.px[0], dt.py[0] - y_start,
                    dt.px[1], dt.py[1] - y_start,
                    dt.px[2], dt.py[2] - y_start,
                    dt.color);
            }
        }
        canvas.pushSprite(0, y_start);
    }
}
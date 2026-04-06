#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <math.h>

#define SD_SPI_CS_PIN 42
#define SD_SPI_SCK_PIN 43
#define SD_SPI_MOSI_PIN 44
#define SD_SPI_MISO_PIN 39

// 画面向き 0 (標準) でのレイアウト
// 上部: STL表示 (0-719) / 下部: UI領域 (720-1279)
#define STL_OFFSET_Y 0
#define UI_OFFSET_Y 720
#define TILE_W 720
#define TILE_H 80
#define TILE_COUNT 9

// UIスプライトの回転軸の座標（720x1280の画面に対して）
const uint16_t rotaionXY[4][5] = {
    {360, 820, 360, 1180, 0},    // 0度
    {180, 1000, 540, 1000, 270}, // 90度
    {360, 1180, 360, 820, 180},  // 180度
    {540, 1000, 180, 1000, 90}   // 270度
};
// QRコードの回転軸の座標（720x1280の画面に対して)
const uint16_t rotaionQR[4][3] = {
    {360, 1040, 0},   // 0度
    {400, 1000, 270}, // 90度
    {360, 960, 180},  // 180度
    {320, 1000, 90}   // 270度
};

int currentRotation = 0; // 0:正立, 1:右向き, 2:逆さま, 3:左向き

const int BtnshiftRange[4][2] = {{480, 80}, {560, -560}, {80, -640}, {160, 0}}; // 各回転でのUIエリアのシフト範囲（UIエリア560X560の原点）
const int AutoshiftRange[4][2] = {{545, 635}, {5, 95}, {85, 175}, {465, 555}};  // AUTOボタンのY座標のシフト範囲（UIエリア内での位置）
int localX, localY;                                                             // タッチ座標をUIエリアのローカル座標に変換するための変数

LGFX_Sprite canvas(&M5.Display);   // STL描画用の720x720スプライト
LGFX_Sprite canvasUI(&M5.Display); // UI用の560x560スプライト

// 画像ファイルの設定
const char *imagePaths[] = {
    /**/ "/x_wb_128.png",
    /**/ "/github_wb_128.png",
}; // 画像ファイルのパス（必要に応じて追加）
long img_buffer = 16384;   // 画像読み込み用のバッファサイズ（必要に応じて調整）
int currentImageIndex = 0; // 表示イメージの番号

struct Triangle // STLファイルの三角形データ構造
{
    float v[3][3];   // 頂点座標
    float normal[3]; // 法線ベクトル
};
std::vector<Triangle> model;           // STLモデルの三角形リスト
String currentFilePath = "/M5-3D.stl"; // 読み込むファイルパス
String currentFileName = "";           // 表示用の名前（自動で入る）

float mat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // モデル変換用の行列
float modelScale = 3500.0f;                          // モデルのスケーリング係数
float offsetX, offsetY, offsetZ;                     // モデルの中心オフセット

bool isAutoMode = true;                // 自動回転モードのフラグ
uint32_t lastTouchTime = 0;            // 最後にタッチがあった時間を記録する変数
const uint32_t AUTO_RETURN_MS = 10000; // タッチがない状態で自動回転に戻るまでの時間（10秒など）

uint16_t baseColor = TFT_WHITE;
struct ColorOption
{
    uint16_t color;
    int x;
};

// 色とx座標のペアを6個定義
ColorOption colorPalette[] = {
    {TFT_WHITE, 40},        // 1つ目
    {TFT_SILVER, 115},      // 2つ目
    {TFT_ORANGE, 190},      // 3つ目
    {TFT_YELLOW, 265},      // 4つ目
    {TFT_GREENYELLOW, 340}, // 5つ目
    {TFT_CYAN, 415},        // 6つ目
};

m5::touch_point_t tp[5]; // タッチポイントの配列（最大5点まで）
int prev_touch_count = 0;
float prev_pinch_dist = 0;
int last_x, last_y;

const float lightDir[3] = {0.577f, 0.577f, 0.577f};

void matMultiply(float A[3][3], float B[3][3], float C[3][3])
{
    float res[3][3] = {0};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            for (int k = 0; k < 3; k++)
                res[i][j] += A[i][k] * B[k][j];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            C[i][j] = res[i][j];
}

// 画像をSDカードから読み込む
uint8_t gIMGBuf[16384]; // 画像読み込み用のバッファ（必要に応じてサイズを調整）
void readIMG(int index) // 画像をSDカードから読み込む関数
{
    currentImageIndex = index;                      // 表示する画像の番号を更新
    auto imagePath = imagePaths[currentImageIndex]; // 表示する画像のパスを取得
    File file = SD.open(imagePath, FILE_READ);      // 画像ファイルを開く
    file.read(gIMGBuf, img_buffer);                 // 画像データをバッファに読み込む
    file.close();
}

// STLファイルをSDカードから読み込む
bool loadSTL(const char *path) // STLファイルをSDカードから読み込む関数
{
    if (!SD.exists(path))
        return false;

    // パス（/M5-3D_clean.stl）からファイル名を抽出
    String p = String(path);
    int lastSlash = p.lastIndexOf('/');
    currentFileName = p.substring(lastSlash + 1);

    File file = SD.open(path, FILE_READ);

    if (!file)
        return false;
    file.seek(80);
    uint32_t count;
    file.read((uint8_t *)&count, 4);
    model.clear();
    float minX = 1e6, maxX = -1e6, minY = 1e6, maxY = -1e6, minZ = 1e6, maxZ = -1e6;
    for (uint32_t i = 0; i < count; i++)
    {
        Triangle tri;
        uint16_t d;
        file.read((uint8_t *)tri.normal, 12);
        file.read((uint8_t *)tri.v, 36);
        file.read((uint8_t *)&d, 2);
        for (int j = 0; j < 3; j++)
        {
            tri.v[j][0] = -tri.v[j][0]; // 鏡像修正
            minX = std::min(minX, tri.v[j][0]);
            maxX = std::max(maxX, tri.v[j][0]);
            minY = std::min(minY, tri.v[j][1]);
            maxY = std::max(maxY, tri.v[j][1]);
            minZ = std::min(minZ, tri.v[j][2]);
            maxZ = std::max(maxZ, tri.v[j][2]);
        }
        model.push_back(tri);
    }
    file.close();
    offsetX = (minX + maxX) / 2.0f;
    offsetY = (minY + maxY) / 2.0f;
    offsetZ = (minZ + maxZ) / 2.0f;
    return true;
}

void drawUI_Ver2() // UIを一新して、上部に文字情報、下部に操作エリアを配置するバージョン
{
    // ... 文字情報の描画 ...
    canvasUI.fillSprite(TFT_BLACK); // パーツ1の背景を塗りつぶす
    canvasUI.setTextColor(TFT_LIGHTGREY);
    canvasUI.setTextDatum(top_center);
    canvasUI.setFont(&fonts::FreeSansBoldOblique18pt7b);
    canvasUI.drawString("Tab5 STL Viewer v2.5", 280, 40);

    canvasUI.setFont(&fonts::FreeSans12pt7b);
    canvasUI.drawString(currentFileName, 280, 85);
    canvasUI.setCursor(180, 125);
    canvasUI.printf("POLYGONS: %d", (int)model.size());
    canvasUI.drawString("SWIPE: ROTATE / PINCH: ZOOM", 280, 165);

    canvasUI.pushRotateZoom(rotaionXY[currentRotation][0], rotaionXY[currentRotation][1], rotaionXY[currentRotation][4], 1.0, 1.0);

    // ... ボタンやパレットの描画 ...
    canvasUI.fillSprite(TFT_BLACK); // パーツ2の背景を塗りつぶす
    for (auto &p : colorPalette)
    {
        canvasUI.fillCircle(p.x, 120, 30, p.color);
        if (p.color == baseColor)
            canvasUI.drawCircle(p.x, 120, 35, TFT_WHITE);
    }

    canvasUI.fillRoundRect(465, 90, 90, 60, 10, isAutoMode ? TFT_BLUE : TFT_DARKGREY);
    canvasUI.setTextColor(TFT_WHITE);
    canvasUI.drawString("AUTO", 510, 110);

    canvasUI.pushRotateZoom(rotaionXY[currentRotation][2], rotaionXY[currentRotation][3], rotaionXY[currentRotation][4], 1.0, 1.0); // UIエリア全体を回転させて描画する

    // ... QRコードの描画 ...

    // PNGの場合
    canvasUI.fillSprite(TFT_BLACK);                                                                                                 // QRコードの背景を塗りつぶす
    readIMG(0);                                                                                                                     // 画像を読み込む（必要に応じて複数の画像を用意して切り替えることも可能）
    canvasUI.drawPng(gIMGBuf, img_buffer, 20, 5, 140, 140);                                                                         // QRコードを描画する
    canvasUI.drawCenterString("X", 90, 165);                                                                                        // QRコードの下に文字を描く
    readIMG(1);                                                                                                                     // 画像を読み込む（必要に応じて複数の画像を用意して切り替えることも可能）
    canvasUI.drawPng(gIMGBuf, img_buffer, 400, 5, 140, 140);                                                                        // QRコードを描画する
    canvasUI.drawCenterString("GitHub", 470, 165);                                                                                  // QRコードの下に文字を描く
    canvasUI.pushRotateZoom(rotaionQR[currentRotation][0], rotaionQR[currentRotation][1], rotaionQR[currentRotation][2], 1.0, 1.0); // QRコードを回転させて描画する

    // QRコードを直接描画する場合
    /*
    canvasUI.fillSprite(TFT_BLACK);                                                                                                 // QRコードの背景を塗りつぶす
    canvasUI.qrcode("https://x.com/kawacci", 20, 5, 140);                                                                           // QRコードを描画する
    canvasUI.drawCenterString("X", 90, 165);                                                                                        // QRコードの下に文字を描く
    canvasUI.qrcode("https://github.com/kawacci", 400, 5, 140);                                                                     // QRコードを描画する
    canvasUI.drawCenterString("GitHub", 470, 165);                                                                                  // QRコードの下に文字を描く
    canvasUI.pushRotateZoom(rotaionQR[currentRotation][0], rotaionQR[currentRotation][1], rotaionQR[currentRotation][2], 1.0, 1.0); // QRコードを回転させて描画する
    */
}

void IMUupdate() // IMUの更新と自動回転判定
{
    if (M5.Imu.update())
    {
        auto data = M5.Imu.getImuData();
        float ay = data.accel.y;
        float ax = data.accel.x;
        int nextRotation = currentRotation;
        if (ay > 0.5f)
            nextRotation = 0; // 正立
        else if (ay < -0.5f)
            nextRotation = 2; // 逆さま
        else if (ax < -0.5f)
            nextRotation = 3; // 左向き
        else if (ax > 0.5f)
            nextRotation = 1; // 右向き

        if (nextRotation != currentRotation)
        {
            currentRotation = nextRotation;
            M5.Display.fillRect(0, 720, 720, 560, TFT_BLACK); // UIエリアをクリア
            drawUI_Ver2();
        }
    }
}

void touchLCD()
{
    int count = M5.Lcd.getTouchRaw(tp, 5); // タッチポイントの数を取得
    static uint32_t lastCheckTime = 0;
    if (count > 0) // タッチがある場合
    {
        lastTouchTime = millis(); // タッチがあるたびにタイマーをリセット

        // --- UIエリア（下部）のタッチ判定 ---
        if (tp[0].y >= 720)
        {                                                     // タッチ座標UIエリア
            if (currentRotation == 0 || currentRotation == 2) // 回転0度と180度はX軸が水平なので、localXはtp[0].xを基準に、localYはtp[0].yを基準に変換
            {
                localX = tp[0].x;                                           // UIキャンバスの左端オフセット
                localY = tp[0].y - 720 - BtnshiftRange[currentRotation][0]; // UIキャンバスの上端オフセットと回転によるシフトを考慮してローカル座標に変換

                // 1. カラーパレット判定 (半径30)
                // 許容範囲を少し広めに（上下35ピクセルなど）設定します
                if (abs(localY) < 35)
                {
                    for (int i = 0; i < 6; i++) // 算出した6個の座標
                    {
                        int targetX = colorPalette[i].x; // 先ほど計算した 40, 115, 190...（回転によってXとYが入れ替わるため、colorPalette[i].xをtargetXとして扱う）
                        if (abs(localX - abs(targetX + BtnshiftRange[currentRotation][1])) < 35)
                        {
                            baseColor = colorPalette[i].color;
                            drawUI_Ver2(); // 選択枠を更新するために再描画
                            break;
                        }
                    }
                }

                // 2. AUTOボタン判定
                // ボタンの描画位置が (465, 170) なら、同じ localY の範囲で判定可能です
                if (localX >= AutoshiftRange[currentRotation][0] - 5 && localX <= AutoshiftRange[currentRotation][1] + 5 && abs(localY) < 35)
                {
                    if (millis() - lastCheckTime > 300)
                    {                             // 300ms間隔をあける
                        isAutoMode = !isAutoMode; // AUTOモードのトグル
                        drawUI_Ver2();
                        lastCheckTime = millis(); // 最後に判定した時間を更新
                    }
                }
            }
            else // 回転90度と270度はX軸が垂直なので、localXはtp[0].yを基準に、localYはtp[0].xを基準に変換
            {
                localX = tp[0].x - BtnshiftRange[currentRotation][0]; // UIキャンバスの左端オフセットと回転によるシフトを考慮してローカル座標に変換
                localY = tp[0].y - 720;                               // UIキャンバスの上端オフセットを考慮してローカル座標に変換

                // 1. カラーパレット判定 (中心 localY = 200, 半径30)
                // 許容範囲を少し広めに（上下35ピクセルなど）設定します
                if (abs(localX) < 35)
                {
                    for (int i = 0; i < 6; i++) // 算出した6個の座標
                    {
                        int targetY = colorPalette[i].x; // 先ほど計算した 40, 115, 190...（回転によってXとYが入れ替わるため、colorPalette[i].xをtargetYとして扱う）
                        if (abs(localY - abs(targetY + BtnshiftRange[currentRotation][1])) < 35)
                        {
                            baseColor = colorPalette[i].color;
                            drawUI_Ver2(); // 選択枠を更新するために再描画
                            break;
                        }
                    }
                }

                // 2. AUTOボタン判定
                // ボタンの描画位置が (465, 170) なら、同じ localY の範囲で判定可能です
                if (localY >= AutoshiftRange[currentRotation][0] - 5 && localY <= AutoshiftRange[currentRotation][1] + 5 && abs(localX) < 35)
                {
                    if (millis() - lastCheckTime > 300)
                    {                             // 300ms間隔をあける
                        isAutoMode = !isAutoMode; // AUTOモードのトグル
                        drawUI_Ver2();
                        lastCheckTime = millis(); // 最後に判定した時間を更新
                    }
                }
            }

        } // タッチ座標UIエリア終了
        else
        {
            // STLエリア（上部）のタッチ
            if (isAutoMode)
            {
                isAutoMode = false;
                drawUI_Ver2();
            }

            if (count == 1)
            {
                if (prev_touch_count == 1)
                {
                    // 向き 0 に合わせたスワイプ方向の調整
                    // loop関数内のシングルタッチ処理部分
                    float dx = (tp[0].x - last_x) * 0.008f;
                    float dy = (tp[0].y - last_y) * 0.008f;

                    // dy にマイナスを付与して上下回転を反転
                    float rotX[3][3] = {{1, 0, 0}, {0, cos(-dy), -sin(-dy)}, {0, sin(-dy), cos(-dy)}};
                    float rotY[3][3] = {{cos(dx), 0, sin(dx)}, {0, 1, 0}, {-sin(dx), 0, cos(dx)}};
                    float tempRot[3][3];
                    matMultiply(rotX, rotY, tempRot);
                    matMultiply(tempRot, mat, mat);
                }
                last_x = tp[0].x;
                last_y = tp[0].y;
            }
            else if (count >= 2)
            {
                float dx = tp[0].x - tp[1].x, dy = tp[0].y - tp[1].y;
                float dist = sqrt(dx * dx + dy * dy);
                if (prev_touch_count >= 2)
                {
                    modelScale += (dist - prev_pinch_dist) * 15.0f;
                    modelScale = std::max(500.0f, std::min(25000.0f, modelScale));
                }
                prev_pinch_dist = dist;
            }
        }
    }
    else
    {
        if (!isAutoMode && (millis() - lastTouchTime > AUTO_RETURN_MS))
        {
            isAutoMode = true;
            drawUI_Ver2();
        }
    }
    prev_touch_count = count;
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Display.setRotation(0); // 向きを 0 に固定
    M5.Display.fillScreen(TFT_BLACK);

    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
    loadSTL(currentFilePath.c_str());

    canvasUI.setColorDepth(16);      // UI用のスプライトの色深度を16ビットに設定
    canvasUI.createSprite(560, 200); // UI用のスプライトを作成

    drawUI_Ver2();
    canvas.setColorDepth(16);            // STL描画用のスプライトの色深度を16ビットに設定
    canvas.createSprite(TILE_W, TILE_H); // STL描画用のスプライトを作成

    lastTouchTime = millis();
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

        // 2. 輝度(dot)計算を「カメラ視点の固定ベクトル」で行う
        // 画面の正面（カメラ）から光が当たっていることにする場合：
        // 光源を (x:0, y:0, z:1.0) とみなすと、dot = rnz になります。
        // 少し斜め（カメラの肩越し）から照らしたい場合は、以下のようにします。
        float lightX = 0.2f;
        float lightY = 0.2f;
        float lightZ = 0.95f; // 正面を強めに
                              // float dot = rnx * lightX + rny * lightY + rnz * lightZ;
                              // float dot = tri.normal[0] * lx + tri.normal[1] * ly + tri.normal[2] * lz;
        float dot = fabsf(rnz) * 0.8f + fabsf(rnx) * 0.2f;
        // 裏向きの面も少しだけ明るく（環境光 0.15f）
        float intensity = std::max(0.15f, dot);

        uint8_t r = ((baseColor >> 11) & 0x1F) << 3;
        uint8_t g = ((baseColor >> 5) & 0x3F) << 2;
        uint8_t b = (baseColor & 0x1F) << 3;
        uint16_t faceColor = M5.Display.color565(r * intensity, g * intensity, b * intensity);

        DrawTri dt;
        float sumZ = 0;
        for (int i = 0; i < 3; i++)
        {
            float vx = tri.v[i][0] - offsetX, vy = tri.v[i][1] - offsetY, vz = tri.v[i][2] - offsetZ;
            float rx = vx * mat[0][0] + vy * mat[0][1] + vz * mat[0][2];
            float ry = vx * mat[1][0] + vy * mat[1][1] + vz * mat[1][2];
            float rz = vx * mat[2][0] + vy * mat[2][1] + vz * mat[2][2];
            float s = modelScale / (rz + 800.0f);
            dt.px[i] = (int)(rx * s) + 360;
            dt.py[i] = (int)(ry * s) + 360; // STL領域は 0-719 内
            sumZ += rz;
        }
        // 向き0 用の表裏判定修正 ( > 0 に変更)
        // 外積の結果が正のとき（反時計回り）を描画対象から外す、
        // もしくは負のときを描画対象にする、という切り替えです。
        if (((long)(dt.px[1] - dt.px[0]) * (dt.py[2] - dt.py[0]) - (long)(dt.py[1] - dt.py[0]) * (dt.px[2] - dt.px[0])) > 0)
            continue;
        dt.avgZ = sumZ / 3.0f;
        dt.color = faceColor;
        dt.minY = std::min({dt.py[0], dt.py[1], dt.py[2]});
        dt.maxY = std::max({dt.py[0], dt.py[1], dt.py[2]});
        drawList.push_back(dt);
    }

    std::sort(drawList.begin(), drawList.end(), [](const DrawTri &a, const DrawTri &b)
              { return a.avgZ < b.avgZ; });

    for (int i = 0; i < TILE_COUNT; i++)
    {
        int y_start = i * TILE_H;
        canvas.fillSprite(TFT_BLACK);
        for (const auto &dt : drawList)
        {
            if (dt.maxY >= y_start && dt.minY < y_start + TILE_H)
                canvas.fillTriangle(dt.px[0], dt.py[0] - y_start, dt.px[1], dt.py[1] - y_start, dt.px[2], dt.py[2] - y_start, dt.color);
        }
        canvas.pushSprite(0, y_start);
    }
}
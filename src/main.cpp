#include <M5Unified.h>
#include <SPI.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include <math.h>
#include <map>

// MTLの材料名をキーにして、RGB565カラーを保持するマップ
std::map<String, uint16_t> materialMap;
uint16_t currentMeshColor = 0xCCCC; // OBJでマテリアル指定がない場合のデフォルト色

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

// QRコードなどの画像ファイルパス
const char *imagePaths[] = {
    "/x_wb_128.png",
    "/github_wb_128.png",
};
long img_buffer = 16384;   // 画像読み込み用バッファサイズ
int currentImageIndex = 0; // 表示イメージの番号

struct Triangle
{
    float v[3][3];   // 三角形の3頂点
    float normal[3]; // 面法線ベクトル
    uint16_t color;  // 面ごとの色(RGB565)
};

std::vector<Triangle> model;           // 読み込んだ三角形リスト
String currentFilePath = "/M5-3D.obj"; // 読み込むファイルパス
String currentFileName = "";           // 画面に表示するファイル名

float mat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; // モデルの回転行列
float modelScale = 3500.0f;                          // モデル描画スケール
float offsetX, offsetY, offsetZ;                     // モデル中心座標オフセット

bool isAutoMode = true;                // 自動回転モード
uint32_t lastTouchTime = 0;            // 最後にタッチがあった時刻
const uint32_t AUTO_RETURN_MS = 10000; // タッチなしで自動回転に戻る時間

uint16_t baseColor = TFT_WHITE; // STL色変更用
struct ColorOption
{
    uint16_t color;
    int x;
};

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

void updateFileList() // SDカード内のSTL/OBJファイルのリストを更新する関数
{
    fileList.clear();
    if (!SD.exists("/STL_OBJ")) // SDカードのルートに「STL_OBJ」フォルダがある前提
    {
        Serial.println("Folder /STL_OBJ not found!");
        return;
    }
    File root = SD.open("/STL_OBJ/"); // フォルダを開く
    while (File file = root.openNextFile())
    {
        String name = file.name();
        if (name.endsWith(".stl") || name.endsWith(".obj"))
        {
            fileList.push_back(name);
        }
        file.close();
    }
}

// 起動時にモデルリストを表示するランチャー画面
void drawLauncher()
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setTextDatum(top_center);
    M5.Display.setFont(&fonts::FreeSansBoldOblique18pt7b);
    M5.Display.drawString("3D MODEL SELECTOR", 360, 50);

    M5.Display.setFont(&fonts::FreeSans12pt7b);
    for (int i = 0; i < fileList.size(); i++)
    {
        int y = 150 + (i * 100);
        M5.Display.drawRoundRect(40, y, 640, 80, 10, TFT_WHITE); // 選択ボタン枠

        // 拡張子によって色を変えて表示
        if (fileList[i].endsWith(".obj"))
        {
            M5.Display.setTextColor(TFT_ORANGE);
            M5.Display.drawString("[OBJ]", 100, y + 25);
        }
        else
        {
            M5.Display.setTextColor(TFT_GREENYELLOW);
            M5.Display.drawString("[STL]", 100, y + 25);
        }
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setTextDatum(top_left);
        M5.Display.drawString(fileList[i], 180, y + 25);
        M5.Display.setTextDatum(top_center);
    }
}

// タッチでモデルを選択し、読み込み中メッセージを表示する
String selectModel()
{
    drawLauncher();

    while (true)
    {
        M5.update();
        auto count = M5.Lcd.getTouchRaw(tp, 1);
        if (count > 0)
        {
            int tx = tp[0].x;
            int ty = tp[0].y;

            for (int i = 0; i < fileList.size(); i++)
            {
                int y = 150 + (i * 100);
                if (tx > 40 && tx < 680 && ty > y && ty < y + 80)
                {
                    M5.Display.fillScreen(TFT_BLACK);
                    M5.Display.setFont(&fonts::FreeSansBoldOblique18pt7b);
                    M5.Display.setTextColor(TFT_YELLOW);
                    M5.Display.drawCenterString("LOADING MODEL...", 360, 600);
                    M5.Display.setFont(&fonts::FreeSans12pt7b);
                    M5.Display.drawCenterString(fileList[i], 360, 660);

                    return fileList[i];
                }
            }
        }
        delay(10);
    }
}

// 3x3行列の乗算
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

// 画像ファイルを読み込んでPNGバッファを更新する
uint8_t gIMGBuf[16384]; // 画像読み込み用バッファ
void readIMG(int index)
{
    currentImageIndex = index;                      // 表示する画像インデックスを更新
    auto imagePath = imagePaths[currentImageIndex]; // 画像ファイルのパスを取得
    File file = SD.open(imagePath, FILE_READ);      // 画像ファイルを開く
    file.read(gIMGBuf, img_buffer);                 // バッファに読み込む
    file.close();
}

// バイナリSTLファイルを読み込む
bool loadSTL(const char *path)
{
    if (!SD.exists(path))
        return false;

    String p = String(path);
    int lastSlash = p.lastIndexOf('/');
    currentFileName = p.substring(lastSlash + 1);

    File file = SD.open(path, FILE_READ);
    if (!file)
        return false;

    // 80バイトのSTLヘッダをスキップ
    file.seek(80);
    uint32_t count;
    file.read((uint8_t *)&count, 4);

    model.clear();
    float minX = 1e6, maxX = -1e6, minY = 1e6, maxY = -1e6, minZ = 1e6, maxZ = -1e6;

    for (uint32_t i = 0; i < count; i++)
    {
        Triangle tri;
        uint16_t attributeByteCount; // 使用しない属性値

        // 法線(12byte)、頂点(36byte)、属性(2byte)を読み込む
        file.read((uint8_t *)tri.normal, 12);
        file.read((uint8_t *)tri.v, 36);
        file.read((uint8_t *)&attributeByteCount, 2);

        // STLでは面ごとに色情報がないので、選択中の色を設定
        tri.color = baseColor;

        for (int j = 0; j < 3; j++)
        {
            tri.v[j][0] = -tri.v[j][0]; // 左右反転補正
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

    // 中心座標と自動スケールの計算（OBJ側と合わせる）
    offsetX = (minX + maxX) / 2.0f;
    offsetY = (minY + maxY) / 2.0f;
    offsetZ = (minZ + maxZ) / 2.0f;

    float diffX = maxX - minX;
    float diffY = maxY - minY;
    float diffZ = maxZ - minZ;
    float maxDiff = std::max({diffX, diffY, diffZ});

    if (maxDiff > 0)
    {
        modelScale = (400.0f / maxDiff) * 800.0f;
        baseScale = modelScale;
        Serial.printf("STL Auto Scale: %f\n", modelScale);
    }

    return (model.size() > 0);
}

bool loadOBJ(const char *path)
{
    Serial.printf("Loading OBJ: %s\n", path);
    File file = SD.open(path, FILE_READ);
    if (!file)
        return false;

    currentFileName = String(path).substring(String(path).lastIndexOf('/') + 1);
    model.clear();

    // OBJの頂点を一時保管するリスト
    std::vector<float *> temp_vertices;
    float minX = 1e6, maxX = -1e6, minY = 1e6, maxY = -1e6, minZ = 1e6, maxZ = -1e6;

    // マテリアルが指定されていない場合のデフォルト色
    uint16_t currentMeshColor = 0xCCCC;

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() < 3)
            continue;

        if (line.startsWith("usemtl "))
        {
            String matName = line.substring(7);
            matName.trim();
            if (materialMap.count(matName))
            {
                currentMeshColor = materialMap[matName];
            }
        }
        else if (line.startsWith("v "))
        {
            float *v = new float[3];
            if (sscanf(line.c_str(), "v %f %f %f", &v[0], &v[1], &v[2]) >= 3)
            {
                v[0] = -v[0]; // 左右反転補正
                minX = std::min(minX, v[0]);
                maxX = std::max(maxX, v[0]);
                minY = std::min(minY, v[1]);
                maxY = std::max(maxY, v[1]);
                minZ = std::min(minZ, v[2]);
                maxZ = std::max(maxZ, v[2]);
                temp_vertices.push_back(v);
            }
            else
            {
                delete[] v;
            }
        }
        else if (line.startsWith("f "))
        {
            int vIdx[3] = {0, 0, 0};
            int count = 0;
            char *ptr = (char *)line.c_str() + 2;

            while (*ptr && count < 3)
            {
                while (*ptr == ' ')
                    ptr++;
                if (*ptr)
                {
                    vIdx[count++] = atoi(ptr);
                    while (*ptr && *ptr != ' ')
                        ptr++;
                }
            }

            if (count >= 3)
            {
                Triangle tri;
                tri.color = currentMeshColor; // 現在のマテリアル色を三角形に設定

                for (int i = 0; i < 3; i++)
                {
                    int idx = vIdx[i];
                    if (idx < 0)
                        idx = temp_vertices.size() + idx + 1;
                    if (idx > 0 && idx <= (int)temp_vertices.size())
                    {
                        float *srcV = temp_vertices[idx - 1];
                        tri.v[i][0] = srcV[0];
                        tri.v[i][1] = srcV[1];
                        tri.v[i][2] = srcV[2];
                    }
                }

                float ax = tri.v[1][0] - tri.v[0][0], ay = tri.v[1][1] - tri.v[0][1], az = tri.v[1][2] - tri.v[0][2];
                float bx = tri.v[2][0] - tri.v[0][0], by = tri.v[2][1] - tri.v[0][1], bz = tri.v[2][2] - tri.v[0][2];
                float nx = ay * bz - az * by, ny = az * bx - ax * bz, nz = ax * by - ay * bx;
                float len = sqrt(nx * nx + ny * ny + nz * nz);
                if (len > 0)
                {
                    tri.normal[0] = nx / len;
                    tri.normal[1] = ny / len;
                    tri.normal[2] = nz / len;
                }
                model.push_back(tri);
            }
        }
    }
    file.close();

    for (auto v : temp_vertices)
        delete[] v;

    offsetX = (minX + maxX) / 2.0f;
    offsetY = (minY + maxY) / 2.0f;
    offsetZ = (minZ + maxZ) / 2.0f;

    float diffX = maxX - minX;
    float diffY = maxY - minY;
    float diffZ = maxZ - minZ;
    float maxDiff = std::max({diffX, diffY, diffZ});

    if (maxDiff > 0)
    {
        modelScale = (400.0f / maxDiff) * 800.0f;
        baseScale = modelScale;
        Serial.printf("Auto Scale Applied: %f (MaxDim: %f)\n", modelScale, maxDiff);
    }

    if (model.size() > 0)
    {
        Serial.printf("Model Loaded: %d triangles.\n", (int)model.size());
    }

    return (model.size() > 0);
}

// MTLファイルを読み込み、materialMapにRGB565カラーを登録する
void loadMTL(const char *path)
{
    File file = SD.open(path, FILE_READ);
    if (!file)
    {
        Serial.printf("MTL not found: %s\n", path);
        return;
    }

    String currentMatName = "";
    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();

        if (line.startsWith("newmtl "))
        {
            currentMatName = line.substring(7);
            currentMatName.trim();
        }
        else if (line.startsWith("Kd "))
        {
            float r, g, b;
            if (sscanf(line.c_str(), "Kd %f %f %f", &r, &g, &b) >= 3)
            {
                uint16_t color = M5.Display.color565(r * 255, g * 255, b * 255);
                materialMap[currentMatName] = color;
            }
        }
    }
    file.close();
    Serial.printf("Loaded %d materials from MTL.\n", (int)materialMap.size());
}

// UIを描画する関数
// 上段にモデル情報と操作説明を表示し、下段に色選択・AUTOボタン・QR画像を配置する
void drawUI_Ver2()
{
    canvasUI.fillSprite(TFT_BLACK); // UI全体の背景をクリア
    canvasUI.setTextColor(TFT_LIGHTGREY);
    canvasUI.setTextDatum(top_center);
    canvasUI.setFont(&fonts::FreeSansBoldOblique18pt7b);
    canvasUI.drawString("Tab5 STL/OBJ Viewer v3.0", 280, 40);

    canvasUI.setFont(&fonts::FreeSans12pt7b);
    canvasUI.drawString(currentFileName, 280, 85);                            // 読み込み中のモデル名
    canvasUI.setCursor(180, 125);
    canvasUI.printf("POLYGONS: %d", (int)model.size());                      // ポリゴン数
    canvasUI.drawString("SWIPE: ROTATE / PINCH: ZOOM", 280, 165);            // 操作ヒント

    canvasUI.pushRotateZoom(rotaionXY[currentRotation][0], rotaionXY[currentRotation][1], rotaionXY[currentRotation][4], 1.0, 1.0);

    // 操作エリアの背景をクリアしてから描画
    canvasUI.fillSprite(TFT_BLACK);
    if (currentFileName.endsWith(".stl") && model.size() > 0)
    {
        for (auto &p : colorPalette)
        {
            canvasUI.fillCircle(p.x, 120, 30, p.color);       // 色パレットを描画
            if (p.color == baseColor)
                canvasUI.drawCircle(p.x, 120, 35, TFT_WHITE); // 選択中の色に枠を表示
        }
    }

    canvasUI.fillRoundRect(465, 90, 90, 60, 10, isAutoMode ? TFT_BLUE : TFT_DARKGREY); // AUTOボタン背景
    canvasUI.setTextColor(TFT_WHITE);
    canvasUI.drawString("AUTO", 510, 110);                                             // AUTOボタン文字

    canvasUI.pushRotateZoom(rotaionXY[currentRotation][2], rotaionXY[currentRotation][3], rotaionXY[currentRotation][4], 1.0, 1.0);

    // QRコード画像を順番に読み込んで描画
    canvasUI.fillSprite(TFT_BLACK);
    readIMG(0);
    canvasUI.drawPng(gIMGBuf, img_buffer, 20, 5, 140, 140);
    canvasUI.drawCenterString("X", 90, 165);

    readIMG(1);
    canvasUI.drawPng(gIMGBuf, img_buffer, 400, 5, 140, 140);
    canvasUI.drawCenterString("GitHub", 470, 165);
    canvasUI.pushRotateZoom(rotaionQR[currentRotation][0], rotaionQR[currentRotation][1], rotaionQR[currentRotation][2], 1.0, 1.0);

    // 直接QRコードを生成して描画する場合の例
    /*
    canvasUI.fillSprite(TFT_BLACK);
    canvasUI.qrcode("https://x.com/kawacci", 20, 5, 140);
    canvasUI.drawCenterString("X", 90, 165);
    canvasUI.qrcode("https://github.com/kawacci", 400, 5, 140);
    canvasUI.drawCenterString("GitHub", 470, 165);
    canvasUI.pushRotateZoom(rotaionQR[currentRotation][0], rotaionQR[currentRotation][1], rotaionQR[currentRotation][2], 1.0, 1.0);
    */
}

void IMUupdate()
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
            M5.Display.fillRect(0, 720, 720, 560, TFT_BLACK); // UI部分を再描画
            drawUI_Ver2();
        }
    }
}

// カラーパレットから選択された色をすべての三角形に適用
void colorchange(int i)
{
    baseColor = colorPalette[i].color;
    for (auto &tri : model)
    {
        tri.color = baseColor;
    }
    drawUI_Ver2(); // 選択色とAUTOボタン状態を反映
}

// タッチ操作を処理する関数
void touchLCD()
{
    int count = M5.Lcd.getTouchRaw(tp, 5);
    static uint32_t lastCheckTime = 0;

    if (count > 0)
    {
        lastTouchTime = millis(); // タッチイベントが発生したのでタイマーをリセット

        // UIエリア（画面下部）へのタッチ判定
        if (tp[0].y >= 720)
        {
            if (currentRotation == 0 || currentRotation == 2)
            {
                localX = tp[0].x;
                localY = tp[0].y - 720 - BtnshiftRange[currentRotation][0];

                // カラーパレットが押されたか判定
                if (currentFileName.endsWith(".stl") && model.size() > 0)
                {
                    if (abs(localY) < 35)
                    {
                        for (int i = 0; i < 6; i++)
                        {
                            int targetX = colorPalette[i].x;
                            if (abs(localX - abs(targetX + BtnshiftRange[currentRotation][1])) < 35)
                            {
                                colorchange(i);
                                break;
                            }
                        }
                    }
                }

                // AUTOボタンの押下判定
                if (localX >= AutoshiftRange[currentRotation][0] - 5 && localX <= AutoshiftRange[currentRotation][1] + 5 && abs(localY) < 35)
                {
                    if (millis() - lastCheckTime > 300)
                    {
                        isAutoMode = !isAutoMode;
                        drawUI_Ver2();
                        lastCheckTime = millis();
                    }
                }
            }
            else
            {
                localX = tp[0].x - BtnshiftRange[currentRotation][0];
                localY = tp[0].y - 720;

                if (currentFileName.endsWith(".stl") && model.size() > 0)
                {
                    if (abs(localX) < 35)
                    {
                        for (int i = 0; i < 6; i++)
                        {
                            int targetY = colorPalette[i].x;
                            if (abs(localY - abs(targetY + BtnshiftRange[currentRotation][1])) < 35)
                            {
                                colorchange(i);
                                break;
                            }
                        }
                    }
                }

                if (localY >= AutoshiftRange[currentRotation][0] - 5 && localY <= AutoshiftRange[currentRotation][1] + 5 && abs(localX) < 35)
                {
                    if (millis() - lastCheckTime > 300)
                    {
                        isAutoMode = !isAutoMode;
                        drawUI_Ver2();
                        lastCheckTime = millis();
                    }
                }
            }
        }
        else
        {
            // モデル描画エリアへのタッチは回転/ズーム操作として扱う
            if (isAutoMode)
            {
                isAutoMode = false;
                drawUI_Ver2();
            }

            if (count == 1)
            {
                if (prev_touch_count == 1)
                {
                    float dx = (tp[0].x - last_x) * 0.008f;
                    float dy = (tp[0].y - last_y) * 0.008f;

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
                    float delta = (dist - prev_pinch_dist) * (modelScale / 200.0f);
                    modelScale += delta;
                    float minS = baseScale * 0.2f;
                    float maxS = baseScale * 5.0f;
                    modelScale = std::max(minS, std::min(maxS, modelScale));
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

bool loadModel(const char *path)
{
    String p = String(path);
    String p_lower = p;
    p_lower.toLowerCase();

    if (p_lower.endsWith(".stl"))
    {
        return loadSTL(path);
    }
    else if (p_lower.endsWith(".obj"))
    {
        // 引数 path から .mtl のパスを生成する
        String mtlPath = p;
        mtlPath.replace(".obj", ".mtl");
        mtlPath.replace(".OBJ", ".MTL"); // 大文字にも対応しておくと安心です

        loadMTL(mtlPath.c_str());
        return loadOBJ(path);
    }
    return false;
}

void setup()
{
    // シリアル通信の開始（M5.beginより先に呼ぶのが確実です）
    Serial.begin(115200);
    // USB CDCがPCに認識されるまで最大2秒待機
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
    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000))
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
    updateFileList();

    // 2. 起動時にランチャーでファイルを選択させる
    String selected = selectModel();
    currentFilePath = "/STL_OBJ/" + selected; // ここでパスが確定する

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
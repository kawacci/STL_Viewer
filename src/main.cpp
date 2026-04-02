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

// 共通設定
#define TILE_W 720
#define TILE_H 80
#define TILE_COUNT 9

LGFX_Sprite canvas(&M5.Display);

struct Triangle
{
    float v[3][3];
    float normal[3];
};
std::vector<Triangle> model;
String currentFilePath = "/M5-3D.stl"; // 読み込むファイルパス
String currentFileName = "";           // 表示用の名前（自動で入る）

float mat[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
float modelScale = 3500.0f;
float offsetX, offsetY, offsetZ;

bool isAutoMode = true;
uint32_t lastTouchTime = 0;
const uint32_t AUTO_RETURN_MS = 5000;

uint16_t baseColor = TFT_WHITE;
struct ColorOption
{
    uint16_t color;
    int x;
};
ColorOption colorPalette[] = {{TFT_WHITE, 100}, {TFT_CYAN, 220}, {TFT_GREENYELLOW, 340}, {TFT_ORANGE, 460}};

m5::touch_point_t tp[5];
int prev_touch_count = 0;
float prev_pinch_dist = 0;
int last_x, last_y;

const float lightDir[3] = {0.577f, 0.577f, 0.577f};

// 現在の画面向きを保持
int currentRotation = 0;

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

bool loadSTL(const char *path)
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

void drawUI()
{
    int ui_y = 720;
    M5.Display.fillRect(0, ui_y, 720, 560, M5.Display.color565(20, 20, 22));
    M5.Display.setTextColor(TFT_LIGHTGREY);
    M5.Display.setTextDatum(top_center);
    M5.Display.setFont(&fonts::FreeSansBoldOblique18pt7b);
    M5.Display.drawString("Tab5 STL Viewer", 360, ui_y + 40);

    // --- 追加箇所：ファイル名の表示 ---
    M5.Display.setFont(&fonts::FreeSans12pt7b);
    M5.Display.setTextColor(TFT_LIGHTGREY); // 色を変えても可
    M5.Display.drawString(currentFileName, 360, ui_y + 85);
    // ------------------------------

    M5.Display.setTextColor(TFT_LIGHTGREY);      // 色を戻す
    M5.Display.setCursor(360 - 100, ui_y + 125); // Y座標を少し下げて調整
    M5.Display.printf("POLYGONS: %d", (int)model.size());
    M5.Display.drawString("SWIPE: ROTATE / PINCH: ZOOM", 360, ui_y + 165); // Y座標を調整
    for (auto &p : colorPalette)
    {
        M5.Display.fillCircle(p.x, ui_y + 480, 30, p.color);
        if (p.color == baseColor)
            M5.Display.drawCircle(p.x, ui_y + 480, 35, TFT_WHITE);
    }

    M5.Display.fillRoundRect(580, ui_y + 450, 100, 60, 10, isAutoMode ? TFT_BLUE : TFT_DARKGREY);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.drawString("AUTO", 630, ui_y + 465);
}

void setup()
{
    auto cfg = M5.config();
    M5.begin(cfg);
    M5.Imu.begin();
    M5.Display.setRotation(0);

    SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
    loadSTL(currentFilePath.c_str());
    drawUI();
    canvas.setColorDepth(16);
    canvas.createSprite(TILE_W, TILE_H);
    lastTouchTime = millis();
}

void loop()
{
    M5.update();

    // --- 1. IMUによる自動回転判定 ---
    if (M5.Imu.update())
    {
        auto data = M5.Imu.getImuData();
        float ay = data.accel.y;
        int nextRotation = currentRotation;
        if (ay > 0.5f)
            nextRotation = 0; // 正立
        else if (ay < -0.5f)
            nextRotation = 2; // 逆さま
        if (nextRotation != currentRotation)
        {
            currentRotation = nextRotation;
            M5.Display.setRotation(currentRotation);
            M5.Display.fillScreen(TFT_BLACK);
            drawUI();
        }
    }

    // --- 2. タッチ入力と座標正規化 ---
    int count = M5.Lcd.getTouchRaw(tp, 5);
    if (count > 0)
    {
        lastTouchTime = millis();
        int tx = tp[0].x;
        int ty = tp[0].y;
        if (currentRotation == 2)
        {
            tx = 720 - tx;
            ty = 1280 - ty;
        }

        if (ty > 720)
        { // UI判定
            if (tx > 580 && ty > 720 + 450)
            {
                isAutoMode = true;
                drawUI();
            }
            for (auto &p : colorPalette)
            {
                if (abs(tx - p.x) < 40 && abs(ty - (720 + 480)) < 40)
                {
                    baseColor = p.color;
                    drawUI();
                }
            }
        }
        else
        { // モデル操作
            if (isAutoMode)
            {
                isAutoMode = false;
                drawUI();
            }
            if (count == 1)
            {
                if (prev_touch_count == 1)
                {
                    // 【回転方向の最終調整】
                    // dx, dy の両方にマイナスをつけると、操作と回転を反転させます
                    float dx = (tx - last_x) * 0.008f;
                    float dy = -(ty - last_y) * 0.008f;

                    float rotX[3][3] = {{1, 0, 0}, {0, cos(dy), -sin(dy)}, {0, sin(dy), cos(dy)}};
                    float rotY[3][3] = {{cos(dx), 0, sin(dx)}, {0, 1, 0}, {-sin(dx), 0, cos(dx)}};
                    float tempRot[3][3];
                    matMultiply(rotX, rotY, tempRot);
                    matMultiply(tempRot, mat, mat);
                }
                last_x = tx;
                last_y = ty;
            }
            else if (count >= 2)
            {
                float pdx = tp[0].x - tp[1].x, pdy = tp[0].y - tp[1].y;
                float dist = sqrt(pdx * pdx + pdy * pdy);
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
            drawUI();
        }
    }
    prev_touch_count = count;

    if (isAutoMode)
    {
        float adx = 0.008f, ady = 0.005f;
        float rotX[3][3] = {{1, 0, 0}, {0, cos(ady), -sin(ady)}, {0, sin(ady), cos(ady)}};
        float rotY[3][3] = {{cos(adx), 0, sin(adx)}, {0, 1, 0}, {-sin(adx), 0, cos(adx)}};
        float tempRot[3][3];
        matMultiply(rotX, rotY, tempRot);
        matMultiply(tempRot, mat, mat);
    }

    // --- 3Dレンダリング ---
    struct DrawTri
    {
        int px[3], py[3], minY, maxY;
        float avgZ;
        uint16_t color;
    };
    std::vector<DrawTri> drawList;
    drawList.reserve(model.size());

    for (const auto &tri : model)
    {
        float rnx = tri.normal[0] * mat[0][0] + tri.normal[1] * mat[0][1] + tri.normal[2] * mat[0][2];
        float rny = tri.normal[0] * mat[1][0] + tri.normal[1] * mat[1][1] + tri.normal[2] * mat[1][2];
        float rnz = tri.normal[0] * mat[2][0] + tri.normal[1] * mat[2][1] + tri.normal[2] * mat[2][2];
        float dot = rnx * lightDir[0] + rny * lightDir[1] + rnz * lightDir[2];
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
            dt.py[i] = (int)(ry * s) + 360;
            sumZ += rz;
        }

        long cp = (long)(dt.px[1] - dt.px[0]) * (dt.py[2] - dt.py[0]) - (long)(dt.py[1] - dt.py[0]) * (dt.px[2] - dt.px[0]);

        // 【重要：カリング判定を固定】
        // 透けている状態を直すため、判定を cp >= 0 に変更しました
        if (cp >= 0)
            continue; // 「>=」を「<=」に変えると、描画される面が完全に入れ替わります

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
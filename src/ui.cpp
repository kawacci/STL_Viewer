#include "ui.h"
#include "model.h"

// 画像読み込み用のバッファ
uint8_t gIMGBuf[16384];

// QRコードなどの画像を読み込む
void readIMG(int index)
{
    currentImageIndex = index;                      // 表示する画像インデックスを更新
    auto imagePath = imagePaths[currentImageIndex]; // 画像ファイルのパスを取得
    File file = SD.open(imagePath, FILE_READ);      // 画像ファイルを開く
    if (!file) {
        Serial.printf("Failed to open image: %s\n", imagePath);
        return;
    }
    file.read(gIMGBuf, img_buffer);                 // バッファに読み込む
    file.close();
}

// 起動時にモデルリストを表示するランチャー画面
void drawLauncher()
{
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setTextDatum(top_center);
    M5.Display.setFont(&fonts::FreeSansBoldOblique18pt7b);
    M5.Display.drawString("3D MODEL SELECTOR", 360, 50);

    if (fileList.size() == 0) {
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setFont(&fonts::FreeSans12pt7b);
        M5.Display.drawCenterString("NO FILES FOUND", 360, 300);
        M5.Display.drawCenterString("Insert SD card with", 360, 360);
        M5.Display.drawCenterString("STL or OBJ files", 360, 400);
        return;
    }

    M5.Display.setFont(&fonts::FreeSans12pt7b);
    for (int i = 0; i < fileList.size(); i++)
    {
        int y = 150 + (i * 100);
        M5.Display.drawRoundRect(40, y, 640, 80, 10, TFT_WHITE); // 選択ボタン枠

        // ファイル名を抽出（フルパスから）
        String fullPath = fileList[i];
        int lastSlash = fullPath.lastIndexOf('/');
        String fileName = (lastSlash >= 0) ? fullPath.substring(lastSlash + 1) : fullPath;

        // 拡張子によって色を変えて表示
        if (fileName.endsWith(".obj") || fileName.endsWith(".OBJ"))
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
        M5.Display.drawString(fileName, 180, y + 25);
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
                    String fullPath = fileList[i];
                    int lastSlash = fullPath.lastIndexOf('/');
                    String fileName = (lastSlash >= 0) ? fullPath.substring(lastSlash + 1) : fullPath;

                    M5.Display.fillScreen(TFT_BLACK);
                    M5.Display.setFont(&fonts::FreeSansBoldOblique18pt7b);
                    M5.Display.setTextColor(TFT_YELLOW);
                    M5.Display.drawCenterString("LOADING MODEL...", 360, 600);
                    M5.Display.setFont(&fonts::FreeSans12pt7b);
                    M5.Display.drawCenterString(fileName, 360, 660);

                    return fileList[i]; // フルパスを返す
                }
            }
        }
        delay(10);
    }
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
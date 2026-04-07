#include "model.h"
#include <M5Unified.h>
#include <algorithm>

// SDカード内のSTL/OBJファイル一覧を更新
// ファイルのフルパスを保存する
void updateFileList()
{
    fileList.clear();
    
    // /STL_OBJを最優先で試す（以前のコード）
    const char *searchPaths[] = {"/STL_OBJ", "/STL", "/", "/OBJ", "/models"};
    bool found = false;
    
    for (const char *searchPath : searchPaths) {
        if (!SD.exists(searchPath)) {
            Serial.printf("Path %s does not exist\n", searchPath);
            continue;
        }
        
        Serial.printf("Searching in: %s\n", searchPath);
        File root = SD.open(searchPath);
        if (!root) {
            Serial.printf("Cannot open %s\n", searchPath);
            continue;
        }
        
        int count = 0;
        while (true) {
            File file = root.openNextFile();
            if (!file) break;
            
            String name = file.name();
            String lowerName = name;
            lowerName.toLowerCase();
            
            if (lowerName.endsWith(".stl") || lowerName.endsWith(".obj")) {
                // フルパスを作成
                String fullPath = searchPath;
                if (!fullPath.endsWith("/")) fullPath += "/";
                fullPath += name;
                fileList.push_back(fullPath);
                Serial.printf("Found: %s\n", fullPath.c_str());
                count++;
            }
            file.close();
        }
        root.close();
        
        if (count > 0) {
            Serial.printf("Total found in %s: %d files\n", searchPath, count);
            found = true;
            break;
        }
    }
    
    if (!found) {
        Serial.println("No STL/OBJ files found on SD card");
    }
    Serial.printf("updateFileList complete. Total: %d\n", (int)fileList.size());
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
            minX = std::min<float>(minX, tri.v[j][0]);
            maxX = std::max<float>(maxX, tri.v[j][0]);
            minY = std::min<float>(minY, tri.v[j][1]);
            maxY = std::max<float>(maxY, tri.v[j][1]);
            minZ = std::min<float>(minZ, tri.v[j][2]);
            maxZ = std::max<float>(maxZ, tri.v[j][2]);
        }
        model.push_back(tri);
    }
    file.close();

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
                minX = std::min<float>(minX, v[0]);
                maxX = std::max<float>(maxX, v[0]);
                minY = std::min<float>(minY, v[1]);
                maxY = std::max<float>(maxY, v[1]);
                minZ = std::min<float>(minZ, v[2]);
                maxZ = std::max<float>(maxZ, v[2]);
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
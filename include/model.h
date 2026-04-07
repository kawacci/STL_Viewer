#ifndef MODEL_H
#define MODEL_H

#include <vector>
#include <map>
#include <SD.h>
#include <M5Unified.h>

// Triangle構造体の定義
struct Triangle
{
    float v[3][3];   // 三角形の3頂点
    float normal[3]; // 面法線ベクトル
    uint16_t color;  // 面ごとの色(RGB565)
};

// モデル関連関数の宣言
void updateFileList();                                         // SDカードからSTL/OBJファイルのリストを更新する関数
bool loadSTL(const char *path);                                // バイナリSTLファイルを読み込む
bool loadOBJ(const char *path);                                // OBJファイルを読み込む
void loadMTL(const char *path);                                // MTLファイルを読み込む
bool loadModel(const char *path);                              // ファイルパスからSTL/OBJを自動判別して読み込む
void matMultiply(float A[3][3], float B[3][3], float C[3][3]); // 3x3行列の乗算
// 外部参照するグローバル変数
extern std::map<String, uint16_t> materialMap;
extern uint16_t currentMeshColor;
extern std::vector<Triangle> model;
extern String currentFileName;
extern float offsetX, offsetY, offsetZ;
extern float modelScale;
extern float baseScale;
extern uint16_t baseColor;
extern std::vector<String> fileList;
extern const char *imagePaths[2];
extern long img_buffer;
extern int currentImageIndex;

#endif
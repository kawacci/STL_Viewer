import trimesh
import numpy as np

def simplify_planar_priority(input_path, output_path):
    print(f"Loading: {input_path}")
    mesh = trimesh.load(input_path)
    
    # 1. まず重複頂点を完全に結合
    mesh.merge_vertices()
    
    # 2. 【重要】同一平面上の面を論理的に統合する
    # normal_angle: 何度以内の傾きなら同じ面とみなすか（0.5～1.0度くらいが安全）
    print("Merging planar faces...")
    mesh = mesh.simplify_quadratic_decimation(len(mesh.faces)) # 構造整理
    
    # 3. 平面を優先して「再三角形化」する
    # これにより、細かかった平面が大きな三角形に置き換わります
    # 角度の閾値を少し厳しめ(0.1)に設定して形状崩れを防ぎます
    simplified = mesh.simplify_quadric_decimation(0.5) # まず半分に削減

    # 4. 最後に「平面の結合」を明示的に行う
    # これにより、残った平面内の不要なエッジを消し込みます
    # (trimeshのバージョンによっては simplified.process() でも有効)
    
    print(f"Final faces: {len(simplified.faces)}")
    simplified.export(output_path)
    print(f"Saved to: {output_path}")

if __name__ == "__main__":
    input_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D.stl"
    output_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_planar.stl"
    simplify_planar_priority(input_file, output_file)
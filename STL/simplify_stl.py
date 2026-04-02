import trimesh
import numpy as np

def simplify_stl(input_path, output_path, target_faces=2000):
    # 1. STLファイルの読み込み
    print(f"Loading: {input_path}")
    mesh = trimesh.load(input_path)
    
    original_count = len(mesh.faces)
    print(f"Original faces: {original_count}")

    # 2. 重複頂点の結合（CADデータにはよくある隙間を埋める）
    mesh.merge_vertices()
    
    # 3. 平面統合の事前処理（同一平面上の面を論理的にまとめる）
    # 角度の閾値（deg）を指定して、ほぼ同じ向きの面を結合候補にする
    # 0.5度〜1.0度くらいが形状を壊さず綺麗にいけます
    print("Decimating mesh (Planar preference)...")
    
    # simplify_quadratic_decimation は形状を維持しながら面数を減らすアルゴリズムです
    # target_faces に向かって、平面部分から優先的に削削してくれます
    simplified = mesh.simplify_quadric_decimation(target_faces)

    # 4. 結果の表示
    final_count = len(simplified.faces)
    print(f"Final faces: {final_count}")
    print(f"Reduction rate: {(1.0 - final_count/original_count)*100:.1f}%")

    # 5. 保存（M5で読み込めるバイナリ形式で出力）
    simplified.export(output_path)
    print(f"Saved to: {output_path}")

if __name__ == "__main__":
    input_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_clean.stl"
    output_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_clean2.stl"

    # target_faces の代わりに、削減後の割合（0.0 ～ 1.0）を指定します
    # 2000 / 5986 ≒ 0.33
    simplify_stl(input_file, output_file, target_faces=0.33)
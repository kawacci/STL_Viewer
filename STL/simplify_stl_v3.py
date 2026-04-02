import trimesh
import numpy as np

def simplify_planar_priority(input_path, output_path):
    print(f"Loading: {input_path}")
    mesh = trimesh.load(input_path)
    
    # 1. 重複頂点の結合（CADデータ特有の細かい隙間を埋める）
    mesh.merge_vertices()
    
    # 2. 形状を維持しつつ、微細なノイズ面だけを統合
    # ここでの 0.9 は「10%だけ減らす」という意味です。
    # これにより、形状を壊さずに平面内の余計な分割を整理します。
    print("Decimating mesh (High quality mode)...")
    simplified = mesh.simplify_quadric_decimation(0.9) 

    # 3. 保存
    print(f"Original faces: {len(mesh.faces)}")
    print(f"Final faces: {len(simplified.faces)}")
    simplified.export(output_path)
    print(f"Saved to: {output_path}")

if __name__ == "__main__":
    input_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D.stl"
    output_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_v3.stl"
    simplify_planar_priority(input_file, output_file)
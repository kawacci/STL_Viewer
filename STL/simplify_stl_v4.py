import trimesh
import numpy as np

def simplify_planar_strict(input_path, output_path):
    print(f"Loading: {input_path}")
    mesh = trimesh.load(input_path)
    
    # 1. まず重複頂点を完全に結合
    mesh.merge_vertices()
    
    # 2. 【重要】同一平面上の面を論理的に統合する
    # normal_angle: 何度以内の傾きなら同じ面とみなすか
    # (0.1度～0.2度くらいが形状を壊さず綺麗にいけます)
    print("Merging planar faces...")
    simplified = mesh.simplify_quadric_decimation(0.4) # 形状維持を最優先(1%だけ減らす)

    # 3. 最後に「平面の結合」を明示的に行う
    # これにより、残った平面内の不要なエッジを消し込みます
    simplified = simplified.process()
    
    print(f"Final faces: {len(simplified.faces)}")
    simplified.export(output_path)
    print(f"Saved to: {output_path}")

if __name__ == "__main__":
    input_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_base.stl"
    output_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_v4.stl"
    simplify_planar_strict(input_file, output_file)
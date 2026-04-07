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

def simplify_obj_file(input_obj, output_obj, target_ratio=0.65):
    """OBJファイルを面数削減して簡略化"""
    print(f"Loading OBJ: {input_obj}")
    scene = trimesh.load(input_obj)
    
    # SceneからMeshを抽出
    if isinstance(scene, trimesh.Scene):
        # 複数のメッシュがある場合は結合
        mesh = trimesh.util.concatenate([geom for geom in scene.geometry.values()])
    else:
        mesh = scene
    
    print(f"Original faces: {len(mesh.faces)}")
    
    # 重複頂点を統合
    mesh.merge_vertices()
    print(f"After merge vertices: {len(mesh.faces)}")
    
    # 面数削減 (Quadric Mesh Simplification)
    # 第1引数が削減比率 (0.65 = 65%に削減)
    print(f"Simplifying to {int(100*target_ratio)}% of original...")
    simplified = mesh.simplify_quadric_decimation(target_ratio)
    print(f"Simplified faces: {len(simplified.faces)}")
    
    # エクスポート
    simplified.export(output_obj)
    print(f"Saved to: {output_obj}")
    print(f"Reduction: {len(mesh.faces)} -> {len(simplified.faces)} ({100*len(simplified.faces)/len(mesh.faces):.1f}%)")

if __name__ == "__main__":
    input_stl_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_base.stl"
    output_stl_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_v4.stl"
    output_obj_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D_simplified.obj"
    input_obj_file = r"C:\Users\user\Documents\PlatformIO\Projects\STL_Viewer\STL\M5-3D.obj"
    
    # STLを簡略化
    print("=" * 60)
    print("Processing STL file...")
    print("=" * 60)
    simplify_planar_strict(input_stl_file, output_stl_file)
    
    # OBJを簡略化 (65%に削減してSTLと同程度の面数にする)
    print("\n" + "=" * 60)
    print("Processing OBJ file...")
    print("=" * 60)
    import os
    if os.path.exists(input_obj_file):
        simplify_obj_file(input_obj_file, output_obj_file, target_ratio=0.65)
    else:
        print(f"OBJ file not found: {input_obj_file}")
        print("Note: OBJ file may be on SD card. Please simplify it manually if needed.")
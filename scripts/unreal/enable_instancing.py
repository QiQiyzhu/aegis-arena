"""Enable the actual material permutation needed by the new instanced arena trim."""
import unreal

material = unreal.load_asset("/Game/Aegis/Materials/M_AegisActor")
if not material:
    raise RuntimeError("Existing authored actor material is missing")
material.set_editor_property("used_with_instanced_static_meshes", True)
unreal.MaterialEditingLibrary.recompile_material(material)
if not unreal.EditorAssetLibrary.save_loaded_asset(material):
    raise RuntimeError("Could not save material usage permutation")
unreal.log("AEGIS_INSTANCING_MATERIAL_SAVED")

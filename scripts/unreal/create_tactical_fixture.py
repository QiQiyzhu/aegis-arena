"""Create the isolated v1.2 tactical fixture map without replacing existing assets."""
import unreal

path = "/Game/Aegis/Maps/AegisTacticalFunctional"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError("Tactical fixture already exists; refusing overwrite")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if not levels.new_level(path):
    raise RuntimeError("Cannot create tactical fixture map")
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
floor.set_actor_scale3d(unreal.Vector(40, 30, 1))
floor.set_actor_label("Tactical fixture navigation floor")
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1750, -1250, 150))
if not unreal.AegisEditorLibrary.add_navigation_bounds(editor.get_editor_world()):
    raise RuntimeError("Cannot add tactical navigation bounds")
fixture = actors.spawn_actor_from_class(unreal.AegisTacticalFunctionalTest, unreal.Vector(0, 0, 0))
fixture.set_actor_label("Tactical AI - real movement and authorized sight")
if not levels.save_current_level():
    raise RuntimeError("Cannot save tactical fixture")
unreal.log("AEGIS_TACTICAL_FIXTURE_CREATED")

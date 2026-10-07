"""Create the isolated objective integration fixture; never overwrite an asset."""
import unreal

path = "/Game/Aegis/Maps/AegisOperationFunctional"
assets = unreal.EditorAssetLibrary
if assets.does_asset_exist(path):
    raise RuntimeError("Operation fixture map already exists; refusing overwrite")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if not levels.new_level(path):
    raise RuntimeError("Could not create operation fixture map")
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
floor.set_actor_scale3d(unreal.Vector(40, 30, 1))
floor.set_actor_label("Operation fixture floor")
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1200, 0, 150))
runner = actors.spawn_actor_from_class(unreal.AegisScenarioRunner, unreal.Vector(0, 0, 0))
runner.set_editor_property("objective_trial", True)
for prop, name in (("behavior", "BT_Aegis"), ("cover_query", "EQS_Cover"),
                   ("attack_query", "EQS_Attack"), ("retreat_query", "EQS_Retreat")):
    asset = unreal.load_asset(f"/Game/Aegis/AI/{name}")
    if not asset:
        raise RuntimeError(f"Missing required AI asset: {name}")
    runner.set_editor_property(prop, asset)
if not unreal.AegisEditorLibrary.add_navigation_bounds(editor.get_editor_world()):
    raise RuntimeError("Operation fixture navigation bounds creation failed")
fixture = actors.spawn_actor_from_class(unreal.AegisOperationFunctionalTest, unreal.Vector(0, 0, 0))
fixture.set_actor_label("Operation objectives - stopped AI and authored occupants")
if not levels.save_current_level():
    raise RuntimeError("Operation fixture save failed")
unreal.log("AEGIS_OPERATION_FIXTURE_CREATED")

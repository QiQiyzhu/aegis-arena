"""Create a new isolated trial lifecycle map. Never overwrite an existing asset."""
import unreal

path = "/Game/Aegis/Maps/AegisTrialFunctional"
assets = unreal.EditorAssetLibrary
if assets.does_asset_exist(path):
    raise RuntimeError("Trial fixture map already exists; refusing overwrite")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if not levels.new_level(path):
    raise RuntimeError("Could not create isolated fixture map")
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
floor.set_actor_scale3d(unreal.Vector(40, 30, 1))
floor.set_actor_label("Trial fixture floor")
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1200, 0, 150))
runner = actors.spawn_actor_from_class(unreal.AegisScenarioRunner, unreal.Vector(0, 0, 0))
for prop, name in (("behavior", "BT_Aegis"), ("cover_query", "EQS_Cover"), ("attack_query", "EQS_Attack"), ("retreat_query", "EQS_Retreat")):
    runner.set_editor_property(prop, unreal.load_asset(f"/Game/Aegis/AI/{name}"))
if not unreal.AegisEditorLibrary.add_navigation_bounds(editor.get_editor_world()):
    raise RuntimeError("Navigation bounds creation failed")
fixture = actors.spawn_actor_from_class(unreal.AegisTrialFunctionalTest, unreal.Vector(0, 0, 0))
fixture.set_actor_label("Trial lifecycle - fixture damage, not balance evaluation")
if not levels.save_current_level():
    raise RuntimeError("Fixture save failed")
unreal.log("AEGIS_TRIAL_FIXTURE_CREATED")

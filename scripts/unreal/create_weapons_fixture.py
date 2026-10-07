"""Create one isolated native weapons fixture map without modifying existing maps."""
import unreal

path = "/Game/Aegis/Maps/AegisWeaponsFunctional"
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError("Weapons fixture already exists; refusing overwrite")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not levels.new_level(path):
    raise RuntimeError("Could not create weapons fixture map")
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube.Cube"))
floor.set_actor_scale3d(unreal.Vector(40, 30, 1))
floor.set_actor_label("Weapons fixture floor")
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1700, -1000, 150))
fixture = actors.spawn_actor_from_class(unreal.AegisWeaponsFunctionalTest, unreal.Vector(0, 0, 0))
fixture.set_actor_label("Weapons rules - fixed geometry and seeded windup")
if not levels.save_current_level():
    raise RuntimeError("Weapons fixture save failed")
unreal.log("AEGIS_WEAPONS_FIXTURE_CREATED")

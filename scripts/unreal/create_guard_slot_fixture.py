"""Create the isolated Guard firing-slot geometry fixture; never overwrite old evidence assets."""
import unreal

path = '/Game/Aegis/Maps/AegisGuardSlotFunctional'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    raise RuntimeError('Guard slot fixture already exists; refusing overwrite')
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
if not levels.new_level(path):
    raise RuntimeError('Cannot create Guard slot fixture map')
floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
floor.static_mesh_component.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube.Cube'))
floor.set_actor_scale3d(unreal.Vector(40, 30, 1))
floor.set_actor_label('Guard slot fixture navigation floor')
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1750, -1250, 150))
if not unreal.AegisEditorLibrary.add_navigation_bounds(editor.get_editor_world()):
    raise RuntimeError('Cannot add fixture navigation bounds')
fixture = actors.spawn_actor_from_class(unreal.AegisGuardSlotFunctionalTest, unreal.Vector(0, 0, 0))
fixture.set_actor_label('Guard slot: same geometry, baseline vs opt-in v2')
if not levels.save_current_level():
    raise RuntimeError('Cannot save Guard slot fixture')
unreal.log('AEGIS_GUARD_SLOT_FIXTURE_CREATED')

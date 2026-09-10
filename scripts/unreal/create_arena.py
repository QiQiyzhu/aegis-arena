"""Run inside UE 5.7 after compiling modules. Never fabricates .umap bytes outside UE.

Creates primitive geometry and a functional-test actor. NavMesh brush and BT/EQS
graphs must be authored/validated in the editor as documented in unreal-setup.md.
This script has not been executed without a installed Unreal Editor.
"""
import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assets = unreal.EditorAssetLibrary
MAP = "/Game/Aegis/Maps/AegisArena"
if assets.does_asset_exist(MAP):
    raise RuntimeError("AegisArena already exists; generation refuses to overwrite authored levels")
# Save dirty levels through the editor first; new_level closes the current level.
if not levels.save_all_dirty_levels():
    raise RuntimeError("Save current work before generating the arena")
if not levels.new_level(MAP):
    raise RuntimeError("Could not create arena level")

cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
if not cube:
    raise RuntimeError("Required Engine primitive cube missing")

def block(label, location, scale):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    if not actor:
        raise RuntimeError(f"Could not create {label}")
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor

block("Arena floor", (0,0,-50), (40,30,1))
for label, position, scale in (
    ("North wall", (0,1500,150), (40,0.5,4)),
    ("South wall", (0,-1500,150), (40,0.5,4)),
    ("East wall", (2000,0,150), (0.5,30,4)),
    ("West wall", (-2000,0,150), (0.5,30,4)),
    ("Cover A", (-300,400,150), (3.6,3.6,3)),
    ("Cover B", (300,-400,150), (3.6,3.6,3)),
    ("Cover C", (600,600,150), (2.8,2.8,3)),
    ("Cover D", (-700,-600,150), (2.8,2.8,3)),
):
    block(label, position, scale)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1200,0,150))
light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0,0,1200), unreal.Rotator(-55,-30,0))
actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0,0,1000))
runner_class = unreal.load_class(None, "/Script/AegisArena.AegisScenarioRunner")
test_class = unreal.load_class(None, "/Script/AegisArenaEditor.AegisCombatFunctionalTest")
if not runner_class or not test_class:
    raise RuntimeError("Compile both AegisArena and AegisArenaEditor modules first")
runner = actors.spawn_actor_from_class(runner_class, unreal.Vector(0,0,0))
runner.set_actor_label("Scenario Runner - configure BT and EQS")
test = actors.spawn_actor_from_class(test_class, unreal.Vector(-1200,900,0))
test.set_actor_label("Combat Functional Test")
if not levels.save_current_level():
    raise RuntimeError("Map save failed")
unreal.log_warning("Geometry generated. REQUIRED: add NavMeshBoundsVolume, build navigation, author/wire BT+EQS, run functional test. See docs/unreal-setup.md. No validation has been implied.")

"""Generate the UE 5.8 arena, functional map, BB/BT/EQS, and primitive materials.
Run with UnrealEditor-Cmd -run=pythonscript -script=<this absolute path>.
Existing authored maps/assets are refused, never overwritten or fabricated.
"""
import unreal

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assets = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
ARENA = "/Game/Aegis/Maps/AegisArena"
FUNCTIONAL = "/Game/Aegis/Maps/AegisFunctional"
AI_NAMES = ("BB_Aegis", "BT_Aegis", "EQS_Cover", "EQS_Attack", "EQS_Retreat")
for name in (ARENA, FUNCTIONAL):
    if assets.does_asset_exist(name):
        raise RuntimeError(f"Refusing to overwrite authored level: {name}")
existing = [assets.does_asset_exist(f"/Game/Aegis/AI/{name}") for name in AI_NAMES]
if any(existing) and not all(existing):
    raise RuntimeError("Partial AI asset set exists. Inspect the previous generation failure.")
if not any(existing) and not unreal.AegisEditorLibrary.build_ai_assets():
    raise RuntimeError("Native BB / BT / EQS generation failed")


def material(name, color, parameter=False):
    path = f"/Game/Aegis/Materials/{name}"
    if assets.does_asset_exist(path):
        return unreal.load_asset(path)
    result = tools.create_asset(name, "/Game/Aegis/Materials", unreal.Material, unreal.MaterialFactoryNew())
    expression = unreal.MaterialEditingLibrary.create_material_expression(
        result, unreal.MaterialExpressionVectorParameter if parameter else unreal.MaterialExpressionConstant3Vector, -300, 0)
    if parameter:
        expression.set_editor_property("parameter_name", "Tint")
        expression.set_editor_property("default_value", unreal.LinearColor(*color))
    else:
        expression.set_editor_property("constant", unreal.LinearColor(*color))
    unreal.MaterialEditingLibrary.connect_material_property(expression, "", unreal.MaterialProperty.MP_BASE_COLOR)
    roughness = unreal.MaterialEditingLibrary.create_material_expression(result, unreal.MaterialExpressionConstant, -300, 150)
    roughness.set_editor_property("r", 0.72)
    unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(result)
    if not assets.save_loaded_asset(result):
        raise RuntimeError(f"Material save failed: {path}")
    return result

floor_mat = material("M_Floor", (0.045, 0.065, 0.09, 1))
wall_mat = material("M_Cover", (0.18, 0.24, 0.30, 1))
material("M_AegisActor", (0.1, 0.8, 0.9, 1), True)
cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")
if not cube:
    raise RuntimeError("Engine primitive cube missing")


def block(label, location, scale, mat=wall_mat):
    actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
    if not actor:
        raise RuntimeError(f"Could not create {label}")
    actor.set_actor_label(label)
    actor.static_mesh_component.set_static_mesh(cube)
    actor.static_mesh_component.set_material(0, mat)
    actor.set_actor_scale3d(unreal.Vector(*scale))
    return actor


def lighting():
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1500),
                                         unreal.Rotator(pitch=-65, yaw=-35, roll=0))
    sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 1200))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)
    sky.get_component_by_class(unreal.SkyLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)


if not levels.save_all_dirty_levels() or not levels.new_level(ARENA):
    raise RuntimeError("Could not save current work and create arena")
block("Arena floor", (0, 0, -50), (40, 30, 1), floor_mat)
for label, position, scale in (
    ("North wall", (0, 1500, 150), (40, 0.5, 4)),
    ("South wall", (0, -1500, 150), (40, 0.5, 4)),
    ("East wall", (2000, 0, 150), (0.5, 30, 4)),
    ("West wall", (-2000, 0, 150), (0.5, 30, 4)),
    ("Cover A", (-300, 400, 150), (3.6, 3.6, 3)),
    ("Cover B", (300, -400, 150), (3.6, 3.6, 3)),
    ("Cover C", (600, 600, 150), (2.8, 2.8, 3)),
    ("Cover D", (-700, -600, 150), (2.8, 2.8, 3)),
):
    block(label, position, scale)
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1200, 0, 150))
lighting()
camera = actors.spawn_actor_from_class(unreal.CameraActor, unreal.Vector(-1600, 0, 2900),
                                        unreal.Rotator(pitch=-62, yaw=0, roll=0))
camera.set_actor_label("Evaluation overview camera")
camera.set_editor_property("tags", ["AegisEvaluationCamera"])
runner = actors.spawn_actor_from_class(unreal.AegisScenarioRunner, unreal.Vector(0, 0, 0))
runner.set_actor_label("Scenario Runner - native BT Utility evaluation")
for prop, asset_name in (("behavior", "BT_Aegis"), ("cover_query", "EQS_Cover"), ("attack_query", "EQS_Attack"), ("retreat_query", "EQS_Retreat")):
    runner.set_editor_property(prop, unreal.load_asset(f"/Game/Aegis/AI/{asset_name}"))
if not unreal.AegisEditorLibrary.add_navigation_bounds(editor.get_editor_world()):
    raise RuntimeError("Native navigation brush generation failed")
if not levels.save_current_level():
    raise RuntimeError("Arena map save failed")

if not levels.new_level(FUNCTIONAL):
    raise RuntimeError("Functional level creation failed")
block("Functional test floor", (0, 0, -50), (20, 20, 1), floor_mat)
lighting()
actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-700, -700, 150))
test = actors.spawn_actor_from_class(unreal.AegisCombatFunctionalTest, unreal.Vector(-400, 0, 0))
test.set_actor_label("Aegis Combat Functional Test")
if not levels.save_current_level():
    raise RuntimeError("Functional map save failed")
levels.load_level(ARENA)
unreal.log("AEGIS_ASSETS_GENERATED: two real maps, native BB/BT, three EQS queries, navigation brush, three primitive materials")

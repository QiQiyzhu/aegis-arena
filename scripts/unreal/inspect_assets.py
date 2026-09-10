"""Read saved UE assets inside the editor and reject incomplete graph/map wiring."""
import datetime
import json
from pathlib import Path
import unreal


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


tree = require(unreal.load_asset("/Game/Aegis/AI/BT_Aegis"), "Missing BT")
root = require(tree.get_editor_property("root_node"), "Saved BT lost its root")
root_children = root.get_editor_property("children")
require(len(root_children) == 2, "Expected decision selector followed by 200ms Wait")
selector = require(root_children[0].child_composite, "Missing role selector")
roles = selector.get_editor_property("children")
require(len(roles) == 2, "Companion/enemy roles must remain separate")
role_actions = []
for role in roles:
    require(len(role.decorators) == 1, "Each role needs a guard")
    composite = require(role.child_composite, "Role selector missing")
    actions = [str(child.child_task.get_editor_property("node_name"))
               for child in composite.get_editor_property("children")]
    role_actions.append(actions)
require(role_actions[0] == ["Recover", "Retreat", "Support", "Attack", "Chase", "Follow", "Wait"],
        "Unexpected saved Companion actions")
require(role_actions[1] == ["Retreat", "Recover", "FindCover", "Attack", "AttackPosition", "Investigate", "Patrol", "Wait"],
        "Unexpected saved enemy actions")
blackboard = require(tree.get_editor_property("blackboard_asset"), "Missing BB reference")
keys = [str(key.get_editor_property("entry_name")) for key in blackboard.get_editor_property("keys")]
expected_keys = {"HasLOS", "HasMemory", "CriticalHealth", "NeedsRecovery", "NeedsCover", "InRange",
                 "IsCompanion", "QueryPending", "UtilityAction", "TargetActor", "LastKnownLocation", "TacticalPoint"}
# UE's bAddBlackboardSelfKey adds its own persistent SelfActor entry.
require(set(keys) - {"SelfActor"} == expected_keys and len(keys) == len(set(keys)),
        f"Unexpected or duplicate BB keys: {keys}")
queries = {}
for name in ("EQS_Cover", "EQS_Attack", "EQS_Retreat"):
    query = require(unreal.load_asset(f"/Game/Aegis/AI/{name}"), f"Missing {name}")
    summary = json.loads(unreal.AegisEditorLibrary.describe_query(query))
    require(summary["optionCount"] == 1 and summary.get("generator") == "EnvQueryGenerator_SimpleGrid",
            f"{name} expected one simple grid option")
    require(summary.get("tests") == ["EnvQueryTest_Pathfinding", "EnvQueryTest_Trace", "EnvQueryTest_Distance"],
            f"{name} expected path, trace and distance tests")
    require(summary.get("distanceFactor") == 1, f"{name} score unexpectedly migrated or inverted")
    queries[name] = summary

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
require(levels.load_level("/Game/Aegis/Maps/AegisArena"), "Arena failed to load")
world_actors = actors.get_all_level_actors()
cameras = [actor for actor in world_actors if isinstance(actor, unreal.CameraActor)
           and actor.get_actor_label() == "Evaluation overview camera"]
require(len(cameras) == 1, "Expected one evaluation camera")
require(cameras[0].actor_has_tag("AegisEvaluationCamera"), "Evaluation camera runtime tag missing")
camera_rotation = cameras[0].get_actor_rotation()
require(abs(camera_rotation.pitch + 62) < 0.01 and abs(camera_rotation.roll) < 0.01,
        "Evaluation camera must look downward; Python Rotator positional order is roll/pitch/yaw")
lights = [actor for actor in world_actors if isinstance(actor, (unreal.DirectionalLight, unreal.SkyLight))]
for light in lights:
    component_class = unreal.DirectionalLightComponent if isinstance(light, unreal.DirectionalLight) else unreal.SkyLightComponent
    require(light.get_component_by_class(component_class).get_editor_property("mobility") == unreal.ComponentMobility.MOVABLE,
            "The dynamic demonstration scene must not require unbuilt lightmaps")
bounds = [actor for actor in world_actors if isinstance(actor, unreal.NavMeshBoundsVolume)]
nav_data = [actor for actor in world_actors if isinstance(actor, unreal.RecastNavMesh)]
require(len(nav_data) == 1 and nav_data[0].get_editor_property("force_rebuild_on_load"),
        "Dynamic demonstration navigation must rebuild after loading an unbaked commandlet map")
require(len(bounds) == 1, "Expected one real NavMeshBoundsVolume")
origin, extent = bounds[0].get_actor_bounds(False)
require(extent.x >= 2000 and extent.y >= 1500 and extent.z >= 300,
        f"Navigation brush has invalid bounds: {extent}")
runners = [actor for actor in world_actors if isinstance(actor, unreal.AegisScenarioRunner)]
require(len(runners) == 1, "Expected one scenario owner")
for name in ("behavior", "cover_query", "attack_query", "retreat_query"):
    require(runners[0].get_editor_property(name), f"Scenario reference {name} missing")
require(levels.load_level("/Game/Aegis/Maps/AegisFunctional"), "Functional map failed to load")
functional = [actor for actor in actors.get_all_level_actors()
              if isinstance(actor, unreal.AegisCombatFunctionalTest)]
require(len(functional) == 1, "Expected one native functional test actor")
report = {"engine": "unreal-editor-asset-inspection", "passed": True,
          "checkedAtUtc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
          "blackboardKeys": keys, "roleActions": role_actions, "queries": queries,
          "navBoundsExtentCm": [extent.x, extent.y, extent.z],
          "cameraPitchDegrees": camera_rotation.pitch, "movableLights": len(lights),
          "arenaActorCount": len(world_actors), "functionalTestActors": len(functional),
          "limitation": "Saved assets and wiring only; no game-world behavior or rendered acceptance"}
path = Path(unreal.Paths.project_saved_dir()) / (
    "AegisAssetInspection-" + datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-%f") + ".json")
path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
unreal.log(f"AEGIS_ASSET_INSPECTION_PASS {path}")

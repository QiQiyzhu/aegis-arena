"""Apply the explicit camera/dynamic-light specification to the two Aegis maps in UE."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for name in ("AegisArena", "AegisFunctional"):
    if not levels.load_level(f"/Game/Aegis/Maps/{name}"):
        raise RuntimeError(f"Cannot load {name}")
    for actor in actors.get_all_level_actors():
        if isinstance(actor, unreal.RecastNavMesh):
            # Commandlet saves do not bake navigation; rebuild it in the game world.
            actor.set_editor_property("force_rebuild_on_load", True)
        if isinstance(actor, unreal.CameraActor) and actor.get_actor_label() == "Evaluation overview camera":
            actor.set_actor_rotation(unreal.Rotator(pitch=-62, yaw=0, roll=0), False)
            actor.set_editor_property("tags", ["AegisEvaluationCamera"])
        if isinstance(actor, unreal.DirectionalLight):
            actor.set_actor_rotation(unreal.Rotator(pitch=-65, yaw=-35, roll=0), False)
            actor.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)
        if isinstance(actor, unreal.SkyLight):
            actor.get_component_by_class(unreal.SkyLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)
    if not levels.save_current_level():
        raise RuntimeError(f"Cannot save {name}")
unreal.log("AEGIS_PRESENTATION_CONFIGURED: explicit pitch/yaw/roll and movable lighting; no baked lightmaps")

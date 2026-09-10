using UnrealBuildTool;
public class AegisArenaEditorTarget : TargetRules {
 public AegisArenaEditorTarget(TargetInfo Target) : base(Target) {
  Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.V7;
  IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
  ExtraModuleNames.AddRange(new[] { "AegisArena", "AegisArenaEditor" });
 }
}

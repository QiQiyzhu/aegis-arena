using UnrealBuildTool;
public class AegisArenaEditorTarget : TargetRules {
 public AegisArenaEditorTarget(TargetInfo Target) : base(Target) {
  Type = TargetType.Editor; DefaultBuildSettings = BuildSettingsVersion.V5;
  ExtraModuleNames.AddRange(new[] { "AegisArena", "AegisArenaEditor" });
 }
}

using UnrealBuildTool;
public class AegisArenaTarget : TargetRules {
 public AegisArenaTarget(TargetInfo Target) : base(Target) {
  Type = TargetType.Game; DefaultBuildSettings = BuildSettingsVersion.V5;
  ExtraModuleNames.Add("AegisArena");
 }
}

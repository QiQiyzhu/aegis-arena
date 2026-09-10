using UnrealBuildTool;
public class AegisArenaTarget : TargetRules {
 public AegisArenaTarget(TargetInfo Target) : base(Target) {
  Type = TargetType.Game; DefaultBuildSettings = BuildSettingsVersion.V7;
  IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
  ExtraModuleNames.Add("AegisArena");
 }
}

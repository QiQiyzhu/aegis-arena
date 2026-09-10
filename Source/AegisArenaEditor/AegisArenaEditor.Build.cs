using UnrealBuildTool;
public class AegisArenaEditor : ModuleRules {
 public AegisArenaEditor(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AegisArena", "FunctionalTesting" });
  PrivateDependencyModuleNames.Add("UnrealEd");
 }
}

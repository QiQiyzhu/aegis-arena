using UnrealBuildTool;
public class AegisArenaEditor : ModuleRules {
 public AegisArenaEditor(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage=PCHUsageMode.UseExplicitOrSharedPCHs;
  PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "AegisArena", "FunctionalTesting" });
  PrivateDependencyModuleNames.AddRange(new[] { "UnrealEd", "AIModule", "AIGraph", "BehaviorTreeEditor", "NavigationSystem", "AssetRegistry", "Json" });
 }
}

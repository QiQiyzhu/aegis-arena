using UnrealBuildTool;
using System.IO;
public class AegisArena : ModuleRules {
 public AegisArena(ReadOnlyTargetRules Target) : base(Target) {
  PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
  CppStandard = CppStandardVersion.Cpp20;
  PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "AIModule", "NavigationSystem", "GameplayTasks", "Json", "JsonUtilities", "RenderCore", "HTTP", "Slate", "SlateCore", "AudioMixer" });
  PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../core/include")));
 }
}

// Copyright ppmpreetham 2025-03-04 10:18:21, All Rights Reserved.

using UnrealBuildTool;

public class Blend4Real : ModuleRules
{
	public Blend4Real(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PrivateIncludePaths.AddRange(
			new string[]
			{
			}
		);

		PublicDependencyModuleNames.AddRange(
			new[]
			{
				"Core", "LevelEditor"
			}
		);
		if (Target.Platform == UnrealTargetPlatform.Mac) PublicFrameworks.Add("Carbon");

		PrivateDependencyModuleNames.AddRange(
			new[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"EditorStyle",
				"UnrealEd",
				"LevelEditor",
				"Projects",
				"ToolMenus",
				"EditorInteractiveToolsFramework",
				"DeveloperSettings",
				"Kismet",
				"SubobjectEditor",
				"SubobjectDataInterface"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
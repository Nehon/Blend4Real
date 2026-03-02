// Copyright 2025-Present - Nehon (Rémy Bouquet). All Rights Reserved.

using UnrealBuildTool;
using System.IO;

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
				"SubobjectDataInterface",
				"ComponentVisualizers",
			"ControlRig",
			"Persona",
			"AnimGraph",
			"SkeletalMeshEditor",
			"SkeletalMeshUtilitiesCommon",
			"MeshModelingToolsEditorOnly",
			"SkeletalMeshModifiers",
			"InteractiveToolsFramework"
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
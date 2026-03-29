// Copyright 2024-2025 Evianaive. All Rights Reserved.
using UnrealBuildTool;

public class StructFunctionKismet : ModuleRules
{
	public StructFunctionKismet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"UnrealEd",
				"BlueprintGraph",
				"KismetCompiler",
				"Kismet",
				"StructUtils",
				"StructFunctionRuntime",
			}
		);
	}
}

// Copyright 2024-2025 Evianaive. All Rights Reserved.
using UnrealBuildTool;

public class StructFunctionRuntime : ModuleRules
{
	public StructFunctionRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
			}
		);

		PrivateDependencyModuleNames.AddRange(new string[] { });
	}
}

// Copyright ZeroLight ltd. All Rights Reserved.
using UnrealBuildTool;

// Lightweight module that owns ZLCloudPlugin's global shaders.
// loads at PostConfigInit so IMPLEMENT_GLOBAL_SHADER registers before shader types are finalised.
public class ZLCloudPluginShaders : ModuleRules
{
	public ZLCloudPluginShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrecompileForTargets = PrecompileTargetsType.Any;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"RHI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"CoreUObject",
			"Engine",
			"Projects",
			"RenderCore",
			"Renderer",
			"RHI",
		});
	}
}

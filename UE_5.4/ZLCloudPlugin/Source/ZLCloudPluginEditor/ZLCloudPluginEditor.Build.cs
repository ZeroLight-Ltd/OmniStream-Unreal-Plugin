using UnrealBuildTool;

public class ZLCloudPluginEditor : ModuleRules
{
    public ZLCloudPluginEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "KismetCompiler", "BlueprintGraph",
            "ZLCloudPlugin", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "GraphEditor"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "KismetCompiler", "BlueprintGraph",
            "ZLCloudPlugin", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "GraphEditor"
        });

        bUseUnity = true;
    }
}

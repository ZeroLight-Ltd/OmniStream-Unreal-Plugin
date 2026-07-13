using UnrealBuildTool;

public class ZLCloudPluginEditor : ModuleRules
{
    public ZLCloudPluginEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "KismetCompiler", "BlueprintGraph",
            "ZLCloudPlugin", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "GraphEditor",
            "ToolMenus"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine", "InputCore", "KismetCompiler", "BlueprintGraph",
            "ZLCloudPlugin", "Slate", "SlateCore", "EditorStyle", "UnrealEd", "GraphEditor",
            "DeveloperToolSettings",
            "AssetRegistry"
        });

        bUseUnity = true;
    }
}

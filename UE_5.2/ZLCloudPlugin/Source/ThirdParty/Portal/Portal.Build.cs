// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class Portal : ModuleRules
{
	public Portal(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Add the import library
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "Release", "portal_client_c.lib"));

			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add("portal_client_c.dll");

			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries/ThirdParty/Portal/Win64/portal_client_c.dll"), Path.Combine(ModuleDirectory, "x64", "Release", "portal_client_c.dll"));
		}
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
			//TODO
        }
        else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			//TODO
		}
	}
}

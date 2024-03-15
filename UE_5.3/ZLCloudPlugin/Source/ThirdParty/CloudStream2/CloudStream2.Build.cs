// Fill out your copyright notice in the Description page of Project Settings.

using System.IO;
using UnrealBuildTool;

public class CloudStream2 : ModuleRules
{
	public CloudStream2(ReadOnlyTargetRules Target) : base(Target)
	{
		Type = ModuleType.External;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Add the import library
			PublicAdditionalLibraries.Add(Path.Combine(ModuleDirectory, "x64", "Release", "CloudStream2.lib"));

			// Delay-load the DLL, so we can load it from the right place first
			PublicDelayLoadDLLs.Add("CloudStream2.dll");

			// Ensure that the DLL is staged along with the executable
			RuntimeDependencies.Add(Path.Combine(PluginDirectory, "Binaries/ThirdParty/CloudStream2/Win64/CloudStream2.dll"), Path.Combine(ModuleDirectory, "x64", "Release", "CloudStream2.dll"));
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

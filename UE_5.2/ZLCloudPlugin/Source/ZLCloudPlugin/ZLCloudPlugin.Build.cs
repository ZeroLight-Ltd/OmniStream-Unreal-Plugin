// Copyright ZeroLight ltd. All Rights Reserved.
using System;
using System.IO;
using System.Collections.Generic;
using EpicGames.Core;
using UnrealBuildTool;

public class ZLCloudPlugin : ModuleRules
{
	public ZLCloudPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
          bUsePrecompiled=true;

		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Private/PCH.h";

		var EngineDir = Path.GetFullPath(Target.RelativeEnginePath);
		
		bool version_5_1_or_newer = false;
		if (Target.Version.MajorVersion >= 5 && Target.Version.MinorVersion >= 1 )
		{
			version_5_1_or_newer = true;
		}
		

		// This is so for game projects using our public headers don't have to include extra modules they might not know about.
		PublicDependencyModuleNames.AddRange(new string[]
		{
				"InputDevice",
				"Json", 
				"JsonUtilities",
		});
		
		if(version_5_1_or_newer)
		{
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"PixelCapture",
			});
		}

		// NOTE: General rule is not to access the private folder of another module
		PrivateIncludePaths.AddRange(new string[]
		{
				Path.Combine(EngineDir, "Source/Runtime/AudioMixer/Private"),
				Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private")
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
				"ApplicationCore",
				"Core",
				"CoreUObject",
				"Engine",
				"InputCore",
				"Json",
				"RenderCore",
				"RHI",
				"RHICore",
				"Slate",
				"SlateCore",
				"AudioMixer",
				"WebRTC",
				"WebSockets",
				"Sockets",
				"MediaUtils",
				"DeveloperSettings",
				"AVEncoder",
				"Projects",
				"Networking",
				"CloudStream2"
		});
		
		if(version_5_1_or_newer)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"PixelCaptureShaders",
			});
		}


		if (Target.Type == TargetType.Editor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
					"MainFrame",
					"LevelEditor",
					"UATHelper",
					"DesktopPlatform",
					"AssetTools",
					"OutputLog",
					"LiveCoding",
					"UnrealEd",
					"Portal"
			});
		}


		AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

		// required for casting UE4 BackBuffer to Vulkan Texture2D for NvEnc
		PrivateDependencyModuleNames.AddRange(new string[] { "CUDA", "VulkanRHI", "nvEncode" });

		AddEngineThirdPartyPrivateStaticDependencies(Target, "NVAftermath");

		PrivateIncludePathModuleNames.Add("VulkanRHI");
		PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private"));
		AddEngineThirdPartyPrivateStaticDependencies(Target, "Vulkan");

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Windows))
		{
			PublicDependencyModuleNames.Add("D3D11RHI");
			PublicDependencyModuleNames.Add("D3D12RHI");
			PrivateIncludePaths.AddRange(
				new string[]{
						Path.Combine(EngineDir, "Source/Runtime/D3D12RHI/Private"),
						Path.Combine(EngineDir, "Source/Runtime/D3D12RHI/Private/Windows")
				});

			AddEngineThirdPartyPrivateStaticDependencies(Target, "DX12");
			PublicSystemLibraries.AddRange(new string[] {
					"DXGI.lib",
					"d3d11.lib",
				});

			PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Windows"));
		}
		else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrivateIncludePaths.Add(Path.Combine(EngineDir, "Source/Runtime/VulkanRHI/Private/Linux"));
		}
	}
}

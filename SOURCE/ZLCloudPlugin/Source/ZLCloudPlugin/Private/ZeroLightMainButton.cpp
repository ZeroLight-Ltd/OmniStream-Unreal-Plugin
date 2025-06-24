// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#define ALLOW_GITHUB_RELEASE_CHECKS 1

#include "ZeroLightMainButton.h"
#include "LevelEditor.h"
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SWindowTitleBar.h"
#include "Interfaces/IMainFrameModule.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SUserWidget.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFilemanager.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IProjectManager.h"
#include "ProjectDescriptor.h"
#include "GameProjectUtils.h"
#include "ISettingsModule.h"
#include "ILiveCodingModule.h"
#include "IUATHelperModule.h"
#include <filesystem>
#include "OutputLogModule.h"
#include "ZeroLightEditorStyle.h"
#include "ZeroLightAboutScreen.h"
#include "ZLStateEditor.h"
#include "CloudStream2.h"
#include "HttpModule.h"
#include "Http.h"
#include "BlueprintEditor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "K2Node_CallFunction.h"
#include "Editor.h"

#define LOCTEXT_NAMESPACE "ZLCloudPlugin"

DEFINE_LOG_CATEGORY(LogPortalCLI);

//add defines for multi platform compile
#define BUILDPLATFORM "Win64"

#define XR_DEPS_VERSION_STRING "1.0.3"
#define XR_CAPS_VERSION_STRING "1.0.1"
#define SPOTLITE_CAPS_VERSION_STRING "1.0.1"
#define SPOTLITE_CAPS_CAMERA_VERSION_STRING "1.0.1"
//#define SPOTLITE_CAPS_POSTEFFECTS_VERSION_STRING "1.0"

static TSharedPtr<SHorizontalBox> NotificationUsersBox = nullptr;

TSharedPtr<SHorizontalBox> FZeroLightMainButton::GetUserBox()
{
	return NotificationUsersBox;
}

void FZeroLightMainButton::CheckForNewerPlugin()
{
	FString GitHubApiUrl = TEXT("https://api.github.com/repos/ZeroLight-Ltd/OmniStream-Unreal-Plugin/releases/latest");

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> request = FHttpModule::Get().CreateRequest();
	request->SetURL(GitHubApiUrl);
	request->SetVerb("GET");
	request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	request->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr request, FHttpResponsePtr response, bool bWasSuccessful)
	{
		if (bWasSuccessful && response.IsValid() && response->GetResponseCode() == 200)
		{
			FString responseContent = response->GetContentAsString();
			TSharedPtr<FJsonObject> JsonObject;
			TSharedRef<TJsonReader<TCHAR>> reader = TJsonReaderFactory<TCHAR>::Create(responseContent);

			if (FJsonSerializer::Deserialize(reader, JsonObject) && JsonObject.IsValid())
			{
				if (JsonObject->HasField("tag_name"))
				{
					githubVersion = JsonObject->GetStringField("tag_name");
				}
			}

			githubVersion.RemoveFromStart("v");

			IPluginManager& PluginManager = IPluginManager::Get();
			TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
			FString currentVersion = "1.0.0";
			if (ZlCloudStreamPlugin)
			{
				const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
				currentVersion = *Descriptor.VersionName;

				currentVersion.RemoveFromStart("v");

				auto SplitVersion = [](const FString& Version) -> TArray<int32>
				{
					TArray<FString> parts;
					Version.ParseIntoArray(parts, TEXT("."), true);

					TArray<int32> versionNumbers;
					for (const FString& part : parts)
					{
						versionNumbers.Add(FCString::Atoi(*part));  // Convert each part to int32
					}

					return versionNumbers;
				};

				auto PluginVersionNewer = [](const TArray<int32> currentPluginSemvers, const TArray<int32> comparisonPluginSemvers) -> bool
				{
					// Compare major, minor, patch in order
					for (int32 i = 0; i < 3; ++i)
					{
						if (comparisonPluginSemvers[i] > currentPluginSemvers[i])
						{
							return true;
						}
						else if (comparisonPluginSemvers[i] <= currentPluginSemvers[i])
						{
							return false;
						}
					}

					return false;
				};

				// Split both versions into parts (major, minor, patch)
				TArray<int32> currentPluginSemvers = SplitVersion(currentVersion);
				TArray<int32> comparisonPluginSemvers = SplitVersion(githubVersion);

				// Ensure both versions have exactly 3 parts (major.minor.patch)
				while (currentPluginSemvers.Num() < 3) currentPluginSemvers.Add(0);  // If any part is missing, assume 0
				while (comparisonPluginSemvers.Num() < 3) comparisonPluginSemvers.Add(0);

				if (PluginVersionNewer(currentPluginSemvers, comparisonPluginSemvers))
				{
					UE_LOG(LogPortalCLI, Log, TEXT("Newer OmniStream plugin version available: %s"), *githubVersion);
					newerPluginAvailable = true;
				}
				else
				{
					newerPluginAvailable = false;
				}
			}
		}
	});

	request->ProcessRequest();
}

void FZeroLightMainButton::Initialize()
{
#if ALLOW_GITHUB_RELEASE_CHECKS
	CheckForNewerPlugin();
#endif

	s_progressText = FText::FromString("");

	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
	LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(Extender);

	Extender->AddToolBarExtension("File", EExtensionHook::After, MakeShared<FUICommandList>(), FToolBarExtensionDelegate::CreateLambda(
		[this](FToolBarBuilder& ToolBarBuilder)
		{
			ToolBarBuilder.AddComboButton(
				FUIAction(),
				FOnGetContent::CreateSP(this, &FZeroLightMainButton::GetMenu),
				TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FZeroLightMainButton::GetButtonText)),
				TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FZeroLightMainButton::GetTooltip)),
				TAttribute<FSlateIcon>::Create(TAttribute<FSlateIcon>::FGetter::CreateSP(this, &FZeroLightMainButton::GetButtonIcon))
			);
		}
	));
}

FText FZeroLightMainButton::GetTooltip()
{
	return FText::FromString(TEXT("ZeroLight OmniStream"));
}

FText FZeroLightMainButton::GetButtonText()
{
	return FText::FromString("ZeroLight");
}

FText FZeroLightMainButton::GetDeployButtonText() const
{
	return s_useExisting ? FText::FromString("Upload to OmniStream") : FText::FromString("Build and Upload to OmniStream");
}

FText FZeroLightMainButton::GetCurrentProgressText() const
{
	return s_progressText;
}

FText FZeroLightMainButton::GetLastSucessBuildRef() const
{
	return FText::FromString(s_lastSucessPortalBuildRef);
}

FText FZeroLightMainButton::UpdateProgress() const
{
	if (s_triggerBuildTask)
	{
		s_triggerBuildTask = false;

		if (BuildAndDeployWnd != nullptr)
		{
			BuildAndDeployWnd->BringToFront();
		}

		StartBuildAndDeployTask();
	}

	if (s_triggerExistingBuildUploadTask)
	{
		s_triggerExistingBuildUploadTask = false;

		if (BuildAndDeployWnd != nullptr)
		{
			BuildAndDeployWnd->BringToFront();
		}

		StartExistingBuildDeployTask();
	}

	if (s_sendBuildFailed)
	{
		if (BuildAndDeployWnd != nullptr)
		{
			BuildAndDeployWnd->BringToFront();
		}

		portalCLI->ReportBuildFailure();

		s_sendBuildFailed = false;
	}

	if (s_triggerBuildUpload)
	{
		if (BuildAndDeployWnd != nullptr)
		{
			BuildAndDeployWnd->BringToFront();
		}

		UE_LOG(LogPortalCLI, Log, TEXT("Build Complete - Starting Upload..."));

		s_triggerBuildUpload = false;

		if (TriggerBuildUpload())
		{

		}
	}

	if (s_needsRefocusWindow)
	{
		if (GEditor)
		{
			TSharedPtr<SWindow> ParentWindow = FGlobalTabmanager::Get()->GetRootWindow();

			if (ParentWindow.IsValid())
				ParentWindow->HACK_ForceToFront();

			// Optionally, you can try to bring the main frame window to the front:
			//TSharedPtr<SWindow> MainFrame = FSlateApplication::Get().GetActiveTopLevelWindow();
			//if (MainFrame.IsValid())
			//{
			//	MainFrame->BringToFront(true);
			//}

			if (BuildAndDeployWnd != nullptr)
			{
				BuildAndDeployWnd->HACK_ForceToFront();
			}
		}

		s_needsRefocusWindow = false;
	}

	return s_progressText;
}

void FZeroLightMainButton::StartBuildAndDeployTask() const
{
	if(s_isCIBuild)
		UE_LOG(LogPortalCLI, Log, TEXT("Starting CI Build & Upload Task..."));

	FString RunningEditorExe = FPlatformProcess::ExecutableName();
	UE_LOG(LogPortalCLI, Log, TEXT("Editor Executable: %s"), *RunningEditorExe);

	FString ConfigName = TEXT("Development"); // Default
	bool needsExeArg = false;
	if (RunningEditorExe.Contains(TEXT("DebugGame")))
	{
		ConfigName = TEXT("DebugGame");
		needsExeArg = true;
	}

	// Ensure engine path ends with slash
	FString engineDir = FPaths::EngineDir();
	if (!engineDir.EndsWith(TEXT("/")) && !engineDir.EndsWith(TEXT("\\")))
	{
		engineDir += TEXT("/");
	}

	FString projectPath = FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath());

	FString runUATPath = FPaths::ConvertRelativePathToFull(engineDir + "Build/BatchFiles/RunUAT.bat");

	FString outFolderName = GetBuildFolder();

	FString commandArgs("BuildCookRun -platform=Win64 -nop4 -skipbuildeditor -nocompileeditor -cook -build -stage -allmaps -archive -pak -package ");

	commandArgs += FString("-project=\"" + projectPath + "\" ");
	commandArgs += FString("-clientconfig=Development ");

	//Ensure we dont have a unclean current build context
	portalCLI->ClearCurrentBuild();

	SetProgressText(FText::FromString("Starting build process..."));

	commandArgs += FString("-archivedirectory=\"" + outFolderName + "\" ");

	if (needsExeArg)
	{
		// Append correct Unreal Editor executable path using absolute path
		FString unrealExePath = FPaths::ConvertRelativePathToFull(engineDir + "Binaries/Win64/UnrealEditor-Win64-" + ConfigName + "-Cmd.exe");
		FString unrealExeArg = FString::Printf(TEXT("-unrealexe=\"%s\" "), *unrealExePath);
		commandArgs += unrealExeArg;
	}

	UE_LOG(LogPortalCLI, Log, TEXT("UAT Command: %s %s"), *runUATPath, *commandArgs);

	TFunction<void(FString result, double timeElapsed)> OnBuildFinished = [this](FString result, double timeElapsed) {

		if (result == "Completed")
		{
			OnBuildPackageSuccess();
		}
		else
		{
			SetProgressText(FText::FromString("Build encountered errors during deployment"));

			s_sendBuildFailed = true;
			s_isCurrentlyBuilding = false;

			if (s_isCIBuild)
			{
				UE_LOG(LogPortalCLI, Error, TEXT("Command Line Build & Upload failed with engine build errors - see build log for info. Aborting..."));
				std::exit(-1);
			}
		}
	};

	SetProgressText(FText::FromString("Building..."));

	if (s_isCIBuild)
		UE_LOG(LogPortalCLI, Log, TEXT("Running UATHelper..."));

	SetBuildFolder(outFolderName);
	s_isCurrentlyBuilding = true;
	IUATHelperModule& uatHelper = FModuleManager::LoadModuleChecked<IUATHelperModule>("UATHelper");
	uatHelper.CreateUatTask(
		commandArgs,
		FText::FromString("Win64"),
		LOCTEXT("PackagingProjectTaskName", "Packaging Project"),
		LOCTEXT("PackagingTaskName", "Packaging"),
		FAppStyle::Get().GetBrush(TEXT("MainFrame.PackageProject")),
		nullptr,
		OnBuildFinished,
		outFolderName);
}

void FZeroLightMainButton::StartExistingBuildDeployTask() const
{
	FString existingBuildFolder = GetBuildFolder();
	//Ensure we dont have a unclean current build context
	portalCLI->ClearCurrentBuild();

	FString exeStr = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
	FString exeName = exeStr + "-Win64-Shipping.exe"; //Release exe name
	FString altExeName = exeStr + ".exe";

	//Blueprint-only projects use this name
	FString defaultExeName = "UnrealGame.exe";

	if (FPaths::FileExists(existingBuildFolder + "/" + exeStr + "/Binaries/" + BUILDPLATFORM + "/" + exeName) ||
		FPaths::FileExists(existingBuildFolder + "/" + exeStr + "/Binaries/" + BUILDPLATFORM + "/" + altExeName) ||
		FPaths::FileExists(existingBuildFolder + "/" + exeStr + "/Binaries/" + BUILDPLATFORM + "/" + defaultExeName))
	{

		SetProgressText(FText::FromString("Starting deploy process..."));
		SetBuildFolder(existingBuildFolder);

		if (!AddRunInfoJSONAndMetadataToBuild(existingBuildFolder, ""))
		{
			//Error creating runinfo json
			//success = false;
		}

		if (!PortalUploadAndZip(existingBuildFolder))
		{
			//Error uploading to Portal
			//success = false;
		}

	}
	else 
	{
		FString overrideExeName = FindOverrideExeName(existingBuildFolder);
		FString overrideProjectName = FindOverrideProjectName(existingBuildFolder);

		if (overrideExeName.IsEmpty())
		{
			SetProgressText(FText::FromString(existingBuildFolder + " is not an existing build directory"));
			return;
		}

		if (!AddRunInfoJSONAndMetadataToBuild(existingBuildFolder, overrideProjectName, overrideExeName))
		{
			//Error creating runinfo json
			//success = false;
		}

		if (!PortalUploadAndZip(existingBuildFolder))
		{
			//Error uploading to Portal
			//success = false;
		}
		
	}
}

ECheckBoxState FZeroLightMainButton::UseExistingBuild() const
{
	return s_useExisting ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FZeroLightMainButton::UseRemoveDebugSymbols() const
{
	return s_removeDebugSymbols ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

ECheckBoxState FZeroLightMainButton::UseExistingRunInfo() const
{
	return s_useExistingRunInfo ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FSlateIcon FZeroLightMainButton::GetButtonIcon()
{
	return FSlateIcon("ZeroLightEditorStyle", "MainButton.ZeroLight");
}

TSharedRef<SWidget> FZeroLightMainButton::GetMenu()
{
	FMenuBuilder MenuBuilder(true, nullptr);

	MenuBuilder.AddSubMenu(
		FText::FromString("Tools"),
		FText::FromString("Advanced Utilities"),
		FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenuBuilder) {
		SubMenuBuilder.AddMenuEntry(
			FText::FromString("Log State JSON Schema"),
			FText::FromString("Prints schema of state processed using OmniStream State Manager"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FZeroLightMainButton::ValidateJSONSchema_Async)));

		SubMenuBuilder.AddMenuEntry(
			FText::FromString("State Management Editor"),
			FText::FromString("Utility for defining and managing application state JSON"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FZeroLightMainButton::ShowStateManagementWindow))
		);
	}));

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		FText::FromString("Settings"),
		FText::FromString("Settings for ZeroLight OmniStream"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &FZeroLightMainButton::ShowPluginSettingsWindow))
	);

	MenuBuilder.AddMenuEntry(
		FText::FromString("Build and Upload"),
		FText::FromString("Build your content to be deployed on ZeroLight OmniStream"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &FZeroLightMainButton::ShowBuildAndDeployDialog))
	);

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
		FText::FromString("About"),
		FText::FromString("Display information about ZeroLight OmniStream"),
		FSlateIcon(),
		FUIAction(FExecuteAction::CreateSP(this, &FZeroLightMainButton::About))
	);

	return MenuBuilder.MakeWidget();
}

void FZeroLightMainButton::SetDeployName(const FText& name)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->deployName = name.ToString();
		FApp::SetProjectName(*name.ToString());
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}

void FZeroLightMainButton::SetPortalDisplayName(const FString& name)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->displayName = name;
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}

void FZeroLightMainButton::SetBuildMetadata(const FString& metadata)
{
	s_buildMetadataStr = metadata;
}

void FZeroLightMainButton::SetDeployName(const FString& name)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->deployName = name;
		//FApp::SetProjectName(*name);
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}

bool FZeroLightMainButton::NewerGithubPluginAvailable()
{
	return newerPluginAvailable;
}

void FZeroLightMainButton::TogglePluginEnabled(const FString& PluginName, bool bEnable)
{
	IPluginManager& pluginManager = IPluginManager::Get();

	TSharedPtr<IPlugin> plugin = pluginManager.FindPlugin(PluginName);
	if (plugin.IsValid())
	{
		if (plugin->IsEnabled() == bEnable)
		{
			return;
		}

		const FProjectDescriptor* projectDescriptor = IProjectManager::Get().GetCurrentProject();
		if (projectDescriptor)
		{
			TArray<FPluginReferenceDescriptor> modifiedPluginReferences = projectDescriptor->Plugins;
			bool bFound = false;
			for (int i = 0; i < modifiedPluginReferences.Num(); i++)
			{
				if (modifiedPluginReferences[i].Name == PluginName)
				{
					modifiedPluginReferences[i].bEnabled = bEnable;
					bFound = true;
					break;
				}
			}

			if (!bFound)
			{
				FPluginReferenceDescriptor newPluginRef(PluginName, bEnable);
				modifiedPluginReferences.Add(newPluginRef);
			}

			FProjectDescriptorModifier ModifyProject = FProjectDescriptorModifier::CreateLambda(
			[modifiedPluginReferences](FProjectDescriptor& Descriptor) { Descriptor.Plugins = modifiedPluginReferences; return true; });

			const FString& ProjectFilename = FPaths::GetProjectFilePath();
			GameProjectUtils::TryMakeProjectFileWriteable(ProjectFilename);

			FText outFail;
			FProjectDescriptor Descriptor;
			if (Descriptor.Load(ProjectFilename, outFail))
			{
				ModifyProject.Execute(Descriptor);
				
				Descriptor.Save(ProjectFilename, outFail);

				return;
			}
		}
	}
}

FReply FZeroLightMainButton::AutoResolveProjectWarnings()
{
	bool needsRestart = false;

	if (!IsCodeProject())
	{
		/*FString HeaderPath = FPaths::GameSourceDir() / TEXT("OmniStreamAutoGeneratedClass.h");
		FString SourcePath = FPaths::GameSourceDir() / TEXT("OmniStreamAutoGeneratedClass.cpp");

		FString HeaderContent = TEXT("#pragma once\n\nclass FOmniStreamAutoGeneratedClass\n{\npublic:\n\tFOmniStreamAutoGeneratedClass();\n};\n");
		FString SourceContent = TEXT("#include \"OmniStreamAutoGeneratedClass.h\"\n\nFOmniStreamAutoGeneratedClass::FOmniStreamAutoGeneratedClass()\n{\n}\n");

		FFileHelper::SaveStringToFile(HeaderContent, *HeaderPath);
		FFileHelper::SaveStringToFile(SourceContent, *SourcePath);

		FText outFailReason, outFailLog;
		FGameProjectGenerationModule::Get().UpdateCodeProject(outFailReason, outFailLog);
		needsRestart = true;*/

		FGameProjectGenerationModule::Get().OpenAddCodeToProjectDialog(
			FAddToProjectConfig().DefaultClassName("OmniStreamAutoGeneratedClass")
		);
	}

	if (IsPixelStreamingEnabled())
	{
		TogglePluginEnabled(TEXT("PixelStreaming"), false);
		needsRestart = true;
	}

	if (!IsPixelCaptureEnabled())
	{
		TogglePluginEnabled(TEXT("PixelCapture"), true);
		needsRestart = true;
	}

	if (NeedsOpenXREnabling())
	{
		TogglePluginEnabled(TEXT("OpenXR"), true);
		needsRestart = true;
	}

	if (needsRestart)
	{
		FText Title = FText::FromString("Restart Required");
		FText Message = FText::FromString("Resolving OmniStream warnings require a restart. Please restart the editor for the changes to take effect.");
		FMessageDialog::Open(EAppMsgType::Ok, Message, &Title);
	}

	return FReply::Handled();
}

void FZeroLightMainButton::SetPortalAssetLineID(const FText& id)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->portalAssetLineId = id.ToString();
		s_cloudstreamSettings->buildId = id.ToString();
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}


void FZeroLightMainButton::SetBuildID(const FText& id)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->buildId = id.ToString();
		
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}

void FZeroLightMainButton::SetBuildFolder(const FString& path)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_cloudstreamSettings->buildFolder = path;
		s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
	}
}

FString FZeroLightMainButton::GetBuildFolder()
{
	if (s_cloudstreamSettings != nullptr)
	{
		return s_cloudstreamSettings->buildFolder;
	}
	return "";
}

void FZeroLightMainButton::SetUseExistingBuild(const ECheckBoxState state)
{
	s_useExisting = (state == ECheckBoxState::Checked) ? true : false;

	if (IsInSlateThread())
		FSlateApplication::Get().ForceRedrawWindow(BuildAndDeployWnd.ToSharedRef());
}

void FZeroLightMainButton::SetRemoveDebugSymbols(const ECheckBoxState state)
{
	s_removeDebugSymbols = (state == ECheckBoxState::Checked) ? true : false;

	if (IsInSlateThread())
		FSlateApplication::Get().ForceRedrawWindow(BuildAndDeployWnd.ToSharedRef());
}

void FZeroLightMainButton::SetUseExistingRunInfoJson(const ECheckBoxState state)
{
	if (s_cloudstreamSettings != nullptr)
	{
		s_useExistingRunInfo = (state == ECheckBoxState::Checked) ? true : false;

		if (IsInSlateThread())
			FSlateApplication::Get().ForceRedrawWindow(BuildAndDeployWnd.ToSharedRef());
	}
}

void FZeroLightMainButton::SetUsePortalUpload(const ECheckBoxState state)
{
	if (portalCLI != nullptr)
	{
		SetDeployName("application");

		SetBuildID(FText::FromString(GetPortalAssetLineID()));

		if (IsInSlateThread())
			FSlateApplication::Get().ForceRedrawWindow(BuildAndDeployWnd.ToSharedRef());
	}
}

void FZeroLightMainButton::SetPortalStorageProvider(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo)
{
	if (NewValue.IsValid())
	{
		s_portalStorageProvider = FString(*NewValue);

		FSlateApplication::Get().ForceRedrawWindow(BuildAndDeployWnd.ToSharedRef());
	}
}



void FZeroLightMainButton::SetIsBuilding(bool state)
{
	s_isCurrentlyBuilding = state;
}

void FZeroLightMainButton::SetIsPortalAuthorised(bool state)
{
	s_isPortalAuthorised = state;
}

void FZeroLightMainButton::SetPortalUsername(FString username)
{
	s_portalUsername = username;
}

static FString GetCurrentTimestamp()
{
	time_t rawtime;
	struct tm timeinfo;
	char buffer[90];

	time(&rawtime);
	localtime_s(&timeinfo, &rawtime);

	strftime(buffer, sizeof(buffer), "%H:%M:%S ", &timeinfo);
	std::string timestamp(buffer);
	return FString(timestamp.c_str());
}

void FZeroLightMainButton::SetProgressText(const FText& text)
{
	if (text.IsEmpty())
	{
		s_progressText = FText::FromString("");
		return;
	}

	FText ts = FText::FromString(GetCurrentTimestamp());
	
	TArray<FText> log {ts, text};

	std::string tempString = TCHAR_TO_UTF8(&text.ToString());

	s_progressText = FText::Join(FText::FromString(""), log);

	if(s_isCIBuild)
		UE_LOG(LogPortalCLI, Warning, TEXT("OmniStream Build & Upload CI - %s"), *text.ToString());
}

FReply FZeroLightMainButton::TriggerBuildAndDeploy(FString buildFolderOverride)
{
	bool success = true;

	FString engineDir = FPaths::EngineDir();
	FString projectPath = FPaths::GetProjectFilePath();
	FString projectDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath()));

	SetRetryFailedRenameBuildPath("");

	OutputMessage assetLineNameResponse = portalCLI->m_client->client_get_asset_line(TCHAR_TO_ANSI(*s_cloudstreamSettings->portalAssetLineId));

	FString assetLineJson = FFIString(portal::output_message_to_json(assetLineNameResponse.m_output_message)).ToFString();

	TSharedRef<TJsonReader<TCHAR>> assetLineJsonReader = TJsonReaderFactory<TCHAR>::Create(assetLineJson);
	TSharedPtr<FJsonObject> assetLineJsonObj;
	if (FJsonSerializer::Deserialize(assetLineJsonReader, assetLineJsonObj) && assetLineJsonObj.IsValid())
	{
		if (assetLineJsonObj->TryGetField(FString("data")) != nullptr)
		{
			FString displayName = assetLineJsonObj->GetObjectField(FString("data"))->GetStringField(FString("displayName"));
			FZeroLightMainButton::SetPortalDisplayName(displayName);

			if (s_isCIBuild)
			{
				UE_LOG(LogPortalCLI, Warning, TEXT("OmniStream Build & Upload CI - Asset Line Display Name %s"), *displayName);
			}
			else
			{
				UE_LOG(LogPortalCLI, Log, TEXT("Asset Line ID %s"), *s_cloudstreamSettings->portalAssetLineId);
			}

			
		}
		else
		{
			SetProgressText(FText::FromString("Asset Line ID " + s_cloudstreamSettings->portalAssetLineId + " not found"));
			assetLineNameResponse.print();
			return FReply::Unhandled();
		}
	}
	else
	{
		SetProgressText(FText::FromString("Asset Line ID " + s_cloudstreamSettings->portalAssetLineId + " not found"));
		assetLineNameResponse.print();
		return FReply::Unhandled();
	}

	if (s_useExisting)
	{
		FString existingBuildFolder("");

		if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), TEXT("Locate existing build..."), projectDir, existingBuildFolder))
		{
			success = false;
		}
		else
		{
			portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::GETTOKEN, false, true, existingBuildFolder);	
		}
	}
	else
	{
		FString runUATPath = engineDir + "Build/BatchFiles/RunUAT.bat";

		FString outFolderName("");

		if (FPaths::FileExists(runUATPath))
		{
			if(buildFolderOverride != "")
			{
				buildFolderOverride = buildFolderOverride.TrimQuotes();
			}

			if (buildFolderOverride == "")
			{
				SetBuildFolder(projectDir);

				if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), TEXT("Package project..."), projectDir, outFolderName))
				{
					success = false;
					return FReply::Unhandled();
				}
				else
					SetBuildFolder(outFolderName);
			}
			else
			{
				outFolderName = buildFolderOverride;
			}
	
			UE_LOG(LogPortalCLI, Display, TEXT("CloudStream2 : Freeing plugin for build process"));
			ZLCloudPlugin::CloudStream2::FreePlugin();

			ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
			if (LiveCoding != nullptr && LiveCoding->IsEnabledForSession())
			{
				LiveCoding->EnableForSession(false);
			}

			portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::GETTOKEN, true, false, outFolderName);

			success = true;
		}

	}

	if (success)	
		return FReply::Handled();
	else
	{
		UE_LOG(LogPortalCLI, Error, TEXT("OmniStream Build & Upload CI - Automated build process encountered errors, check log for further details"));
		return FReply::Unhandled();
	}
}

bool FZeroLightMainButton::OnBuildPackageSuccess() const
{
	s_triggerBuildUpload = true;
	
	return true;
}

bool FZeroLightMainButton::RestoreToBuildDirectory()
{
	std::filesystem::path origPathStd = TCHAR_TO_UTF8(*s_restoreOrigPath);
	std::filesystem::path restorePathStd = TCHAR_TO_UTF8(*s_restorePath);
	std::filesystem::path pathToCleanupStd = TCHAR_TO_UTF8(*s_restorePathToCleanup);
	//Attempt 5 times because unreal sometimes holds on to packaged build folder a while adding FileOpenOrder metadata
	int attempts = 0;
	const int max_attempts = 5;
	bool renameSuccess = false;

	while (attempts < max_attempts && !renameSuccess) {
		try {
			std::filesystem::rename(origPathStd, restorePathStd);
			renameSuccess = true;
		}
		catch (const std::filesystem::filesystem_error& e) {
			UE_LOG(LogPortalCLI, Log, TEXT("Restore OmniStream packaged build to original build directory attempt %d failed: %s"), (attempts + 1), *FString(e.what()));
			attempts++;
			FPlatformProcess::Sleep(1.5f);
		}
	}

	if (!renameSuccess)
	{
		SetProgressText(FText::FromString(s_restoreOrigPath + " could not be restored to original build directory - Close folder and retry"));
		SetIsBuilding(false);
		return false;
	}
	else
	{
		attempts = 0;
		bool cleanupSuccess = false;
		while (attempts < max_attempts && !cleanupSuccess) {
			try {
				std::filesystem::remove_all(pathToCleanupStd);
				cleanupSuccess = true;
			}
			catch (const std::filesystem::filesystem_error& e) {
				UE_LOG(LogPortalCLI, Log, TEXT("Remove empty OmniStream packaged build folder structure attempt %d failed: %s"), (attempts + 1), *FString(e.what()));
				attempts++;
				FPlatformProcess::Sleep(1.5f);
			}
		}

		if (!cleanupSuccess)
		{
			//do we need to log error here? structure is empty at this stage so wont be an issue on a reupload anyway
			//UE_LOG(LogPortalCLI, Log, TEXT("Could not remove empty OmniStream packaged build directories"));
		}
	}

	return true;
}

FString FZeroLightMainButton::FindOverrideExeName(FString buildDirectory) const
{
	FString exeStr = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString overrideExeName;

	PlatformFile.IterateDirectory(*buildDirectory, [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory)
		{
			FString SubdirName = FPaths::GetCleanFilename(FilenameOrDirectory);
			FString SubdirPath = FPaths::Combine(buildDirectory, SubdirName);

			FString BinariesPath = FPaths::Combine(SubdirPath, TEXT("Binaries"));

			if (BinariesPath.Contains(TEXT("Engine/Binaries")))
			{
				return true;
			}

			if (PlatformFile.DirectoryExists(*BinariesPath))
			{
				overrideExeName = SubdirName;

				PlatformFile.IterateDirectoryRecursively(*BinariesPath, [&](const TCHAR* FileOrDir, bool bIsDirectory)
				{
					FString FullPath(FileOrDir);
					if (!bIsDirectory && FullPath.Contains("Binaries/Win64")) //only win builds but here to avoid any ThirdParty exe being caught
					{
						FString FileName = FPaths::GetCleanFilename(FileOrDir);
						if (FileName.EndsWith(TEXT(".exe")))
						{
							overrideExeName = FPaths::GetBaseFilename(FileName);
							return false;
						}
					}
					return true;
				});

				return false; 
			}
		}
		return true;
	});

	UE_LOG(LogPortalCLI, Log, TEXT("Overriding expected exe name %s with found exe name %s"), *exeStr, *overrideExeName);
	return overrideExeName;
}

FString FZeroLightMainButton::FindOverrideProjectName(FString buildDirectory) const
{
	FString exeStr = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	FString overrideExeName;

	PlatformFile.IterateDirectory(*buildDirectory, [&](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
	{
		if (bIsDirectory)
		{
			FString SubdirName = FPaths::GetCleanFilename(FilenameOrDirectory);
			FString SubdirPath = FPaths::Combine(buildDirectory, SubdirName);

			FString BinariesPath = FPaths::Combine(SubdirPath, TEXT("Binaries"));

			if (BinariesPath.Contains(TEXT("Engine/Binaries")))
			{
				return true;
			}

			if (PlatformFile.DirectoryExists(*BinariesPath))
			{
				overrideExeName = SubdirName;

				return false;
			}
		}
		return true;
	});

	UE_LOG(LogPortalCLI, Log, TEXT("Overriding expected project name %s with found exe name %s"), *exeStr, *overrideExeName);
	return overrideExeName;
}

bool FZeroLightMainButton::TriggerBuildUpload(FString retryPath) const
{
	//if we include more platform support this needs to pull from platform settings
	FString origPath(s_cloudstreamSettings->buildFolder + "/Windows");

	if (!retryPath.IsEmpty())
		origPath = retryPath;

	if (FPaths::DirectoryExists(origPath))
	{
		FString noSpaceNameStr(s_cloudstreamSettings->deployName); //not needed, but cleaner handling on many version control apps
		noSpaceNameStr.RemoveSpacesInline();

		FString replacementFolder(s_cloudstreamSettings->buildFolder + "/" + noSpaceNameStr);

		std::filesystem::path origPathStd = TCHAR_TO_UTF8(*origPath);

		//Using std filesystem for simplicity here because AssetViewUtils asserts that its called on game thread
		bool replacementFolderExists = FPaths::DirectoryExists(replacementFolder);
		int dupId = 0;

		while (replacementFolderExists)
		{
			dupId++;
			FString dupIDExt = FString::Printf(TEXT("(%i)"), dupId);
			if (!FPaths::DirectoryExists(replacementFolder + dupIDExt))
			{
				replacementFolder = replacementFolder + dupIDExt;
				replacementFolderExists = false;
			}
		}

		std::filesystem::path replacePathStd = TCHAR_TO_UTF8(*replacementFolder);

		//Attempt 3 times because unreal sometimes holds on to packaged build folder a while adding FileOpenOrder metadata
		int attempts = 0;
		const int max_attempts = 3;
		bool renameSuccess = false;

		while (attempts < max_attempts && !renameSuccess) {
			try {
				std::filesystem::rename(origPathStd, replacePathStd);
				renameSuccess = true;
			}
			catch (const std::filesystem::filesystem_error& e) {
				UE_LOG(LogPortalCLI, Log, TEXT("Move packaged build attempt %d failed: %s"), (attempts + 1), *FString(e.what()));
				attempts++;
				FPlatformProcess::Sleep(1.0f);
			}
		}

		if (!renameSuccess)
		{
			SetProgressText(FText::FromString(origPath + " could not be moved - Close folder and retry"));
			SetIsBuilding(false);
			SetRetryFailedRenameBuildPath(s_cloudstreamSettings->buildFolder + "/Windows");
			return false;
		}

		FString thumbnailImageFilePath = GetThumbnailFilePath();
		std::filesystem::path thumbnailImageFilePathStd = TCHAR_TO_UTF8(*thumbnailImageFilePath);
		if (FPaths::FileExists(thumbnailImageFilePath))
		{
			attempts = 0;
			bool copyThumbnailFileSuccess = false;
			FString copyThumbnailFilePathDest = replacementFolder + "/" + "appThumbnail" + FPaths::GetExtension(thumbnailImageFilePath, true);
			std::filesystem::path copyThumbnailFilePathDestStd = TCHAR_TO_UTF8(*copyThumbnailFilePathDest);

			while (attempts < max_attempts && !copyThumbnailFileSuccess) {
				try {
					std::filesystem::copy_file(thumbnailImageFilePathStd, copyThumbnailFilePathDestStd, std::filesystem::copy_options::overwrite_existing);
					copyThumbnailFileSuccess = true;
				}
				catch (const std::filesystem::filesystem_error& e) {
					UE_LOG(LogPortalCLI, Log, TEXT("Move thumbnail image file to packaged build attempt %d failed: %s"), (attempts + 1), *FString(e.what()));
					attempts++;
					FPlatformProcess::Sleep(1.0f);
				}
			}
		}
		
		FString exeStr = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
		FString exeName = exeStr + "-Win64-Shipping.exe"; //Release exe name
		FString altExeName = exeStr + ".exe";
		FString defaultExeName = "UnrealGame.exe";

		FString cppProjectExePath = replacementFolder + "/" + exeStr + "/Binaries/" + BUILDPLATFORM + "/";
		FString bpProjectExePath = replacementFolder + "/Engine/Binaries/" + BUILDPLATFORM + "/";

		if (FPaths::FileExists(cppProjectExePath + exeName) ||
			FPaths::FileExists(cppProjectExePath + altExeName) ||
			FPaths::FileExists(cppProjectExePath + defaultExeName) ||
			FPaths::FileExists(bpProjectExePath + exeName) ||
			FPaths::FileExists(bpProjectExePath + altExeName) ||
			FPaths::FileExists(bpProjectExePath + defaultExeName))
		{
			/*if (!AddCloudPluginConfigToBuild(replacementFolder))
			{
				return false;
			}*/

			if (!AddRunInfoJSONAndMetadataToBuild(replacementFolder, ""))
			{
				//Error creating runinfo json
				return false;
			}

			if (!PortalUploadAndZip(replacementFolder))
			{
				//Error uploading to Portal
				return false;
			}
		}
		else
		{
			FString overrideExeName = FindOverrideExeName(replacementFolder);
			FString overrideProjectName = FindOverrideProjectName(replacementFolder);

			if (!AddRunInfoJSONAndMetadataToBuild(replacementFolder, overrideProjectName, overrideExeName))
			{
				//Error creating runinfo json
				return false;
			}

			if (!PortalUploadAndZip(replacementFolder))
			{
				//Error uploading to Portal
				return false;
			}
		}
	}
	else
	{
		UE_LOG(LogPortalCLI, Log, TEXT("Build output not found at %s"), *origPath);
		SetIsBuilding(false);
		return false;
	}
	return true;
}

bool FZeroLightMainButton::RetryRenameUploadExistingBuild() const
{
	FString retryPath = s_retryRenameBuildPath;
	s_retryRenameBuildPath = "";

	return TriggerBuildUpload(retryPath);
}

TSharedPtr<FJsonObject> FZeroLightMainButton::AggregatePredictedJSONSchema() const
{
	TSharedPtr<FJsonObject> aggregatedJsonObject = MakeShared<FJsonObject>();
	static const TArray<FName> functionsToSearch = { "GetRequestedStateValue", "GetRequestedStateValueString", "GetRequestedStateValueBool", "GetRequestedStateValueNumber",
													 "GetRequestedStateValueStringArray", "GetRequestedStateValueBoolArray", "GetRequestedStateValueNumberArray" };
	TArray<FString> foundArguments;

	// Get all blueprints in the project
	FAssetRegistryModule& assetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	TArray<FAssetData> assetData;
	FARFilter filter;
	filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	filter.bRecursivePaths = true;
	assetRegistryModule.Get().GetAssets(filter, assetData);

	for (const FAssetData& asset : assetData)
	{
		UBlueprint* blueprint = Cast<UBlueprint>(asset.GetAsset());
		if (blueprint)
		{
			for (UEdGraph* graph : blueprint->UbergraphPages)
			{
				for (UEdGraphNode* node : graph->Nodes)
				{
					if (UK2Node_CallFunction* functionNode = Cast<UK2Node_CallFunction>(node))
					{
						for (FName function : functionsToSearch)
						{
							if (function.IsEqual(functionNode->FunctionReference.GetMemberName()))
							{
								UEdGraphPin* keyStringPin = functionNode->FindPin(TEXT("FieldName"));
								if (keyStringPin && keyStringPin->DefaultValue.Len() > 0)
								{
									FString functionNameStr = functionNode->FunctionReference.GetMemberName().ToString();
									if (functionNameStr.Equals("GetRequestedStateValue")) //legacy original func was only string, handle this
										functionNameStr = "GetRequestedStateValueString";
									FString leftPart, returnValueStr;

									functionNameStr.Split(TEXT("GetRequestedStateValue"), &leftPart, &returnValueStr);

									TArray<FString> keys;
									keyStringPin->DefaultValue.ParseIntoArray(keys, TEXT("."));

									TSharedPtr<FJsonObject> CurrentObject = aggregatedJsonObject;
									for (int32 i = 0; i < keys.Num(); ++i)
									{
										if (i == keys.Num() - 1)
										{
											CurrentObject->SetStringField(keys[i], returnValueStr);
										}
										else
										{
											TSharedPtr<FJsonObject> SubObject;
											if (CurrentObject->HasField(keys[i]))
											{
												SubObject = CurrentObject->GetObjectField(keys[i]);
											}
											else
											{
												SubObject = MakeShared<FJsonObject>();
												CurrentObject->SetObjectField(keys[i], SubObject);
											}
											CurrentObject = SubObject;
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

	return aggregatedJsonObject;
}

bool FZeroLightMainButton::AddRunInfoJSONAndMetadataToBuild(FString outputFolderPath, FString overrideProjectName, FString overrideExeName) const
{
	FString projectStr = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
	if (!overrideProjectName.IsEmpty())
		projectStr = overrideProjectName;

	FString exeName = projectStr + "-Win64-Shipping.exe"; //Release exe name
	FString defaultExeName = "UnrealGame.exe";

	FString cppProjectExePath = outputFolderPath + "/" + projectStr + "/Binaries/" + BUILDPLATFORM + "/";

	FString bpProjectExePath = outputFolderPath + "/";

	TSharedPtr<FJsonObject> runInfoJsonObj = MakeShareable(new FJsonObject);

	TSharedPtr<FJsonObject> executablesJSONObj = MakeShareable(new FJsonObject);
	TSharedPtr<FJsonObject> runModesJSONObj = MakeShareable(new FJsonObject);

	executablesJSONObj->SetStringField("name", s_cloudstreamSettings->deployName);
	executablesJSONObj->SetStringField("assetLineDisplayName", s_cloudstreamSettings->displayName);
	executablesJSONObj->SetStringField("buildId", s_cloudstreamSettings->buildId);
	executablesJSONObj->SetStringField("project", s_projectName.ToLower());

	if (FPaths::DirectoryExists(cppProjectExePath))
	{
		if (overrideExeName.IsEmpty())
		{
			if (FPaths::FileExists(cppProjectExePath + exeName))
				executablesJSONObj->SetStringField("filename", exeName);
			else if (FPaths::FileExists(cppProjectExePath + defaultExeName))
				executablesJSONObj->SetStringField("filename", defaultExeName); //Handle debug/development builds
			else
				executablesJSONObj->SetStringField("filename", projectStr + ".exe"); //Handle debug/development builds
		}
		else
		{
			executablesJSONObj->SetStringField("filename", overrideExeName + ".exe"); //Handle debug/development builds
		}

		executablesJSONObj->SetStringField("folder", projectStr + "/Binaries/Win64");
	}
	else
	{
		if (overrideExeName.IsEmpty())
		{
			if (FPaths::FileExists(bpProjectExePath + exeName))
				executablesJSONObj->SetStringField("filename", exeName);
			else if (FPaths::FileExists(bpProjectExePath + defaultExeName))
				executablesJSONObj->SetStringField("filename", defaultExeName); //Handle debug/development builds
			else
				executablesJSONObj->SetStringField("filename", projectStr + ".exe"); //Handle debug/development builds
		}
		else
		{
			executablesJSONObj->SetStringField("filename", overrideExeName + ".exe"); //Handle debug/development builds
		}

		executablesJSONObj->SetStringField("folder", "");
	}

	executablesJSONObj->SetBoolField("monitorHeartbeat", true);

	executablesJSONObj->SetStringField("logPath", projectStr + "/Saved/Logs/unreal_log.txt");

	FString cloudArgs = (s_cloudstreamSettings->bDisableTextureStreamingOnLaunch) ? "-WINDOWED -ResX=1270 -ResY=720 -NOTEXTURESTREAMING -Unattended -NoVSync -LOG=unreal_log.txt" : "-WINDOWED -ResX=1270 -ResY=720 -Unattended -NoVSync -LOG=unreal_log.txt";
	FString cloudXRArgs = (s_cloudstreamSettings->bDisableTextureStreamingOnLaunch) ? "-WINDOWED -VR -cloudxr -ResX=1270 -ResY=720 -NOTEXTURESTREAMING -Unattended -NoVSync -LOG=unreal_log.txt" : "-WINDOWED -VR -cloudxr -ResX=1270 -ResY=720 -Unattended -NoVSync -LOG=unreal_log.txt";
	AddRunMode("Cloud", cloudArgs, runModesJSONObj);
	AddRunMode("CloudXR", cloudXRArgs, runModesJSONObj, "xr");

	TArray<TSharedPtr<FJsonValue>> executablesJSONArray;
	executablesJSONArray.Add(MakeShareable(new FJsonValueObject(executablesJSONObj)));

	runInfoJsonObj->SetStringField("handler", "server");
	runInfoJsonObj->SetBoolField("autorun", true);
	runInfoJsonObj->SetStringField("defaultRunMode", (s_cloudstreamSettings->supportsVR) ? "CloudXR" : "Cloud");
	runInfoJsonObj->SetStringField("streamingType", "unreal");
	runInfoJsonObj->SetBoolField("rebootOnDisconnect", s_cloudstreamSettings->bRebootAppOnDisconnect);
	runInfoJsonObj->SetNumberField("stateWarningTime", s_cloudstreamSettings->stateRequestWarningTime);
	runInfoJsonObj->SetNumberField("stateTimeout", s_cloudstreamSettings->stateRequestTimeout);
	runInfoJsonObj->SetBoolField("defaultConnectStateSupported", true);

	runInfoJsonObj->SetArrayField("executables", executablesJSONArray);
	runInfoJsonObj->SetObjectField("runModes", runModesJSONObj);

	//Metadata
	{
		//Produce metadata for Portal
		TSharedPtr<FJsonObject> metadataJsonObj = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> versioningJSONObj = MakeShareable(new FJsonObject);
		TSharedPtr<FJsonObject> appCapabilitiesJSONObj = MakeShareable(new FJsonObject);

		//Gather metadata flags/caps
		metadataJsonObj->SetStringField("type", "unreal");

		//Misc metadata
		{
			metadataJsonObj->SetStringField("portal_display_name", s_cloudstreamSettings->displayName);
		}

		//Versioning metadata
		{
			versioningJSONObj->SetStringField("unreal", ENGINE_VERSION_STRING);

			IPluginManager& PluginManager = IPluginManager::Get();
			TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
			if (ZlCloudStreamPlugin)
			{
				const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
				versioningJSONObj->SetStringField("omnistream", *Descriptor.VersionName);
			}

			int cs2_major = 0;
			int cs2_minor = 0;
			int cs2_rev = 0;
			int cs2_build = 0;

			bool freeCS2AfterPluginVerCheck = false;
			if (!ZLCloudPlugin::CloudStream2::IsPluginInitialised())
			{
				ZLCloudPlugin::CloudStream2::InitPlugin();
				if(ZLCloudPlugin::CloudStream2::IsPluginInitialised())
					freeCS2AfterPluginVerCheck = true;
			}

			if (ZLCloudPlugin::CloudStream2::IsPluginInitialised())
			{
				CloudStream2DLL::GetPluginVersion(cs2_major, cs2_minor, cs2_rev, cs2_build);
				versioningJSONObj->SetStringField("cloudstream2", FString::Printf(TEXT("%d.%d.%d.%d"), cs2_major, cs2_minor, cs2_rev, cs2_build));
			}
			if (freeCS2AfterPluginVerCheck)
				ZLCloudPlugin::CloudStream2::FreePlugin();

			FString portalVer = FString(portal::get_library_version().c_str);
			versioningJSONObj->SetStringField("portalclient", portalVer);

			metadataJsonObj->SetObjectField("version", versioningJSONObj);
		}

		//App capabilities/capability version metadata
		{
			if (s_cloudstreamSettings->supportsVR)
			{
				appCapabilitiesJSONObj->SetStringField("VR", XR_CAPS_VERSION_STRING);
			}

			//add SpotLite and feature caps here if present

			metadataJsonObj->SetObjectField("app_caps", appCapabilitiesJSONObj);
		}

		//Dependencies metadata
		{
			TArray<TSharedPtr<FJsonValue>> dependenciesJson;

			if (s_cloudstreamSettings->supportsVR)
			{
				TSharedPtr<FJsonObject> xrDepsJsonObject = MakeShareable(new FJsonObject);

				xrDepsJsonObject->SetStringField("type", "xr");
				xrDepsJsonObject->SetStringField("version", XR_DEPS_VERSION_STRING);

				dependenciesJson.Add(MakeShareable(new FJsonValueObject(xrDepsJsonObject)));
			}

			metadataJsonObj->SetArrayField("dependencies", dependenciesJson);
		}

		//Estimate JSON Schema 
		{
			TSharedPtr<FJsonObject> aggregatedJsonSchema = AggregatePredictedJSONSchema();

			metadataJsonObj->SetObjectField("state_schema", aggregatedJsonSchema);
		}

		FString metadataJsonStr;

		TSharedRef<TJsonWriter<>> metadataWriter = TJsonWriterFactory<>::Create(&metadataJsonStr);


		if (!FJsonSerializer::Serialize(metadataJsonObj.ToSharedRef(), metadataWriter))
		{
			SetProgressText(FText::FromString("Error serializing build metadata to JSON string"));
			return false;
		}
		else
			s_buildMetadataStr = metadataJsonStr;

		runInfoJsonObj->SetObjectField("metadata", metadataJsonObj);
	}

	FString jsonStr;

	TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&jsonStr);

	
	if (!FJsonSerializer::Serialize(runInfoJsonObj.ToSharedRef(), writer))
	{
		SetProgressText(FText::FromString("Error converting runinfo to JSON string"));
		return false;
	}

	FString jsonFilePath(outputFolderPath + "/run_info.json");

	if (FPaths::FileExists(jsonFilePath)) //Clean up existing
	{
		IFileManager::Get().Delete(*jsonFilePath);
	}

	if (!FFileHelper::SaveStringToFile(jsonStr, *jsonFilePath))
	{
		SetProgressText(FText::FromString("Error saving json file"));
		return false;
	}

	return true;
}

bool FZeroLightMainButton::AddCloudPluginConfigToBuild(FString outputFolderPath) const
{
	return true; //Not needed but kept for reference, see FAQ suggests including +AllowedConfigFiles=TestProject/Config/DefaultZLCloudPluginSettings.ini to the [Staging] section of DefaultGame.ini

	//FString configDir = FPaths::ProjectConfigDir();
	//FString cloudPluginConfigFilePath = configDir + "/" + "DefaultZLCloudPluginSettings.ini";

	//FString projectFolder = FPaths::GetBaseFilename(FPaths::GetProjectFilePath());
	//FString destFolder = outputFolderPath + "/" + projectFolder + "/Config";
	//FString destFilePath = outputFolderPath + "/" + projectFolder + "/Config/" + "DefaultZLCloudPluginSettings.ini";

	//UE_LOG(LogPortalDllCLI, Log, TEXT("Copying config file %s to %s"), *cloudPluginConfigFilePath, *destFilePath);
	//
	//IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();

	//try
	//{
	//	// Directory Exists?
	//	if (!platformFile.DirectoryExists(*destFolder))
	//	{
	//		platformFile.CreateDirectory(*destFolder);
	//	}

	//	platformFile.CopyFile(*destFilePath, *cloudPluginConfigFilePath);
	//}
	//catch (const std::exception& e)
	//{
	//	UE_LOG(LogPortalDllCLI, Error, TEXT("Error copying config file - %s"), *e.what());
	//}	

	//return true;
}

bool FZeroLightMainButton::AddRunMode(FString runMode, FString args, TSharedPtr<FJsonObject> jsonObj, FString streamingSwitch) const
{
	TSharedPtr<FJsonObject> cloudModesJSONObj = MakeShareable(new FJsonObject);

	cloudModesJSONObj->SetStringField("exe", s_projectName.ToLower() + "." + s_cloudstreamSettings->deployName + "." + s_cloudstreamSettings->buildId);

	if (!streamingSwitch.IsEmpty())
		cloudModesJSONObj->SetStringField("streamingSwitch", streamingSwitch);

	FString commandArgs = args;

	cloudModesJSONObj->SetStringField("args", commandArgs);

	jsonObj->SetObjectField(runMode, cloudModesJSONObj);

	return true;
}

bool FZeroLightMainButton::PortalUploadAndZip(FString outputFolderPath) const
{
	s_uploadDirectory = outputFolderPath;
	portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::UPLOAD);

	return true;
}

void FZeroLightMainButton::ShowPluginSettingsWindow()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		SettingsModule->ShowViewer("Project", "Plugins", "ZLCloudPluginSettings");
	}
}

FReply FZeroLightMainButton::CI_TriggerBuildAndDeploy(FString buildPath, bool portalUpload)
{
	s_useExisting = false;
	s_isCIBuild = true;

	return TriggerBuildAndDeploy(buildPath);
}

bool FZeroLightMainButton::CI_HasValidBuildSettingsAndAuthorised(bool portalUpload)
{
	portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::GETTOKEN);

	int authTimeoutS = 30;
	int elapsed = 0;
	while (s_getTokenAsyncTask != nullptr)
	{
		//timeout here 30s, no cached auth token on build machine was valid/found
		FPlatformProcess::Sleep(1);
		elapsed += 1;

		if (elapsed > authTimeoutS)
		{
			AbortCurrentProcessOrReauth();
			return false;
		}
	}

	return true;
}

FZeroLightMainButton::FZeroLightMainButton()
{
	// The name of the environment variable you want to check
	FString devFeaturesStr = TEXT("OMNISTREAM_DEV_FEATURES");

	// Initialize the value to store the environment variable's value
	FString val = FPlatformMisc::GetEnvironmentVariable(*devFeaturesStr);

	// Check if the environment variable exists and retrieve its value
	if (!val.IsEmpty())
	{
		s_devFeaturesEnabled = true;
		UE_LOG(LogPortalDllCLI, Log, TEXT("Development Features enabled by environment variable OMNISTREAM_DEV_FEATURES"));
	}

	if (s_cloudstreamSettings == nullptr)
		s_cloudstreamSettings = GetMutableDefault<UZLCloudPluginSettings>();

	portalCLI = MakeShareable<portalcli>(new portalcli());

	FZeroLightMainButton::SetUsePortalUpload(ECheckBoxState::Checked);

	s_CIMainButtonPtr = this;
	s_savedIniPath = FPaths::GeneratedConfigDir() + TEXT("WindowsEditor/ZLCloudPluginSettings.ini");
}

FZeroLightMainButton::~FZeroLightMainButton()
{
}

void FZeroLightMainButton::ShowStateManagementWindow()
{
	if (StateManagementWnd != nullptr)
	{
		StateManagementWnd->BringToFront();
		return;
	}

	StateManagementWnd = SNew(SWindow)
		.Title(FText::FromString("OmniStream: State JSON Editor"))
		.CreateTitleBar(true)
		.FocusWhenFirstShown(true)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.ClientSize(FVector2D(800,600))
		.MinHeight(500)
		.MinWidth(600)
		.SizingRule(ESizingRule::UserSized);

	StateManagementWnd->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&](const TSharedRef<SWindow>&)
	{
		this->ClearStateManagementWindowRef();
	}));

	StateManagementWnd->SetContent(SNew(FZLStateEditor));

	FSlateApplication::Get().AddWindow(StateManagementWnd.ToSharedRef(), true);
}

void FZeroLightMainButton::ShowBuildAndDeployDialog()
{
	if (BuildAndDeployWnd != nullptr)
	{
		BuildAndDeployWnd->BringToFront();
		return;
	}

	portalCLI->AsyncPortalTask((s_cloudstreamSettings->portalAssetLineId == "") ? portalcli::AsyncPortalTaskType::SELECTASSETLINE : portalcli::AsyncPortalTaskType::GETTOKEN);

	BuildAndDeployWnd = SNew(SWindow)
		.Title(FText::FromString("OmniStream: Build and Upload"))
		.CreateTitleBar(true)
		.FocusWhenFirstShown(true)
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.MinWidth(365)
		.MinHeight(300)
		.SizingRule(ESizingRule::Autosized);

	BuildAndDeployWnd->SetOnWindowClosed(FOnWindowClosed::CreateLambda([&](const TSharedRef<SWindow>&) 
	{ 
		portalCLI->StopOutputReadTask();

		if (s_getTokenAsyncTask != nullptr)
		{
			s_getTokenAsyncTask->Stop();
		}

		if (s_uploadAsyncTask != nullptr)
		{
			s_uploadAsyncTask->Stop();
		}

		this->ClearBuildWindowRef();
	}));

	BuildAndDeployWnd->SetContent(

	SNew(SHorizontalBox)
	+ SHorizontalBox::Slot().FillWidth(1.0f)
	.MaxWidth(365)
	.Padding(15, 4, 15, 5)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SExpandableArea)
				.InitiallyCollapsed(false)
				.Padding(FMargin(10, 5, 10, 10))
				.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BodyBorderBackgroundColor(FLinearColor::White)
				.HeaderContent()
				[
					SNew(STextBlock).Text(NSLOCTEXT("OmniStreamSettings", "Settings", "OmniStream Deployment"))
				]
				.BodyContent()
				[
					SNew(SGridPanel)
						+ SGridPanel::Slot(0, 0)
						.Padding(FMargin(5, 2, 2, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Left)
						[
							SNew(STextBlock).Text(FText::FromString("Name"))
						]
						+ SGridPanel::Slot(1, 0)
						.Padding(FMargin(5, 2, 2, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Right)
						[
							SNew(SEditableTextBox).Text_Lambda([this]() {
								return (s_cloudstreamSettings->displayName == "") ? FText::FromString(s_cloudstreamSettings->GetDeployName()) : FText::FromString(s_cloudstreamSettings->displayName);
							})
							
							.MinDesiredWidth(230)
							.OnTextChanged(FOnTextChanged::CreateSP(this, &FZeroLightMainButton::SetDeployName))
							.IsReadOnly(true)
						]
						+ SGridPanel::Slot(0, 1)
						.Padding(FMargin(5, 2, 2, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Left)
						[
							SNew(STextBlock).Text_Lambda([this]() { return FText::FromString("Portal ID"); })
						]
						+ SGridPanel::Slot(1, 1)
						.Padding(FMargin(5, 2, 2, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Right)
						[
							SNew(SEditableTextBox).Text_Lambda([this]() { return FText::FromString(s_cloudstreamSettings->portalAssetLineId); })
								.MinDesiredWidth(230)
								.OnTextChanged(FOnTextChanged::CreateSP(this, &FZeroLightMainButton::SetBuildID))
								.IsReadOnly(true)
						]
						+ SGridPanel::Slot(0, 2)
						.ColumnSpan(2)
						.Padding(FMargin(0, 5, 0, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Fill)
						[
							SNew(SButton)
								.Text_Lambda([this]() {
								if (s_cloudstreamSettings->portalAssetLineId == "")
									return FText::FromString("Pick Portal Asset");
								else
									return FText::FromString("Change Portal Asset");
							})
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.OnClicked(this, &FZeroLightMainButton::SelectAssetLine)
							.Visibility(EVisibility::Visible)
							.IsEnabled_Lambda([this]() {
							if (s_selectAssetLineAsyncTask == nullptr && !s_isCurrentlyBuilding && s_currentAssetVersionId.IsEmpty())
								return true;
							else
								return false;
							})
						]
						+ SGridPanel::Slot(0, 3)
						.ColumnSpan(2)
						.Padding(FMargin(0, 5, 0, 1))
						.VAlign(EVerticalAlignment::VAlign_Center)
						.HAlign(EHorizontalAlignment::HAlign_Fill)
						[
							SNew(SExpandableArea)
								.Visibility_Lambda([this]() {
									return EVisibility::Visible;
							})
								.InitiallyCollapsed(true)
								.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
								.BodyBorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
								.BodyBorderBackgroundColor(FLinearColor::White)
								.HeaderContent()
								[
									SNew(STextBlock)
										.Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Portal User Details"))
								]
								.BodyContent()
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight()
										.Padding(FMargin(5, 2, 0, 1))
										.VAlign(EVerticalAlignment::VAlign_Center)
										.HAlign(EHorizontalAlignment::HAlign_Left)
										[
											SNew(STextBlock)
												.AutoWrapText(false)
												.WrapTextAt(165.0f)
												.Text_Lambda([this]() {
												FText loggedInUser = FText::Format(LOCTEXT("UsernameLabel", "Username: {0}"), FText::FromString(s_portalUsername));
												return loggedInUser;
											})
										]
										+ SVerticalBox::Slot().AutoHeight()
										.Padding(FMargin(0, 2, 0, 1))
										.VAlign(EVerticalAlignment::VAlign_Center)
										.HAlign(EHorizontalAlignment::HAlign_Fill)
										[
											SNew(SButton)
												.Text_Lambda([this]() {
													if (IsPortalUserLoggedIn())
														return FText::FromString("        Log Out        ");
													else
														return FText::FromString("        Log In        ");
												})
												.IsEnabled_Lambda([this]() {
													if (!s_isCurrentlyBuilding)
														return true;
													else
														return false;
												})
												.HAlign(EHorizontalAlignment::HAlign_Center)
												.OnClicked_Lambda([this]() {
													if (IsPortalUserLoggedIn())
													{
														ForcePortalLogout();
													}
													else
													{
														portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::GETTOKEN);
													}
													return FReply::Handled();
												})
										]
								]
						]
				]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SUniformGridPanel)
				//.SlotPadding(FMargin(10, 5, 10, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SExpandableArea)
						.Visibility_Lambda([this]() {
						if (ProjectHasWarnings())
							return EVisibility::Visible;
						else
							return EVisibility::Collapsed;
						})
						.InitiallyCollapsed(false)
						.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
						.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BodyBorderBackgroundColor(FLinearColor::White)
						.HeaderContent()
						[
							SNew(STextBlock)
								.Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Warnings"))
								.ColorAndOpacity(FColor::Orange)
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot().AutoHeight()
								.Padding(FMargin(5, 2, 0, 1))
								.VAlign(EVerticalAlignment::VAlign_Center)
								.HAlign(EHorizontalAlignment::HAlign_Left)
								[
									SNew(STextBlock)
										.AutoWrapText(false)
										.WrapTextAt(250.0f)
										.Text_Lambda([this]() {
										if (ProjectHasWarnings())
										{
											FString codeProjWarning = FString("-C++ Project required. Create C++ class in project");
											FString pixelStreamWarning = FString("-Pixel Streaming plugin should be disabled");
											FString pixelCaptureWarning = FString("-Pixel Capture plugin should be enabled");
											FString openXRWarning = FString("-VR support requires OpenXR plugin");

											FString warningsStr = FString("");
											if (!IsCodeProject())
												warningsStr += codeProjWarning + "\n";

											if (IsPixelStreamingEnabled())
												warningsStr += pixelStreamWarning;

											if (!IsPixelCaptureEnabled())
												warningsStr += (warningsStr.IsEmpty()) ? pixelCaptureWarning : "\n" + pixelCaptureWarning;

											if (NeedsOpenXREnabling())
												warningsStr += (warningsStr.IsEmpty()) ? openXRWarning : "\n" + openXRWarning;

											return FText::FromString(warningsStr);
										}
										else
											return FText::FromString("");
									})
										.ColorAndOpacity(FColor::Orange)
										.Visibility_Lambda([this]() {
										if (ProjectHasWarnings())
											return EVisibility::Visible;
										else
											return EVisibility::Collapsed;
									})
								]
							+ SVerticalBox::Slot().AutoHeight()
								.Padding(FMargin(5, 2, 0, 1))
								.VAlign(EVerticalAlignment::VAlign_Center)
								.HAlign(EHorizontalAlignment::HAlign_Fill)
								[
									SNew(SButton)
										.Text(FText::FromString("        Fix Warnings        "))
										.HAlign(EHorizontalAlignment::HAlign_Center)
										.OnClicked(this, &FZeroLightMainButton::AutoResolveProjectWarnings)
										.Visibility_Lambda([this]() {
										if (ProjectHasWarnings())
											return EVisibility::Visible;
										else
											return EVisibility::Collapsed;
										})
								]
							+SVerticalBox::Slot().AutoHeight()
								.Padding(FMargin(5, 2, 0, 1))
								.VAlign(EVerticalAlignment::VAlign_Center)
								.HAlign(EHorizontalAlignment::HAlign_Fill)
								[
									SNew(SButton)
										.Text(FText::FromString("        FAQ Docs        "))
										.HAlign(EHorizontalAlignment::HAlign_Center)
										.OnClicked(this, &FZeroLightMainButton::OpenFAQDocsWebPage)
										.Visibility_Lambda([this]() {
										if (ProjectHasWarnings())
											return EVisibility::Visible;
										else
											return EVisibility::Collapsed;
									})
								]
						]
				]
		]

		//FreeSpace
		+ SVerticalBox::Slot()
		.FillHeight(1)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SUniformGridPanel)
			//.SlotPadding(FMargin(10, 5, 10, 0))
			+ SUniformGridPanel::Slot(0,0)
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(true)
				.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BodyBorderBackgroundColor(FLinearColor::White)
				.HeaderContent()
				[
					SNew(STextBlock).Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Advanced"))
				]
				.BodyContent()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					.Padding(0,0,0,3)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(13, 0, 60, 0)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock).Text(FText::FromString("Existing Build"))
						]

						+ SHorizontalBox::Slot()
						[
								SNew(SCheckBox)
								.OnCheckStateChanged(this, &FZeroLightMainButton::SetUseExistingBuild)
							.IsChecked(this, &FZeroLightMainButton::UseExistingBuild)
							.IsEnabled_Lambda([this]() {
							if (s_isCurrentlyBuilding)
								return false;
							else
								return s_uploadAsyncTask == nullptr;
						})
						]
					]

					+ SVerticalBox::Slot()
					.FillHeight(1)

					+ SVerticalBox::Slot().AutoHeight()
					.Padding(0, 0, 0, 3)
					[
						SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(13, 0, 60, 0)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock).Text(FText::FromString("Clean Debug Symbols from Build"))
							]

							+ SHorizontalBox::Slot()
							[
								SNew(SCheckBox)
									.OnCheckStateChanged(this, &FZeroLightMainButton::SetRemoveDebugSymbols)
									.IsChecked(this, &FZeroLightMainButton::UseRemoveDebugSymbols)
									.IsEnabled_Lambda([this]() {
										if (s_isCurrentlyBuilding)
											return false;
										else
											return s_uploadAsyncTask == nullptr;
								})
							]
					]

					/*+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.Padding(13, 3, 6, 4)
							.VAlign(EVerticalAlignment::VAlign_Center)
							[
								SNew(STextBlock).Text(FText::FromString("Custom Run Info JSON"))
							]

							+ SHorizontalBox::Slot()
							.Padding(0, 3, 0, 4)
							[
								SNew(SCheckBox)
									.OnCheckStateChanged(this, &FZeroLightMainButton::SetUseExistingRunInfoJson)
									.IsChecked(this, &FZeroLightMainButton::UseExistingRunInfo)
									.IsEnabled_Lambda([this]() {
									if (s_isCurrentlyBuilding)
										return false;
									else
										return s_uploadAsyncTask == nullptr;
								})
							]
					]*/

					+ SVerticalBox::Slot()
					.FillHeight(1)
				]
			]
		]

#if ALLOW_GITHUB_RELEASE_CHECKS

		+ SVerticalBox::Slot()
		.FillHeight(1)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SUniformGridPanel)
				//.SlotPadding(FMargin(10, 5, 10, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SExpandableArea)
						.Visibility_Lambda([this]() {
						if (NewerGithubPluginAvailable())
							return EVisibility::Visible;
						else
							return EVisibility::Collapsed;
					})
						.InitiallyCollapsed(false)
						.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
						.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BodyBorderBackgroundColor(FLinearColor::White)
						.HeaderContent()
						[
							SNew(STextBlock)
								.Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Updates"))
								.ColorAndOpacity(FColor::Orange)
						]
						.BodyContent()
						[
							SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
								.Padding(FMargin(5, 2, 0, 1))
								.VAlign(EVerticalAlignment::VAlign_Center)
								.HAlign(EHorizontalAlignment::HAlign_Left)
								[
									SNew(STextBlock)
										.AutoWrapText(false)
										.WrapTextAt(250.0f)
										.Text_Lambda([this]() {
											return FText::FromString("Newer plugin version available on GitHub");
										})
										.ColorAndOpacity(FColor::Orange)
								]
								+ SVerticalBox::Slot().AutoHeight()
								.Padding(FMargin(5, 2, 0, 1))
								.VAlign(EVerticalAlignment::VAlign_Center)
								.HAlign(EHorizontalAlignment::HAlign_Fill)
								[
									SNew(SButton)
										.Text(FText::FromString("        GitHub Releases        "))
										.HAlign(EHorizontalAlignment::HAlign_Center)
										.OnClicked(this, &FZeroLightMainButton::OpenGithubReleaseWebPage)
								]
						]
				]
		]
#endif

		//FreeSpace
		+ SVerticalBox::Slot()
		.FillHeight(1)

		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SUniformGridPanel)
				//.SlotPadding(FMargin(10, 5, 10, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SExpandableArea)
						.InitiallyCollapsed(true)
						.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
						.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.BodyBorderBackgroundColor(FLinearColor::White)
						.HeaderContent()
						[
							SNew(STextBlock).Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Development Features"))
						]
						.BodyContent()
						[
							SNew(SUniformGridPanel)
							.SlotPadding(FMargin(0,5,0,0))
							+ SUniformGridPanel::Slot(0, 0)
							[
								SNew(SHorizontalBox)
									+ SHorizontalBox::Slot()
									.AutoWidth()
									.Padding(13, 0, 5, 0)
									.VAlign(EVerticalAlignment::VAlign_Center)
									[
										SNew(STextBlock).Text(FText::FromString("Storage Provider"))
										.IsEnabled(true)
									]
									+ SHorizontalBox::Slot()
									[
										SNew(SComboBox<TSharedPtr<FString>>)
										.OptionsSource(&this->s_portalStorageProviderOptions)
										.OnSelectionChanged(this, &FZeroLightMainButton::SetPortalStorageProvider)
										.OnGenerateWidget_Lambda([](TSharedPtr<FString> Item) {
											return SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(*Item) : FText::GetEmpty());
										})
										.IsEnabled_Lambda([this]() {
											if (!s_isCurrentlyBuilding)
												return true;
											else
												return false;
										})
										.Content()
										[
											SNew(STextBlock)
											.Text_Lambda([this]() {
													return FText::FromString(s_portalStorageProvider);
											})
										]
									]
							]
						]
						.Visibility_Lambda([this]() {
						if (s_devFeaturesEnabled)
							return EVisibility::Visible;
						else
							return EVisibility::Hidden;
						})
				]
		]
		+ SVerticalBox::Slot()
			.FillHeight(1)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SUniformGridPanel)
			//.SlotPadding(FMargin(10, 5, 10, 0))
			+ SUniformGridPanel::Slot(0, 0)
			[
				SNew(SExpandableArea)
				.Visibility_Lambda([this]() {
						return EVisibility::Hidden;
				})
				.IsEnabled_Lambda([this]() {
					return !s_lastSucessPortalBuildRef.IsEmpty();
				})
				.InitiallyCollapsed(true)
				.BorderBackgroundColor(FLinearColor(0.4f, 0.4f, 0.4f, 1.0f))
				.BodyBorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.BodyBorderBackgroundColor(FLinearColor::White)
				.HeaderContent()
				[
					SNew(STextBlock).Text(NSLOCTEXT("OmniStreamSettings", "Settings", "Upload Result"))
				]
				.BodyContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(13, 0, 5, 0)
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString("Deploy Ref - "))
					]
					+ SHorizontalBox::Slot()
					[
						SNew(SEditableTextBox)
						.Text(this, &FZeroLightMainButton::GetLastSucessBuildRef)
						.IsReadOnly(true)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
			.FillHeight(1)
		+ SVerticalBox::Slot().AutoHeight()
			.Padding(10, 5, 10, 5)
			.VAlign(EVerticalAlignment::VAlign_Bottom)
			.HAlign(EHorizontalAlignment::HAlign_Left)
			[
				SNew(SScrollBox)
				.Orientation(EOrientation::Orient_Horizontal)
				.ScrollBarVisibility(EVisibility::Visible)
				+ SScrollBox::Slot()
				[
					SNew(STextBlock).Text(this, &FZeroLightMainButton::UpdateProgress)
				]
			]
		+ SVerticalBox::Slot()
			.FillHeight(1)
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SUniformGridPanel)
				//.SlotPadding(FMargin(10, 5, 10, 0))
				+ SUniformGridPanel::Slot(0, 0)
				[
					SNew(SButton)
					.Text(this, &FZeroLightMainButton::GetDeployButtonText)
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.OnClicked(this, &FZeroLightMainButton::TriggerBuildAndDeploy, FString(""))
					.ToolTipText_Lambda([this]() {
						if (ProjectHasWarnings())
						{
							return FText::FromString("Resolve warnings before uploading");
						}
						else if(!IsPortalUserLoggedIn())
							return FText::FromString("Log in to Portal before uploading");
						else
							return FText::FromString("");
					})
					.IsEnabled_Lambda([this]() { 
						if (!s_isPortalAuthorised || !s_currentAssetVersionId.IsEmpty() || s_isCurrentlyBuilding || s_getTokenAsyncTask != nullptr || s_uploadAsyncTask != nullptr || s_selectAssetLineAsyncTask != nullptr || ProjectHasWarnings() || !IsPortalUserLoggedIn())
							return false;
						else
							return true;
					})
				]
				+ SUniformGridPanel::Slot(0, 1)
					[
						SNew(SButton)
						.Text_Lambda([this]() {
							if (!s_currentAssetVersionId.IsEmpty() && s_uploadAsyncTask == nullptr)
								return FText::FromString("Cancel Upload");
							else if (!s_retryRenameBuildPath.IsEmpty())
								return FText::FromString("Retry Upload");
							else
								return (s_isCurrentlyBuilding || s_getTokenAsyncTask != nullptr || s_uploadAsyncTask != nullptr || s_selectAssetLineAsyncTask != nullptr) ? FText::FromString("Cancel") : FText::FromString("Refresh Auth Token");
						})
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.Visibility_Lambda([this]() {
							return (s_uploadAsyncTask != nullptr || s_getTokenAsyncTask != nullptr || s_selectAssetLineAsyncTask != nullptr || !s_isPortalAuthorised || !s_currentAssetVersionId.IsEmpty() || !s_retryRenameBuildPath.IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
						})
						.OnClicked(this, &FZeroLightMainButton::AbortCurrentProcessOrReauth)
					]
					+ SUniformGridPanel::Slot(0, 2)
					[
						SNew(SButton)
							.Text_Lambda([this]() {
							return (s_uploadAsyncTask != nullptr) ? FText::FromString("Pause Upload") : FText::FromString("Resume Upload");
						})
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.Visibility_Lambda([this]() {
							return (s_uploadAsyncTask != nullptr || !s_currentAssetVersionId.IsEmpty()) ? EVisibility::Visible : EVisibility::Collapsed;
						})
						.OnClicked(this, &FZeroLightMainButton::PauseOrResumeUpload)
					]
			]

		+ SVerticalBox::Slot() //Spacer
			.FillHeight(1)
	]
	);

	FSlateApplication::Get().AddWindow(BuildAndDeployWnd.ToSharedRef(), true);
}

FReply FZeroLightMainButton::AbortCurrentProcessOrReauth()
{
	if (s_getTokenAsyncTask == nullptr && !s_isCurrentlyBuilding && !s_isPortalAuthorised)
	{
		portalCLI->AsyncPortalTask((s_cloudstreamSettings->portalAssetLineId == "") ? portalcli::AsyncPortalTaskType::SELECTASSETLINE : portalcli::AsyncPortalTaskType::GETTOKEN);
	}
	else
	{
		SetProgressText(FText::FromString("Process cancelled"));

		if (s_getTokenAsyncTask != nullptr)
		{
			s_getTokenAsyncTask->Stop();
		}

		if (!s_currentAssetVersionId.IsEmpty() && s_uploadAsyncTask == nullptr)
		{
			s_currentAssetVersionId = ""; //Cancel tracking the existing/paused asset version upload so next run will be a fresh attempt
			SetProgressText(FText::FromString("Upload Cancelled"));
			//Restore build directory
			RestoreToBuildDirectory();
		}
		else if (s_uploadAsyncTask != nullptr)
		{
			s_userCancelledUpload = true;
			s_uploadAsyncTask->Stop();
		}
		else if (!s_retryRenameBuildPath.IsEmpty())
		{
			RetryRenameUploadExistingBuild();
		}


		if (s_selectAssetLineAsyncTask != nullptr)
		{
			s_selectAssetLineAsyncTask->Stop();
		}


	}

	s_isCurrentlyBuilding = false;

	return FReply::Handled();
}

FReply FZeroLightMainButton::PauseOrResumeUpload()
{
	if (s_uploadAsyncTask != nullptr)
	{
		s_userPausedUpload = true;
		s_uploadAsyncTask->Stop();
	}
	else
		PortalUploadAndZip(s_pausedUploadPath);

	return FReply::Handled();
}

FReply FZeroLightMainButton::SelectAssetLine()
{
	if (s_selectAssetLineAsyncTask == nullptr && !s_isCurrentlyBuilding)
	{
		portalCLI->AsyncPortalTask(portalcli::AsyncPortalTaskType::SELECTASSETLINE);
	}

	s_isCurrentlyBuilding = false;

	return FReply::Handled();
}

FReply FZeroLightMainButton::ForcePortalLogout()
{
	OutputMessage logoutMsg = portalCLI->ForceLogout();

	if(!logoutMsg.is_eof())
		logoutMsg.print();

	s_portalUsername = "Not Logged In";

	return FReply::Handled();
}

FReply FZeroLightMainButton::OpenFAQDocsWebPage()
{
	FString FullURL = FString::Printf(TEXT("https://resources.zerolight.com/docs/omnistream/FAQ#unreal-is-not-including-the-plugins"));
	FPlatformProcess::LaunchURL(*FullURL, nullptr, nullptr);

	return FReply::Handled();
}

FReply FZeroLightMainButton::OpenGithubReleaseWebPage()
{
	FString FullURL = FString::Printf(TEXT("https://github.com/ZeroLight-Ltd/OmniStream-Unreal-Plugin/releases"));
	FPlatformProcess::LaunchURL(*FullURL, nullptr, nullptr);

	return FReply::Handled();
}

void FZeroLightMainButton::ValidateJSONSchema_Async()
{
	Async(EAsyncExecution::TaskGraphMainThread, [this]()
	{
		FScopedSlowTask SlowTask(2, FText::FromString(TEXT("Analyzing state fields in project...")));
		SlowTask.MakeDialog(true);

		SlowTask.EnterProgressFrame(1);

		TSharedPtr<FJsonObject> stateKeysAggregated = AggregatePredictedJSONSchema();

		FString JsonString_AggregatedSchema;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString_AggregatedSchema, 1);
		FJsonSerializer::Serialize(stateKeysAggregated.ToSharedRef(), JsonWriter);
		JsonWriter->Close();

		SlowTask.EnterProgressFrame(1);

		UE_LOG(LogPortalCLI, Log, TEXT("Printing JSON Schema (aggregated from BP nodes using State Manager)"));
		UE_LOG(LogPortalCLI, Log, TEXT("%s"), *JsonString_AggregatedSchema);
	});

}

void FZeroLightMainButton::About()
{
	const FText AboutWindowTitle = FText::FromString(TEXT("About ZeroLight OmniStream"));

	TSharedPtr<SWindow> AboutWindow = 
		SNew(SWindow)
		.CreateTitleBar(false)
		.Title( AboutWindowTitle )
		.ClientSize(FVector2D(600.f, 250.f))
		.SupportsMaximize(false) .SupportsMinimize(false)
		.SizingRule( ESizingRule::FixedSize )
		[
			SNew(SZeroLightAboutScreen)
		];

	IMainFrameModule& MainFrame = FModuleManager::LoadModuleChecked<IMainFrameModule>( "MainFrame" );
	TSharedPtr<SWindow> ParentWindow = MainFrame.GetParentWindow();

	if ( ParentWindow.IsValid() )
	{
		FSlateApplication::Get().AddModalWindow(AboutWindow.ToSharedRef(), ParentWindow.ToSharedRef());
	}
	else
	{
		FSlateApplication::Get().AddWindow(AboutWindow.ToSharedRef());
	}
}

portalcli::portalcli()
{
	if (!PortalClient::InitPlugin())
	{
		UE_LOG(LogPortalCLI, Error, TEXT("ZLCloudStream Failed to load Portal Client dll"));
		return;
	}

	CreateDllPortalClient();
}

portalcli::portalcli(portal::BuildContext* context)
{
	if (!PortalClient::InitPlugin())
	{
		UE_LOG(LogPortalCLI, Error, TEXT("ZLCloudStream Failed to load Portal Client dll"));
		return;
	}

	CreateDllPortalClient(context);
}

void portalcli::CreateDllPortalClient()
{
	if (m_client != nullptr)
	{
		if (m_ps != nullptr)
			m_ps.Reset();

		m_client.Reset();
	}

	Config cfg = Config();

	std::string utf8String(TCHAR_TO_UTF8(TEXT("OmniStreamUpload")));
	int stringSize = utf8String.length() + 1;
	char* utf8StringChar = new char[stringSize];
	strcpy_s(utf8StringChar, stringSize, utf8String.c_str());

	cfg.add_scope(utf8StringChar);

	//Set to live
	FString portalServerStr = FZeroLightMainButton::GetPortalServerUrl();
	std::string portalServerUrl(TCHAR_TO_UTF8(*portalServerStr));
	stringSize = portalServerUrl.length() + 1;
	char* utf8StringUrl = new char[stringSize];
	strcpy_s(utf8StringUrl, stringSize, portalServerUrl.c_str());

	portal::config_set_portal_server(cfg.m_cfg, utf8StringUrl);

	std::string httpProxyOverride(TCHAR_TO_UTF8(*FZeroLightMainButton::GetHttpProxyOverride()));
	stringSize = httpProxyOverride.length() + 1;
	char* httpProxyString = new char[stringSize];
	strcpy_s(httpProxyString, stringSize, httpProxyOverride.c_str());

	if (httpProxyOverride != "")
		portal::config_set_http_proxy(cfg.m_cfg, httpProxyString);

	m_client = MakeShareable<PortalClient>(new PortalClient(&cfg));

	delete[] utf8StringChar;
	delete[] utf8StringUrl;
	delete[] httpProxyString;
}

void portalcli::CreateDllPortalClient(portal::BuildContext* bc)
{
	if (m_client != nullptr)
	{
		if (m_ps != nullptr)
			m_ps.Reset();

		m_client.Reset();
	}

	Config cfg = Config();

	std::string utf8String(TCHAR_TO_UTF8(TEXT("OmniStreamUpload")));
	int stringSize = utf8String.length() + 1;
	char* utf8StringChar = new char[stringSize];
	strcpy_s(utf8StringChar, stringSize, utf8String.c_str());

	cfg.add_scope(utf8StringChar);

	//Set to live
	std::string portalServerUrl(TCHAR_TO_UTF8(*FZeroLightMainButton::GetPortalServerUrl()));
	stringSize = portalServerUrl.length() + 1;
	char* utf8StringUrl = new char[stringSize];
	strcpy_s(utf8StringUrl, stringSize, portalServerUrl.c_str());

	portal::config_set_portal_server(cfg.m_cfg, utf8StringUrl);
	
	std::string httpProxyOverride(TCHAR_TO_UTF8(*FZeroLightMainButton::GetHttpProxyOverride()));
	stringSize = httpProxyOverride.length() + 1;
	char* httpProxyString = new char[stringSize];
	strcpy_s(httpProxyString, stringSize, httpProxyOverride.c_str());

	if(httpProxyOverride != "")
		portal::config_set_http_proxy(cfg.m_cfg, httpProxyString);

	m_client = MakeShareable<PortalClient>(new PortalClient(&cfg, bc));

	m_ps = MakeShareable<ProgressStream>(new ProgressStream(portal::client_take_progress_stream(m_client->m_client)));

	delete[] utf8StringChar;
	delete[] utf8StringUrl;
	delete[] httpProxyString;
}

void portalcli::AsyncPortalTask(AsyncPortalTaskType taskType, bool triggerBuildTask, bool triggerExistingBuildDeploy, FString targetPath)
{
	switch (taskType)
	{
	case portalcli::UPLOAD:
		FZeroLightMainButton::SetProgressText(FText::FromString("Uploading to Portal..."));

		FZeroLightMainButton::SetUploadTask(MakeShareable<UploadAsyncTask>(new UploadAsyncTask(
			portal::client_get_build_context(m_client->m_client), 
			FZeroLightMainButton::GetUploadDirectory(), 
			FZeroLightMainButton::GetDeployName(), 
			FZeroLightMainButton::GetPortalAssetLineID(), 
			FZeroLightMainButton::s_isCIBuild,
			FZeroLightMainButton::GetPortalStorageProvider(),
			FZeroLightMainButton::GetBuildMetadata())));
		break;
	case portalcli::GETTOKEN:
		FZeroLightMainButton::SetAuthTask(MakeShareable<GetTokenAsyncTask>(new GetTokenAsyncTask(portal::client_get_build_context(m_client->m_client), triggerBuildTask, triggerExistingBuildDeploy, targetPath)));
		break;
	case portalcli::SELECTASSETLINE:
		FZeroLightMainButton::SetSelectAssetLineTask(MakeShareable<SelectAssetLineAsyncTask>(new SelectAssetLineAsyncTask(portal::client_get_build_context(m_client->m_client))));
		break;
	default:
		break;
	}	
}

bool portalcli::ReportBuildFailure()
{
	TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
	JsonObject->SetBoolField("fail", true);

	FString OutputString;

	// Create a Json writer
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&OutputString);

	// Serialize the Json object to a string
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

	std::string utf8String(TCHAR_TO_UTF8(*OutputString));

	int stringSize = utf8String.length() + 1;
	char* pSendData = new char[stringSize];
	strcpy_s(pSendData, stringSize, utf8String.c_str());

	OutputMessage createMsg = m_client->finalize_build(pSendData);

	delete pSendData;

	createMsg.print();

	return true;
}

bool portalcli::ClearCurrentBuild()
{
	s_currentBuildIdent = "";

	CreateDllPortalClient();

	return true;
}

void portalcli::StartOutputReadTask()
{
	if (!m_isRunningReadOutputTask)
	{
		m_isRunningReadOutputTask = true;
		AsyncTask(ENamedThreads::AnyThread, [this]()
		{
			while (m_isRunningReadOutputTask)
			{
				if (m_ps != nullptr)
				{
					OutputMessage msg = m_ps->get_next_message();
					if (msg.is_eof()) {
						m_isRunningReadOutputTask = false;
						break;
					}
					if (msg.print())
					{
						FZeroLightMainButton::SetProgressText(FText::FromString(msg.ToFString().Replace(TEXT("\n"), TEXT(" "))));
					}
				}

			}

			if (m_ps != nullptr)
			{
				m_ps.Reset();
				m_ps = nullptr;
			}
		});
	}
}

void portalcli::StopOutputReadTask()
{
	{
		m_isRunningReadOutputTask = false;
	}
}

OutputMessage portalcli::ForceLogout()
{
	if(m_client != nullptr)
		return OutputMessage(portal::client_logout_user(m_client->m_client));
	return OutputMessage(nullptr);
}


uint32 GetTokenAsyncTask::Run()
{
	portalcli threadBardCli = portalcli(buildContext);

	portal::FFIString fallbackFfistr;
	fallbackFfistr.c_str = nullptr;

	m_tokenTask = portal::client_ensure_token_is_fetched_with_fallback_url_async(threadBardCli.m_client->m_client, &fallbackFfistr);

	if (m_tokenTask == nullptr)
	{
		fetch_error();
		FZeroLightMainButton::SetIsPortalAuthorised(false);
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Plugin Failed"));
		return 1;
	}

	FString fallbackUrlStr(FFIString(fallbackFfistr).ToFString());

	//SET_WARN_COLOR(OutputDeviceColor::COLOR_CYAN); //This does not work as documentation implies in OutputDevice.h, using warning for now
	UE_LOG(LogPortalCLI, Warning, TEXT("Authorising Portal credentials, fallback url (use if browser does not open): %s"), *fallbackUrlStr);
	//CLEAR_WARN_COLOR();

	FZeroLightMainButton::SetProgressText(FText::FromString("Authorising Portal credentials..."));

	m_abortHandle = portal::ensure_token_task_get_abort_handle(m_tokenTask);

	portal::EnsureTokenResult* result = portal::ensure_token_task_wait(threadBardCli.m_client->m_client, m_tokenTask);

	if (result == nullptr)
	{
		fetch_error();
		threadBardCli.StopOutputReadTask();
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Plugin Failed"));
		FZeroLightMainButton::SetIsBuilding(false);

		if (s_CIMode)
			FPlatformMisc::RequestExit(true);

		return 1;
	}

	if (!portal::ensure_token_task_result_was_cancelled(result))
	{
		void* resultOutputPtr = portal::ensure_token_task_result_into_output(result);

		if (resultOutputPtr != nullptr)
		{
			FZeroLightMainButton::SetIsPortalAuthorised(true);
			FZeroLightMainButton::SetProgressText(FText::FromString("Portal Auth Successful"));

			if (!FZeroLightMainButton::s_isCIBuild)
			{
				//set logged in username
				OutputMessage loggedInUserMsg = OutputMessage(portal::client_get_logged_in_user(threadBardCli.m_client->m_client));

				FString usernameJson = FFIString(portal::output_message_to_json(loggedInUserMsg.m_output_message)).ToFString();

				TSharedRef<TJsonReader<TCHAR>> usernameJsonReader = TJsonReaderFactory<TCHAR>::Create(usernameJson);
				TSharedPtr<FJsonObject> usernameJsonObj;
				if (FJsonSerializer::Deserialize(usernameJsonReader, usernameJsonObj) && usernameJsonObj.IsValid())
				{
					FString usernameStr;
					if (usernameJsonObj->TryGetField(FString("user_info")) != nullptr)
					{
						if (usernameJsonObj->GetObjectField(FString("user_info"))->TryGetStringField(FString("username"), usernameStr))
							FZeroLightMainButton::SetPortalUsername(usernameStr);
					}
				}
			}

			if (triggerBuild && !downstreamJobTargetPath.IsEmpty())
			{
				FZeroLightMainButton::SetBuildFolder(downstreamJobTargetPath);
				FZeroLightMainButton::TriggerBuild();
			}
			else if (triggerExistingDeploy && !downstreamJobTargetPath.IsEmpty())
			{
				FZeroLightMainButton::SetBuildFolder(downstreamJobTargetPath);
				FZeroLightMainButton::TriggerExistingBuildDeploy();
			}
		}
	}
	else
	{
		portal::ensure_token_task_result_destory(result);
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Auth Cancelled"));
		FZeroLightMainButton::SetIsPortalAuthorised(false);
	}

	return 0;
};

void GetTokenAsyncTask::Stop()
{
	if (m_abortHandle != nullptr)
	{
		portal::abort_handle_cancel_task(m_abortHandle);
		portal::abort_handle_destory_name(m_abortHandle);
		m_abortHandle = nullptr;
	}
}

void GetTokenAsyncTask::Exit()
{
	FZeroLightMainButton::ClearAuthTask();
}

uint32 UploadAsyncTask::Run()
{
	portalcli threadBardCli = portalcli(buildContext);

	m_tokenTask = portal::client_ensure_token_is_fetched_async(threadBardCli.m_client->m_client);

	if (m_tokenTask == nullptr)
	{
		fetch_error();
		FZeroLightMainButton::SetIsPortalAuthorised(false);
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Plugin Failed"));
		return 1;
	}

	threadBardCli.StartOutputReadTask();

	m_abortHandle = portal::ensure_token_task_get_abort_handle(m_tokenTask);

	portal::EnsureTokenResult* result = portal::ensure_token_task_wait(threadBardCli.m_client->m_client, m_tokenTask);

	if (result == nullptr)
	{
		fetch_error();
		threadBardCli.StopOutputReadTask();
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Plugin Failed"));
		FZeroLightMainButton::SetIsBuilding(false);

		if (s_CIMode)
			FPlatformMisc::RequestExit(true);

		return 1;
	}

	if (!portal::ensure_token_task_result_was_cancelled(result))
	{
		void* resultOutputPtr = portal::ensure_token_task_result_into_output(result);

		if (resultOutputPtr != nullptr)
		{
			FZeroLightMainButton::SetIsPortalAuthorised(true);
		}

		portal::abort_handle_destory_name(m_abortHandle);

		//Stirp invalid characters out of customer set names for filepath safety (charatcers will be preserved in the runinfo)
		FString noSpaceNameStr(FZeroLightMainButton::FormatFStringFilenameSafe(FZeroLightMainButton::GetDeployName()));
		FString projectStr(FZeroLightMainButton::FormatFStringFilenameSafe(FZeroLightMainButton::GetProjectName()));
		FString buildIdStr(FZeroLightMainButton::FormatFStringFilenameSafe(FZeroLightMainButton::GetBuildId()));
		
		std::filesystem::path origBuildPathStd = TCHAR_TO_UTF8(*srcPath);

		//For clean extract on deployments rename Build and add top level project folder
		std::filesystem::path parentDirPath = origBuildPathStd.parent_path();
		FString parentPath = FString(parentDirPath.c_str());
		FString finalCopyPath = FString(parentDirPath.c_str());
		FString finalUploadPath = FString(parentDirPath.c_str());

		finalCopyPath += "/OmniStreamBuild/ZLAssets/Viewers/" + projectStr + "/" + noSpaceNameStr + "/" + buildIdStr;
		finalUploadPath += "/OmniStreamBuild";

		if (FZeroLightMainButton::GetCurrentUploadAssetVersionId().IsEmpty())
		{
			std::filesystem::path finalCopyFSPath = TCHAR_TO_UTF8(*finalCopyPath);

			if (FPaths::DirectoryExists(finalUploadPath))
			{
				if (!std::filesystem::remove_all(TCHAR_TO_UTF8(*finalUploadPath)))
				{
				}

				std::filesystem::create_directories(finalCopyFSPath.parent_path());
			}
			else
				std::filesystem::create_directories(finalCopyFSPath.parent_path());

			int attempts = 0;
			const int max_attempts = 3;
			bool renameSuccess = false;

			while (attempts < max_attempts && !renameSuccess) {
				try {
					std::filesystem::rename(origBuildPathStd, finalCopyFSPath);
					renameSuccess = true;
				}
				catch (const std::filesystem::filesystem_error& e) {
					UE_LOG(LogPortalCLI, Log, TEXT("Rename packaged build attempt %d failed: %s"), (attempts + 1), *FString(e.what()));
					attempts++;
					FPlatformProcess::Sleep(1.0f);
				}
			}

			if (!renameSuccess)
			{
				FZeroLightMainButton::SetProgressText(FText::FromString(srcPath + " could not be renamed - Close any open folders and/or files in directory and retry"));
				FZeroLightMainButton::SetIsBuilding(false);
				return false;
			}

			if (FZeroLightMainButton::s_removeDebugSymbols)
			{
				// Get the file manager
				IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

				FString FileExtension = TEXT(".pdb");
				TArray<FString> FoundFiles;

				PlatformFile.FindFilesRecursively(FoundFiles, *finalCopyPath, *FileExtension);

				for (const FString& FilePath : FoundFiles)
				{
					PlatformFile.DeleteFile(*FilePath);
				}
			}
		}

		bool specifyThumbnailPath = false;
		FString thumbnailFilePath = finalCopyPath;

		IFileManager& fileManager = IFileManager::Get();

		TArray<FString> foundFiles;
		fileManager.FindFiles(foundFiles, *finalCopyPath);
		for (const FString& file : foundFiles)
		{
			if (FPaths::GetBaseFilename(file) == "appThumbnail")
			{
				specifyThumbnailPath = true;
				thumbnailFilePath = "ZLAssets/Viewers/" + projectStr + "/" + noSpaceNameStr + "/" + buildIdStr + "/" + file;
			}
		}

		FZeroLightMainButton::s_pausedUploadPath = finalUploadPath; //Store for continuing a paused upload
		FZeroLightMainButton::SetRestorePaths(finalCopyPath, parentPath + "/Windows", finalUploadPath);

		std::string assetLinePtr = TCHAR_TO_UTF8(*assetLineID);
		std::string storageProviderPtr = (portalStorageProvider.IsEmpty()) ? "" : TCHAR_TO_UTF8(*portalStorageProvider);
		std::string dir = TCHAR_TO_UTF8(*finalUploadPath);
		std::string thumbnailPathPtr = (!specifyThumbnailPath) ? "" : TCHAR_TO_UTF8(*thumbnailFilePath);

		std::string metaStr(TCHAR_TO_UTF8(*buildMetadata));
		size_t metaLen = metaStr.length();

		uint32_t len = static_cast<uint32_t>(assetLineID.Len());
		uint32_t spLen = (portalStorageProvider.IsEmpty()) ? 0 : static_cast<uint32_t>(portalStorageProvider.Len());
		uint32_t dirLen = static_cast<uint32_t>(finalUploadPath.Len());
		uint32_t metadataLen = (buildMetadata.IsEmpty()) ? 0 : static_cast<uint32_t>(metaLen);
		uint32_t thumbnailLen = (!specifyThumbnailPath) ? 0 : static_cast<uint32_t>(thumbnailFilePath.Len());

		portal::UTF8InputString assetLineInputStr;
		assetLineInputStr.pointer = reinterpret_cast<const uint8_t*>(&assetLinePtr[0]);
		assetLineInputStr.length_in_bytes = len;

		portal::UTF8InputString storageProviderInputStr;
		storageProviderInputStr.pointer = (portalStorageProvider.IsEmpty()) ? nullptr : reinterpret_cast<const uint8_t*>(&storageProviderPtr[0]);
		storageProviderInputStr.length_in_bytes = spLen;

		portal::UTF8InputString interimSPInputStr;
		interimSPInputStr.pointer = nullptr; interimSPInputStr.length_in_bytes = 0;

		portal::UTF8InputString directoryInputStr;
		directoryInputStr.pointer = reinterpret_cast<const uint8_t*>(&dir[0]);
		directoryInputStr.length_in_bytes = dirLen;

		portal::UTF8InputString extensionDataInputStr;
		extensionDataInputStr.pointer = (buildMetadata.IsEmpty()) ? nullptr : reinterpret_cast<const uint8_t*>(&metaStr[0]);
		extensionDataInputStr.length_in_bytes = metaLen;

		portal::UTF8InputString thumbnailInputStr;
		thumbnailInputStr.pointer = (!specifyThumbnailPath) ? nullptr : reinterpret_cast<const uint8_t*>(&thumbnailPathPtr[0]);
		thumbnailInputStr.length_in_bytes = thumbnailLen;

		FString currentAssetVersionId = FZeroLightMainButton::GetCurrentUploadAssetVersionId();
		if (currentAssetVersionId.IsEmpty())
		{
			OutputMessage createAssetVersionOutput = portal::client_create_portal_asset_version(threadBardCli.m_client->m_client, assetLineInputStr, storageProviderInputStr, extensionDataInputStr, directoryInputStr, 0);

			createAssetVersionOutput.print();

			FString createAssetVersionJsonStr = FFIString(portal::output_message_to_json(createAssetVersionOutput.m_output_message)).ToFString();
			TSharedRef<TJsonReader<TCHAR>> createAssetVersionResultJsonReader = TJsonReaderFactory<TCHAR>::Create(createAssetVersionJsonStr);
			TSharedPtr<FJsonObject> createAssetVersionResultJsonObj;
			if (FJsonSerializer::Deserialize(createAssetVersionResultJsonReader, createAssetVersionResultJsonObj) && createAssetVersionResultJsonObj.IsValid())
			{
				FString assetVersionId = createAssetVersionResultJsonObj->GetObjectField(FString("data"))->GetStringField("_id");
				currentAssetVersionId = assetVersionId;
				FZeroLightMainButton::SetCurrentUploadAssetVersionId(currentAssetVersionId);
			}
		}

		std::string assetVersionIdPtr = TCHAR_TO_UTF8(*currentAssetVersionId);
		uint32_t assetVersionIdLen = static_cast<uint32_t>(currentAssetVersionId.Len());

		portal::UTF8InputString assetVersionIDInputStr;
		assetVersionIDInputStr.pointer = (currentAssetVersionId.IsEmpty()) ? nullptr : reinterpret_cast<const uint8_t*>(&assetVersionIdPtr[0]); 
		assetVersionIDInputStr.length_in_bytes = assetVersionIdLen;

		portal::OneStepUploadParams uploadParams;

		uploadParams.asset_version_id = assetVersionIDInputStr;
		uploadParams.asset_line_id = assetLineInputStr;
		uploadParams.storage_provider = storageProviderInputStr;
		uploadParams.disable_intermeidate_provider = false;
		uploadParams.intermediate_storage_provider = interimSPInputStr;
		uploadParams.directory = directoryInputStr;
		uploadParams.extension_data = extensionDataInputStr;
		uploadParams.thumbnail = thumbnailInputStr;
		uploadParams.chunk_size = 0;

		m_oneStepPortalUploadTask = handle_ptr_error(portal::client_upload_portal_asset_version_one_step_async(
			threadBardCli.m_client->m_client,
			uploadParams));

		if (m_oneStepPortalUploadTask == nullptr)
		{
			threadBardCli.StopOutputReadTask();
			FZeroLightMainButton::SetProgressText(FText::FromString("Portal Upload Failed"));
			FZeroLightMainButton::SetIsBuilding(false);

			if (s_CIMode)
				FPlatformMisc::RequestExit(true);

			return 1;
		}

		m_abortHandle = portal::client_upload_one_step_task_get_abort_handle(m_oneStepPortalUploadTask);

		portal::OneStepUploadResult* uploadResult = handle_ptr_error(portal::client_upload_one_step_task_wait(threadBardCli.m_client->m_client, m_oneStepPortalUploadTask));

		if (uploadResult != nullptr)
		{
			if (!portal::client_upload_one_step_task_result_was_cancelled(uploadResult))
			{
				if (uploadResult != nullptr)
				{
					bool success = true;
					FZeroLightMainButton::SetIsBuilding(false);

					OutputMessage uploadOutput = OutputMessage(portal::client_upload_one_step_task_result_into_output(uploadResult));

					uploadOutput.print();

					FString uploadJsonStr = FFIString(portal::output_message_to_json(uploadOutput.m_output_message)).ToFString();
					TSharedRef<TJsonReader<TCHAR>> uploadResultJsonReader = TJsonReaderFactory<TCHAR>::Create(uploadJsonStr);
					TSharedPtr<FJsonObject> uploadResultJsonObj;
					if (FJsonSerializer::Deserialize(uploadResultJsonReader, uploadResultJsonObj) && uploadResultJsonObj.IsValid())
					{
						FZeroLightMainButton::SetActivePortalBuildRef(uploadResultJsonObj->GetStringField("asset_version_id"));
					}

					if (success)
					{
						if (isCIUpload)
						{
							UE_LOG(LogPortalCLI, Log, TEXT("Command Line Build & Upload upload completed, terminating Unreal Engine..."));
							FPlatformMisc::RequestExit(true);
							return 0;
						}
						else
						{
							FZeroLightMainButton::SetProgressText(FText::FromString("Build successfully packaged and uploaded"));
							FZeroLightMainButton::SetLastSucessPortalBuildRef();
							AsyncTask(ENamedThreads::GameThread, [this]()
							{
								//Creates a new notification info, we pass in our text as the parameter.
								FNotificationInfo Info(LOCTEXT("OmniStreamTaskSuccess_Notification", "OmniStream Upload Completed"));

								//Set a default expire duration
								Info.ExpireDuration = 10.0f;
								Info.bUseLargeFont = true;
								Info.bUseSuccessFailIcons = true;

								FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
							});
							FZeroLightMainButton::SetCurrentUploadAssetVersionId("");
							FZeroLightMainButton::RestoreToBuildDirectory();
						}					
					}
					else
					{
						FZeroLightMainButton::SetProgressText(FText::FromString("Error during Portal upload"));
						if (isCIUpload)
							FPlatformMisc::RequestExit(true);
						else
							FZeroLightMainButton::RestoreToBuildDirectory();
					}
				}
				else
				{
					if(FZeroLightMainButton::s_userCancelledUpload)
						FZeroLightMainButton::SetCurrentUploadAssetVersionId("");

					FZeroLightMainButton::SetProgressText(GetUploadEndedProgressText());
						
					if (isCIUpload)
						FPlatformMisc::RequestExit(true);
					else
					{
						if(FZeroLightMainButton::s_userCancelledUpload)
							FZeroLightMainButton::RestoreToBuildDirectory();
					}
					FZeroLightMainButton::s_userPausedUpload = false;
					FZeroLightMainButton::s_userCancelledUpload = false;
				}
			}
			else
			{
				portal::client_upload_one_step_task_result_destory(uploadResult);
				threadBardCli.StopOutputReadTask();

				if (FZeroLightMainButton::s_userCancelledUpload)
					FZeroLightMainButton::SetCurrentUploadAssetVersionId("");

				FZeroLightMainButton::SetProgressText(GetUploadEndedProgressText());

				if (isCIUpload)
				{
					FPlatformMisc::RequestExit(true);
				}
				else
				{
					AsyncTask(ENamedThreads::GameThread, [this]()
					{
						FNotificationInfo Info(LOCTEXT("OmniStreamTaskSuccess_Notification", "Upload Failed"));
							
						//Set a default expire duration
						Info.ExpireDuration = 10.0f;
						Info.bUseLargeFont = true;
						Info.bUseSuccessFailIcons = true;
						Info.Text = GetUploadEndedProgressText();

						FZeroLightMainButton::s_userCancelledUpload = false;

						FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);

					});
					if(FZeroLightMainButton::s_userCancelledUpload)
						FZeroLightMainButton::RestoreToBuildDirectory();
					FZeroLightMainButton::s_userPausedUpload = false;
				}
			}
		}
		else
		{
			threadBardCli.StopOutputReadTask();
			FZeroLightMainButton::SetProgressText(FText::FromString("Upload Failed"));

			if (isCIUpload)
			{
				FPlatformMisc::RequestExit(true);
			}
			else
			{
				AsyncTask(ENamedThreads::GameThread, [this]()
				{
					// Creates a new notification info, we pass in our text as the parameter.
					FNotificationInfo Info(LOCTEXT("OmniStreamTaskSuccess_Notification", "OmniStream Upload Failed"));

					//Set a default expire duration
					Info.ExpireDuration = 10.0f;
					Info.bUseLargeFont = true;
					Info.bUseSuccessFailIcons = true;

					FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);

				});
			}
		}

		FZeroLightMainButton::SetIsBuilding(false);

		return 0;
	}
	else
	{
		if (FZeroLightMainButton::s_userCancelledUpload)
			FZeroLightMainButton::SetCurrentUploadAssetVersionId("");

		FZeroLightMainButton::SetProgressText(GetUploadEndedProgressText());
		FZeroLightMainButton::s_userCancelledUpload = false;
		FZeroLightMainButton::s_userPausedUpload = false;

		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			//Creates a new notification info, we pass in our text as the parameter.
			FNotificationInfo Info(LOCTEXT("OmniStreamTaskSuccess_Notification", "OmniStream Upload Cancelled"));

			//Set a default expire duration
			Info.ExpireDuration = 5.0f;
			Info.bUseLargeFont = true;
			Info.bUseSuccessFailIcons = true;

			FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::ECompletionState::CS_Fail);
		});

		if (s_CIMode)
			FPlatformMisc::RequestExit(true);
	}

	return 0;
};

FText UploadAsyncTask::GetUploadEndedProgressText()
{
	if (FZeroLightMainButton::s_userPausedUpload)
		return FText::FromString("Upload Paused");
	else
		return (FZeroLightMainButton::s_userCancelledUpload) ? FText::FromString("Upload Cancelled") : FText::FromString("Upload Failed");
}

void UploadAsyncTask::Stop()
{
	if (m_abortHandle != nullptr)
	{
		portal::abort_handle_cancel_task(m_abortHandle);
		portal::abort_handle_destory_name(m_abortHandle);
		m_abortHandle = nullptr;
	}
}

void UploadAsyncTask::Exit()
{
	FZeroLightMainButton::ClearAuthTask();
	FZeroLightMainButton::ClearUploadTask();
};

uint32 SelectAssetLineAsyncTask::Run()
{
	portalcli threadBardCli = portalcli(buildContext);

	portal::OpenSelectAssetLineParams params;

	portal::UTF8InputString currentSelectedAssetLineInputStr;
	FString currentAssetLineID = FZeroLightMainButton::GetPortalAssetLineID();
	std::string assetLinePtr = TCHAR_TO_UTF8(*currentAssetLineID);

	currentSelectedAssetLineInputStr.pointer = (currentAssetLineID.IsEmpty()) ? nullptr : reinterpret_cast<const uint8_t*>(&assetLinePtr[0]);
	currentSelectedAssetLineInputStr.length_in_bytes = currentAssetLineID.Len();

	params.suggested_asset_line = currentSelectedAssetLineInputStr;

	m_tokenTask = portal::client_open_select_asset_line_gui_async(threadBardCli.m_client->m_client, params);

	FZeroLightMainButton::SetProgressText(FText::FromString("Selecting Asset Line from Portal..."));

	if (m_tokenTask == nullptr)
	{
		fetch_error();
		FZeroLightMainButton::SetIsPortalAuthorised(false);
		FZeroLightMainButton::SetProgressText(FText::FromString("Portal Plugin Failed"));
		return 1;
	}

	m_abortHandle = portal::client_open_select_asset_line_gui_get_abort_handle(m_tokenTask);

	portal::OpenSelectAssetLineGuiResult* result = portal::client_open_select_asset_line_gui_wait(threadBardCli.m_client->m_client, m_tokenTask);

	if (!portal::client_open_select_asset_line_gui_result_was_cancelled(result))
	{
		if (result != nullptr)
		{
			OutputMessage selectAssetLineOutput = portal::client_open_select_asset_line_gui_result_into_output(result);

			FZeroLightMainButton::SetIsPortalAuthorised(true);
			FZeroLightMainButton::SetProgressText(FText::FromString("Asset Line Selection Successful"));

			if (!FZeroLightMainButton::s_isCIBuild)
			{
				//set logged in username
				OutputMessage loggedInUserMsg = OutputMessage(portal::client_get_logged_in_user(threadBardCli.m_client->m_client));

				FString usernameJson = FFIString(portal::output_message_to_json(loggedInUserMsg.m_output_message)).ToFString();

				TSharedRef<TJsonReader<TCHAR>> usernameJsonReader = TJsonReaderFactory<TCHAR>::Create(usernameJson);
				TSharedPtr<FJsonObject> usernameJsonObj;
				if (FJsonSerializer::Deserialize(usernameJsonReader, usernameJsonObj) && usernameJsonObj.IsValid())
				{
					FString usernameStr;
					if (usernameJsonObj->TryGetField(FString("user_info")) != nullptr)
					{
						if (usernameJsonObj->GetObjectField(FString("user_info"))->TryGetStringField(FString("username"), usernameStr))
							FZeroLightMainButton::SetPortalUsername(usernameStr);
					}
				}
			}

			FString assetSelectionJson = FFIString(portal::output_message_to_json(selectAssetLineOutput.m_output_message)).ToFString();

			TSharedRef<TJsonReader<TCHAR>> assetSelectJsonReader = TJsonReaderFactory<TCHAR>::Create(assetSelectionJson);
			TSharedPtr<FJsonObject> assetSelectJsonObj;
			if (FJsonSerializer::Deserialize(assetSelectJsonReader, assetSelectJsonObj) && assetSelectJsonObj.IsValid())
			{
				FString assetLineIdStr, usernameStr;
				if (assetSelectJsonObj->TryGetStringField(FString("assetLineId"), assetLineIdStr) && assetSelectJsonObj->TryGetStringField(FString("username"), usernameStr))
				{
					FString ts = GetCurrentTimestamp();
					UE_LOG(LogPortalCLI, Log, TEXT("%s - Portal Username: %s"), *ts, *usernameStr);
					UE_LOG(LogPortalCLI, Log, TEXT("%s - Asset Line ID %s Selected"), *ts, *assetLineIdStr);
				}
			}


			FString assetLineJsonStr = FFIString(portal::output_message_to_json(selectAssetLineOutput.m_output_message)).ToFString();
			TSharedRef<TJsonReader<TCHAR>> assetLineResultJsonReader = TJsonReaderFactory<TCHAR>::Create(assetLineJsonStr);
			TSharedPtr<FJsonObject> assetLineResultJsonObj;
			if (FJsonSerializer::Deserialize(assetLineResultJsonReader, assetLineResultJsonObj) && assetLineResultJsonObj.IsValid())
			{
				FString assetLineId = assetLineResultJsonObj->GetStringField("assetLineId");
				FZeroLightMainButton::SetPortalAssetLineID_Static(FText::FromString(assetLineId));
	
				OutputMessage assetLineNameResponse = threadBardCli.m_client->client_get_asset_line(TCHAR_TO_ANSI(*FZeroLightMainButton::GetPortalAssetLineID()));

				FString assetLineJson = FFIString(portal::output_message_to_json(assetLineNameResponse.m_output_message)).ToFString();

				TSharedRef<TJsonReader<TCHAR>> assetLineJsonReader = TJsonReaderFactory<TCHAR>::Create(assetLineJson);
				TSharedPtr<FJsonObject> assetLineJsonObj;
				if (FJsonSerializer::Deserialize(assetLineJsonReader, assetLineJsonObj) && assetLineJsonObj.IsValid())
				{
					if (assetLineJsonObj->TryGetField(FString("data")) != nullptr)
					{
						FString displayName = assetLineJsonObj->GetObjectField(FString("data"))->GetStringField(FString("displayName"));
						FZeroLightMainButton::SetPortalDisplayName_Static(displayName);
						FString ts = GetCurrentTimestamp();
						UE_LOG(LogPortalCLI, Log, TEXT("%s - Asset Line Display Name: %s"), *ts, *displayName);
					}
					else
					{
						FZeroLightMainButton::SetProgressText(FText::FromString("Asset Line ID " + assetLineId + " not found"));
						assetLineNameResponse.print();
					}
				}
				else
				{
					FZeroLightMainButton::SetProgressText(FText::FromString("Asset Line ID " + assetLineId + " not found"));
					assetLineNameResponse.print();
				}
			}
		}
	}
	else
	{
		portal::client_open_select_asset_line_gui_result_destory(result);
		FZeroLightMainButton::SetProgressText(FText::FromString("Asset Line Selection Cancelled"));
	}

	FZeroLightMainButton::RefocusWindowOnRefresh();

	return 0;
};

void SelectAssetLineAsyncTask::Stop()
{
	if (m_abortHandle != nullptr)
	{
		portal::abort_handle_cancel_task(m_abortHandle);
		portal::abort_handle_destory_name(m_abortHandle);
		m_abortHandle = nullptr;
	}
}

void SelectAssetLineAsyncTask::Exit()
{
	FZeroLightMainButton::ClearSelectAssetLineTask();
}
#endif

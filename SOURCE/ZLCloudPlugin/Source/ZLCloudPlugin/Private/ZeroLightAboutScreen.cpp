// Copyright ZeroLight ltd. All Rights Reserved.
#if WITH_EDITOR

#include "ZeroLightAboutScreen.h"
#include "ZeroLightEditorStyle.h"
#include "Fonts/SlateFontInfo.h"
#include "DesktopPlatformModule.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "FileUtilities/ZipArchiveWriter.h"
#include "Misc/Paths.h"
#include "Misc/EngineVersion.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SWindow.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Application/SlateApplication.h"
#include "Textures/SlateIcon.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "Styling/CoreStyle.h"
#include "UnrealEdMisc.h"
#include "IDocumentation.h"
#include "Interfaces/IPluginManager.h"
#include <CloudStream2dll.h>
#include "CloudStream2.h"
#include <filesystem>
#include "PortalCLI.h"

#define LOCTEXT_NAMESPACE "ZeroLightAboutScreen"

static const TCHAR* ZLHelpURL = TEXT("https://zerolight.com/omnistream");

void SZeroLightAboutScreen::Construct(const FArguments& InArgs)
{
	FText Version = FText::Format( LOCTEXT("VersionLabel", "Version: {0}"), FText::FromString( FEngineVersion::Current().ToString( ) ) );

	ChildSlot
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			[
				SNew(SImage).Image(FSlateIcon("ZeroLightEditorStyle", "ZeroLightAboutScreen.Background").GetIcon())
			]

			+SOverlay::Slot()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(FMargin(10.f, 10.f, 0.f, 0.f))
				]
				+SVerticalBox::Slot()
				+SVerticalBox::Slot()
				.Padding(FMargin(5.f, 5.f, 5.f, 5.f))
				.VAlign(VAlign_Center)
				[
					SNew(SMultiLineEditableText)
					.IsReadOnly(true)
					.Text(GetZLVersionText())
				] 
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+SHorizontalBox::Slot()
					.FillWidth(4.0f)
					.HAlign(HAlign_Left)
					.Padding(FMargin(5.f, 0.f, 5.f, 5.f)) 
					[
						SNew(SButton)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Text(LOCTEXT("CopyToClipboard", "Copy To Clipboard"))
						.ButtonColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
						.OnClicked(this, &SZeroLightAboutScreen::OnCopyToClipboard)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(3.0f)
					.HAlign(HAlign_Left)
					.Padding(FMargin(5.f, 0.f, 0.f, 5.f))
					[
						SNew(SButton)
							.HAlign(HAlign_Right)
							.VAlign(VAlign_Center)
							.Text(LOCTEXT("GetLogs", "Get Logs"))
							.ButtonColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
							.OnClicked(this, &SZeroLightAboutScreen::OnGetLogsZip)
					]
					+SHorizontalBox::Slot()
					.FillWidth(3.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(SHyperlink)
						.Text(LOCTEXT("ZeroLightLinkText", "Get Support"))
						.ToolTipText(LOCTEXT("ZeroLightLinkToolTip", "Get support at ZeroLight.com"))
						.OnNavigate_Lambda([this]() { FPlatformProcess::LaunchURL(ZLHelpURL, nullptr, nullptr); })
					]
					+ SHorizontalBox::Slot()
					.FillWidth(3.0f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.Padding(2.0f, 0.0f, 0.0f, 0.0f)
					[
					SNew(SHyperlink)
					.Text(LOCTEXT("ZeroLightLinkText", "License"))
					.ToolTipText(LOCTEXT("ZeroLightLinkToolTip", "License"))
					.OnNavigate_Lambda([this]() { FPlatformProcess::LaunchFileInDefaultExternalApplication(*GetZLLicensePath()); })
					]
					+SHorizontalBox::Slot()
					.FillWidth(2.0f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					.Padding(FMargin(5.f, 0.f, 5.f,5.f))
					[
						SNew(SButton)
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Text(LOCTEXT("Close", "Close"))
						.ButtonColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f))
						.OnClicked(this, &SZeroLightAboutScreen::OnClose)
					]
				]
			]
		];

	
}

FString SZeroLightAboutScreen::GetZLLicensePath()
{
	FString LicenseString;


	IPluginManager& PluginManager = IPluginManager::Get();
	TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
	if (ZlCloudStreamPlugin)
	{
		LicenseString = ZlCloudStreamPlugin->GetBaseDir() + "/Source/ThirdParty/Licenses/OmniStream_License.txt";
	}

	return LicenseString;
}


FText SZeroLightAboutScreen::GetZLVersionText(bool bIncludeHeader)
{
	FString VersionString;

	if (bIncludeHeader)
	{
		VersionString.Append("\nOmniStream is provided by ZeroLight Ltd\n");
	}

	IPluginManager& PluginManager = IPluginManager::Get();
	TSharedPtr<IPlugin> ZlCloudStreamPlugin = PluginManager.FindPlugin("ZLCloudPlugin");
	if (ZlCloudStreamPlugin)
	{
		const FPluginDescriptor& Descriptor = ZlCloudStreamPlugin->GetDescriptor();
		VersionString.Append(FString::Printf(TEXT("ZLCloudStream Plugin Version: %s"), *Descriptor.VersionName));
	}

	int cs2_major = 0;
	int cs2_minor= 0;
	int cs2_rev = 0;
	int cs2_build = 0;

	bool freeCS2AfterPluginVerCheck = false;
	if (!ZLCloudPlugin::CloudStream2::IsPluginInitialised())
	{
		ZLCloudPlugin::CloudStream2::InitPlugin();
		freeCS2AfterPluginVerCheck = true;
	}

	CloudStream2DLL::GetPluginVersion(cs2_major, cs2_minor, cs2_rev, cs2_build);
	VersionString.Append(FString::Printf(TEXT("\nCloudStream2.dll: %d.%d.%d.%d"), cs2_major, cs2_minor, cs2_rev, cs2_build));

	if (freeCS2AfterPluginVerCheck)
		ZLCloudPlugin::CloudStream2::FreePlugin();

	FString portalVer = FString(portal::get_library_version().c_str);
	VersionString.Append(FString::Printf(TEXT("\nportal_client_c.dll: %s"), *portalVer));

	VersionString.Append(FString::Printf(TEXT("\nBuild Date: %s"), UTF8_TO_TCHAR(__DATE__)));

	return FText::FromString(VersionString);
}

FReply SZeroLightAboutScreen::OnCopyToClipboard()
{
	FPlatformApplicationMisc::ClipboardCopy(*GetZLVersionText(false).ToString());
	return FReply::Handled();
}

FReply SZeroLightAboutScreen::OnGetLogsZip()
{
	FString selectedFolderPath("");
	FString projectDir = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPaths::GetProjectFilePath()));

	if (!FDesktopPlatformModule::Get()->OpenDirectoryDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr), TEXT("Select destination to save logs ZIP"), projectDir, selectedFolderPath))
	{
		return FReply::Handled();
	}

	FString baseFolderName = TEXT("OmniStreamPluginLogs");
	FString zipFilePath = FPaths::Combine(selectedFolderPath, baseFolderName + ".zip");

	int32 id = 0;
	while (IFileManager::Get().FileExists(*zipFilePath))
	{
		id++;
		zipFilePath = FPaths::Combine(selectedFolderPath, baseFolderName + FString::Printf(TEXT("(%d)"), id) + ".zip");
	}

	IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();
	IFileHandle* zipFile = platformFile.OpenWrite(*zipFilePath);
	if (zipFile)
	{
		FZipArchiveWriter* zipWriter = new FZipArchiveWriter(zipFile);

		FString editorLogName = FApp::GetProjectName();
		editorLogName += ".log";

		FString editorLogPath = FPaths::Combine(projectDir, "Saved", "Logs", editorLogName); //ProjectDir/Saved/Logs/[ProjectName].log
		FString tempEditorLogPath = FPaths::Combine(selectedFolderPath, editorLogName);

		FString editorExecutablePath = FPaths::ConvertRelativePathToFull(FPaths::GetPath(FPlatformProcess::ExecutablePath()));
		FString portalLogPath = FPaths::Combine(editorExecutablePath, "portalcli.log"); //Example D:\Unreal\UE_5.3\Engine\Binaries\Win64
		FString tempPortalLogPath = FPaths::Combine(selectedFolderPath, "portalcli.log");

		auto copyAndZipLogFile = [](const FString logPath, const FString logDest, FZipArchiveWriter* zipWriter) {
			int attempts = 0;
			const int max_attempts = 5;
			bool copySuccess = false;

			std::filesystem::path copyFromPath = TCHAR_TO_UTF8(*logPath);
			std::filesystem::path copyToPath = TCHAR_TO_UTF8(*logDest);

			while (attempts < max_attempts && !copySuccess) {
				try {
					std::filesystem::copy_file(copyFromPath, copyToPath);
					copySuccess = true;
				}
				catch (const std::exception&) {
					attempts++;
					FPlatformProcess::Sleep(1.0f);
				}
			}

			if (copySuccess)
			{
				TArray<uint8> copyLogFileData;
				FFileHelper::LoadFileToArray(copyLogFileData, *logDest);

				zipWriter->AddFile(FPaths::GetCleanFilename(logDest), copyLogFileData, FDateTime::Now());
				return true;
			}
			else
				return false;
		};
 
		bool cleanupTempEditorLog = false, cleanupTempPortalLog = false;
		if (FPaths::FileExists(editorLogPath))
			cleanupTempEditorLog = copyAndZipLogFile(editorLogPath, tempEditorLogPath, zipWriter);

		if (FPaths::FileExists(portalLogPath))
			cleanupTempPortalLog = copyAndZipLogFile(portalLogPath, tempPortalLogPath, zipWriter);

		delete zipWriter;
		zipWriter = nullptr;

		if (cleanupTempEditorLog)
			std::filesystem::remove(TCHAR_TO_UTF8(*tempEditorLogPath));

		if(cleanupTempPortalLog)
			std::filesystem::remove(TCHAR_TO_UTF8(*tempPortalLogPath));

		FNotificationInfo Info(LOCTEXT("OmniStreamTaskSuccess_Notification", "OmniStream Log ZIP Saved"));

		Info.ExpireDuration = 10.0f;
		Info.bUseLargeFont = false;
		Info.bUseSuccessFailIcons = true;

		FSlateNotificationManager::Get().AddNotification(Info)->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
		FPlatformProcess::ExploreFolder(*selectedFolderPath);
	}

	return FReply::Handled();
}

FReply SZeroLightAboutScreen::OnClose()
{
	TSharedRef<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow( AsShared() ).ToSharedRef();
	FSlateApplication::Get().RequestDestroyWindow( ParentWindow );
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE

#endif

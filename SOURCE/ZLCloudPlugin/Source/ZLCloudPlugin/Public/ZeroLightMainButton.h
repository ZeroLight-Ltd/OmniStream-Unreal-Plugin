// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "ZLCloudPluginVersion.h"
#include "Widgets/SWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Textures/SlateIcon.h"
#include "Types/SlateEnums.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Misc/MonitoredProcess.h"
#include "Interfaces/IPluginManager.h"
#include <ctime>
#include <regex>
#include "EditorZLCloudPluginSettings.h"
#include "CloudStream2.h"

#if WITH_EDITOR

#include "PortalCLI.h"
#include "CoreMinimal.h"
#include "Async/Async.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/ThreadSafeCounter.h"
#include "HAL/FileManager.h"


DECLARE_LOG_CATEGORY_EXTERN(LogPortalCLI, Log, All);

#include "CoreMinimal.h"

class portalcli
{
public:
	enum AsyncPortalTaskType { UPLOAD, GETTOKEN, SELECTASSETLINE };

	portalcli();
	portalcli(portal::BuildContext* bc);

	void AsyncPortalTask(AsyncPortalTaskType taskType, bool triggerBuildTask = false, bool triggerExistingBuildDeploy = false, FString targetPath = "");
	void CreateDllPortalClient();
	void CreateDllPortalClient(portal::BuildContext* bc);
	bool ClearCurrentBuild();
	bool ReportBuildFailure();
	void StartOutputReadTask();
	void StopOutputReadTask();
	OutputMessage ForceLogout();

	TSharedPtr<PortalClient, ESPMode::ThreadSafe> m_client = nullptr;
	
private:

	FString s_currentBuildIdent;

	FThreadSafeBool m_isRunningReadOutputTask;
	FThreadSafeBool s_isCurrentlyUploading = false;
	FCriticalSection m_readOutputTaskLock;
	
	TSharedPtr<ProgressStream> m_ps;
};

class GetTokenAsyncTask : public FRunnable
{
public:
	GetTokenAsyncTask(portal::BuildContext* bc, bool triggerBuildProcess, bool triggerExistingBuildDeploy, FString targetPath)
	{
		buildContext = bc;
		triggerBuild = triggerBuildProcess;
		triggerExistingDeploy = triggerExistingBuildDeploy;
		downstreamJobTargetPath = targetPath;

		Thread = FRunnableThread::Create(this, TEXT("Portal GetToken Task Thread"));
	};

	//override Init,Run and Stop.
	inline virtual bool Init() override
	{

		return true;
	};

	virtual uint32 Run() override;

	inline virtual void Exit() override;

	inline virtual void Stop() override;

	portal::BuildContext* buildContext;

private:

	bool triggerBuild = false;
	bool triggerExistingDeploy = false;
	FString downstreamJobTargetPath = "";
	FRunnableThread* Thread;
	portal::EnsureTokenTask* m_tokenTask = nullptr;
	portal::AbortHandle* m_abortHandle = nullptr;
};

class UploadAsyncTask : public FRunnable
{
public:
	UploadAsyncTask(portal::BuildContext* bc, FString uploadFolderPath, FString name, FString assetLineId, bool ciUpload, FString storageProvider = "", FString metadata = "")
	{
		buildContext = bc;
		srcPath = uploadFolderPath;
		deployName = name;
		Thread = FRunnableThread::Create(this, TEXT("Portal Upload Task Thread"));
		assetLineID = assetLineId;
		portalStorageProvider = storageProvider;
		buildMetadata = metadata;
		isCIUpload = ciUpload;
	};

	//override Init,Run and Stop.
	inline virtual bool Init() override
	{

		return true;
	};

	virtual uint32 Run() override;

	inline virtual void Exit() override;

	inline virtual void Stop() override;

	FText GetUploadEndedProgressText();

	portal::BuildContext* buildContext;

private:

	FRunnableThread* Thread;
	portal::OneStepUploadTask* m_oneStepPortalUploadTask = nullptr;
	portal::UploadTask* m_uploadTask = nullptr;
	portal::EnsureTokenTask* m_tokenTask = nullptr;
	portal::AbortHandle* m_abortHandle = nullptr;
	FString deployName;
	FString srcPath;
	FString assetLineID;
	FString portalStorageProvider;
	FString buildMetadata;
	bool isCIUpload = false;
};

class SelectAssetLineAsyncTask : public FRunnable
{
public:
	SelectAssetLineAsyncTask(portal::BuildContext* bc)
	{
		buildContext = bc;
		Thread = FRunnableThread::Create(this, TEXT("Portal Select Asset Line Task Thread"));
	};

	//override Init,Run and Stop.
	inline virtual bool Init() override
	{

		return true;
	};

	virtual uint32 Run() override;

	inline virtual void Exit() override;

	inline virtual void Stop() override;

	portal::BuildContext* buildContext;

private:

	FRunnableThread* Thread;
	portal::OpenSelectAssetLineGuiTask* m_tokenTask = nullptr;
	portal::AbortHandle* m_abortHandle = nullptr;
};

class FZeroLightMainButton: public TSharedFromThis<FZeroLightMainButton>
{
public:
	inline static FZeroLightMainButton* s_CIMainButtonPtr;
	FZeroLightMainButton();
	~FZeroLightMainButton();

	void Initialize();

	static TSharedPtr<SHorizontalBox> GetUserBox();

	void CheckForNewerPlugin();

	void About();
	void ShowBuildAndDeployDialog();
	void ShowPluginSettingsWindow();
	void ShowStateManagementWindow();
	FReply CI_TriggerBuildAndDeploy(FString buildPath, bool portalUpload);
	bool CI_HasValidBuildSettingsAndAuthorised(bool portalUpload);
	TSharedPtr<FJsonObject> AggregatePredictedJSONSchema() const;
	inline static void CI_StartUATBuildTask() { if (s_isCIBuild && s_CIMainButtonPtr != nullptr) s_CIMainButtonPtr->StartBuildAndDeployTask(); }
	inline static bool s_triggerBuildTask = false;
	inline static bool s_triggerBuildUpload = false;
	inline static bool s_isCIBuild = false;

	static void SetProgressText(const FText& text);
	static void SetIsBuilding(bool state);
	static void SetIsPortalAuthorised(bool state);
	static void SetPortalUsername(FString username);
	static void SetBuildFolder(const FString& path);
	inline static void SetPortalAssetLineID_Static(const FText& id) {
		if (s_cloudstreamSettings != nullptr)
		{
			s_cloudstreamSettings->portalAssetLineId = id.ToString();
			s_cloudstreamSettings->buildId = id.ToString();
			s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
			s_cloudstreamSettings->SaveToCustomIni();
		}
	}
	inline static void SetPortalDisplayName_Static(const FString& name) {
		if (s_cloudstreamSettings != nullptr)
		{
			s_cloudstreamSettings->displayName = name;
			s_cloudstreamSettings->SaveConfig(NULL, *s_savedIniPath);
			s_cloudstreamSettings->SaveToCustomIni();
		}
	};
	inline bool IsCodeProject() const
	{
		//Same logic as used by internal module (FMainFrameActionCallbacks)
		const bool bIsCodeProject = IFileManager::Get().DirectoryExists(*FPaths::GameSourceDir());
		return bIsCodeProject;
	};
	inline bool IsPixelStreamingEnabled()
	{
		bool bIsPixelStreamingEnabled = false;

		FModuleManager& ModuleManager = FModuleManager::Get();

		bIsPixelStreamingEnabled = ModuleManager.IsModuleLoaded(TEXT("PixelStreaming"));

		return bIsPixelStreamingEnabled;
	};
	inline bool IsPixelCaptureEnabled()
	{
		bool bIsPixelCaptureEnabled = false;

		FModuleManager& ModuleManager = FModuleManager::Get();

		bIsPixelCaptureEnabled = ModuleManager.IsModuleLoaded(TEXT("PixelCapture"));

		return bIsPixelCaptureEnabled;
	};

	inline bool NeedsOpenXREnabling()
	{
		if (s_cloudstreamSettings)
		{
			if (!s_cloudstreamSettings->supportsVR)
				return false;
			else
			{
				bool bIsOpenXREnabled = false;
				bool bIsOpenXREnabled2 = false;

				// Get a reference to the module manager
				FModuleManager& ModuleManager = FModuleManager::Get();

				bIsOpenXREnabled = ModuleManager.IsModuleLoaded(TEXT("OpenXR"));
				bIsOpenXREnabled2 = ModuleManager.IsModuleLoaded(TEXT("OpenXRHMD"));

				return !(bIsOpenXREnabled || bIsOpenXREnabled2);
			}
		}
		return false;
	};

	inline bool ProjectHasWarnings()
	{
		return /*!IsCodeProject() ||*/ IsPixelStreamingEnabled() || NeedsOpenXREnabling() || !IsPixelCaptureEnabled();
	}
	FReply AutoResolveProjectWarnings();
	void TogglePluginEnabled(const FString& PluginName, bool bEnable);
	bool NewerGithubPluginAvailable();
	void SetPortalAssetLineID(const FText& id);
	static FString GetBuildFolder();
	void StartBuildAndDeployTask() const;
	void StartExistingBuildDeployTask() const;
	inline static void TriggerBuild() { s_triggerBuildTask = true; };
	inline static void TriggerExistingBuildDeploy() { s_triggerExistingBuildUploadTask = true; };
	bool TriggerBuildUpload(FString retryPath = "") const;
	bool RetryRenameUploadExistingBuild() const;
	static bool RestoreToBuildDirectory();
	inline static void SetRestorePaths(FString currentPath, FString restorePath, FString pathToCleanup) 
	{
		s_restoreOrigPath = currentPath;
		s_restorePath = restorePath;
		s_restorePathToCleanup = pathToCleanup;
	}
	inline static void SetRetryFailedRenameBuildPath(FString origBuildPath)
	{
		s_retryRenameBuildPath = origBuildPath;
	}

	inline static FString const GetPortalStorageProvider() { return (s_devFeaturesEnabled) ? s_portalStorageProvider : ""; }
	inline static FString GetDeployName() { return s_cloudstreamSettings->deployName; };
	inline static FString GetProjectName() { return s_projectName; };
	inline static FString GetBuildMetadata() { return s_buildMetadataStr; }
	inline static FString GetBuildId() { return s_cloudstreamSettings->buildId; };
	inline static FString GetUploadDirectory() { return s_uploadDirectory; };
	inline static FString GetPortalAssetLineID() { return s_cloudstreamSettings->portalAssetLineId; }
	inline static FString GetPortalServerUrl() { return s_cloudstreamSettings->portalServerUrl; }
	inline static FString GetHttpProxyOverride() { return s_cloudstreamSettings->httpProxyOverride; }
	inline static FString GetThumbnailFilePath() { return s_cloudstreamSettings->thumbnailImagePath.FilePath; }
	inline static FString GetCurrentUploadAssetVersionId() { return s_currentAssetVersionId; }
	inline static void SetCurrentUploadAssetVersionId(FString assetVersionId) { s_currentAssetVersionId = assetVersionId; }
	inline static void RefocusWindowOnRefresh() { s_needsRefocusWindow = true; }
	inline static FString FormatFStringFilenameSafe(FString str)
	{
		std::string inStr(TCHAR_TO_UTF8(*str));
		// Regular expression pattern to match invalid characters
		// Modify the pattern to suit your specific requirements
		std::regex invalidCharacterPattern("[<>:\"/\\|?*]");

		// Remove invalid characters
		std::string cleanedFilename = std::regex_replace(inStr, invalidCharacterPattern, "");

		// Remove spaces
		cleanedFilename.erase(std::remove(cleanedFilename.begin(), cleanedFilename.end(), ' '), cleanedFilename.end());

		return FString(cleanedFilename.c_str());
	}
	inline static TSharedRef<SWidget> GeneratePortalStorageProviderWidget(TSharedPtr<FString> InItem){ return SNew(STextBlock).Text(FText::FromString(*InItem)); }
	inline static void SetAuthTask(TSharedPtr<GetTokenAsyncTask> task) { s_getTokenAsyncTask = task; };
	inline static void SetSelectAssetLineTask(TSharedPtr<SelectAssetLineAsyncTask> task) { s_selectAssetLineAsyncTask = task; };
	inline static void SetUploadTask(TSharedPtr<UploadAsyncTask> task) { s_uploadAsyncTask = task; };
	inline static void SetActivePortalBuildRef(FString buildRef) { s_activePortalBuildRef = buildRef; }
	inline static void SetLastSucessPortalBuildRef(FString buildRef) { s_lastSucessPortalBuildRef = buildRef; }
	inline static void SetLastSucessPortalBuildRef() { s_lastSucessPortalBuildRef = s_activePortalBuildRef; s_activePortalBuildRef = ""; }
	inline static void ClearAuthTask() 
	{ 
		if (s_getTokenAsyncTask != nullptr)
		{
			s_getTokenAsyncTask.Reset();
			s_getTokenAsyncTask = nullptr;
		}
	};

	inline static void ClearUploadTask()
	{
		if (s_uploadAsyncTask != nullptr)
		{
			s_uploadAsyncTask.Reset();
			s_uploadAsyncTask = nullptr;
		}
	};
	inline static void ClearSelectAssetLineTask()
	{
		if (s_selectAssetLineAsyncTask != nullptr)
		{
			s_selectAssetLineAsyncTask.Reset();
			s_selectAssetLineAsyncTask = nullptr;
		}
	};
	inline void ClearBuildWindowRef() { BuildAndDeployWnd = nullptr; };
	inline void ClearStateManagementWindowRef() { StateManagementWnd = nullptr; };

	inline static bool newerPluginAvailable = false;
	inline static bool s_userCancelledUpload = false;
	inline static bool s_userPausedUpload = false;
	inline static bool s_removeDebugSymbols = true;
	inline static FString s_pausedUploadPath = "";
	inline static FString s_restoreOrigPath = "";
	inline static FString s_restorePath = "";
	inline static FString s_restorePathToCleanup = "";
	inline static FString s_retryRenameBuildPath = "";
	inline static FString s_savedIniPath = "";


protected:
	FText GetTooltip();
	FText GetButtonText();
	FText GetCurrentProgressText() const;
	FText GetDeployButtonText() const;
	FText UpdateProgress() const;
	FText GetLastSucessBuildRef() const;
	ECheckBoxState UseExistingBuild() const;
	ECheckBoxState UseRemoveDebugSymbols() const;
	ECheckBoxState UseExistingRunInfo() const;
	FSlateIcon GetButtonIcon();
	TSharedRef<SWidget> GetMenu();
	void SetDeployName(const FText& name);
	void SetPortalDisplayName(const FString& name);
	void SetBuildMetadata(const FString& metadata);
	void SetDeployName(const FString& name);
	void SetBuildID(const FText& id);
	void SetUseExistingBuild(const ECheckBoxState state);
	void SetUseExistingRunInfoJson(const ECheckBoxState state);
	void SetRemoveDebugSymbols(const ECheckBoxState state);
	inline static ECheckBoxState GetRemoveDebugSymbols() { return s_removeDebugSymbols ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; };
	void SetUsePortalUpload(const ECheckBoxState state);
	void SetPortalStorageProvider(TSharedPtr<FString> NewValue, ESelectInfo::Type SelectInfo);
	bool OnBuildPackageSuccess() const;
	FReply AbortCurrentProcessOrReauth();
	FReply PauseOrResumeUpload();
	FReply SelectAssetLine();
	FReply ForcePortalLogout();
	inline bool IsPortalUserLoggedIn() { return s_portalUsername != "Not Logged In"; };
	FReply OpenFAQDocsWebPage();
	FReply OpenGithubReleaseWebPage();
	FString FindOverrideExeName(FString buildDirectory) const;
	FString FindOverrideProjectName(FString buildDirectory) const;
	bool AddRunInfoJSONAndMetadataToBuild(FString outputFolderPath, FString overrideProjectName, FString overrideExeName = "") const;
	bool AddCloudPluginConfigToBuild(FString outputFolderPath) const;
	bool AddRunMode(FString runMode, FString args, TSharedPtr<FJsonObject> jsonObj, FString streamingSwitch = "") const;
	bool PortalUploadAndZip(FString outputFolderPath) const;
	FReply TriggerBuildAndDeploy(FString buildFolderOverride = "");
	
	
	inline static FString githubVersion = "0.0.0";
	//Accessible UI elements
	TSharedPtr<SWindow> BuildAndDeployWnd;
	TSharedPtr<SWindow> StateManagementWnd;
	TSharedPtr<portalcli, ESPMode::ThreadSafe> portalCLI;
	 
	//Data members
	UPROPERTY()
	inline static UZLCloudPluginSettings* s_cloudstreamSettings = nullptr;
	inline static FString s_buildMetadataStr = "";
	inline static FText s_progressText;
	inline static bool s_isCurrentlyBuilding = false;
	inline static bool s_isPortalAuthorised = false;
	inline static bool s_needsRefocusWindow = false;
	inline static bool s_useExisting = false;
	inline static bool s_useExistingRunInfo = false;
	inline static bool s_devFeaturesEnabled = false;

	//Rust bard dll interface seems to have issues with its own pointers when accessed on another thread, so we use flagging to trigger these calls in the main thread
	//Will need rust-side improvement for upload concurrency
	inline static bool s_sendBuildFailed = false;
	inline static bool s_triggerExistingBuildUploadTask = false;
	inline static FString s_invalidIDStr = "Error: Invalid AppName/ID (characters must be alphanumeric/whitespace)";
	inline static FString s_uploadDirectory;
	inline static FString s_projectName = "OmniStream"; //This will be replaced with a dropdown of available options after auth rework
	inline static FString s_lastSucessPortalBuildRef = "";
	inline static FString s_activePortalBuildRef = "";
	inline static FString s_portalStorageProvider = "s3";
	inline static FString s_portalUsername = "Not Logged In";
	inline static FString s_currentAssetVersionId = "";
	inline static TArray<TSharedPtr<FString>> s_portalStorageProviderOptions = {MakeShared<FString>("bard"), MakeShared<FString>("s3"), MakeShared<FString>("bard-zip")};

	inline static TSharedPtr<GetTokenAsyncTask> s_getTokenAsyncTask = nullptr;
	inline static TSharedPtr<SelectAssetLineAsyncTask> s_selectAssetLineAsyncTask = nullptr;
	inline static TSharedPtr<UploadAsyncTask> s_uploadAsyncTask = nullptr;
};

#endif

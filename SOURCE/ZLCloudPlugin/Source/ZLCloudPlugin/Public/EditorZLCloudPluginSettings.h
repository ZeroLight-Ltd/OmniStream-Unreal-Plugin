// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#if WITH_EDITOR
#include "IDetailCustomization.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailPropertyRow.h"
#include "IPropertyTypeCustomization.h"
#include "Widgets/Images/SImage.h"
#include "DetailWidgetRow.h"
#include "Editor/UnrealEd/Public/EditorDirectories.h"
#include "HAL/FileManager.h"
#include "Slate/SlateBrushAsset.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Slate/SlateTextureAtlasInterface.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#endif

#include "EditorZLCloudPluginSettings.generated.h"

// Config loaded/saved to an .ini file.
// It is also exposed through the plugin settings page in editor.
UCLASS(config = ZLCloudPluginSettings, meta = (DisplayName = "Settings"))
class ZLCLOUDPLUGIN_API UZLCloudPluginSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * Defines the startup GameMode asset where ZLCloudPlugin blueprint nodes are used.
	 * This searches the blueprint in the editor and appends metadata used for determining app capabilities/startup behaviours
	 */
	//UPROPERTY(EditAnywhere, config, Category = "")
	//TSoftObjectPtr<UBlueprint> StartupGameModeAsset;

	/**
	 * ZLCloudStream may be running on a machine which has no physical mouse attached, and yet the browser is sending mouse positions. 
	 * As such, we fake the presence of a mouse.
	 */
	UPROPERTY(config, EditAnywhere, Category = Input)
	bool bMouseAlwaysAttached = true;

	/**
	* Keys you want to allow the browser to pass to your application (separated by a comma).
	* Use ZeroLight > Tools > Input Key Filter to edit or record keys, or https://keycode.info/ for codes.
	* Default: Q, W, E, A, S, D, I, U, O, J, K, L, arrows, Space
	*/
	UPROPERTY(config, EditAnywhere, Category = Input)
	FString filteredKeyList = TEXT("81,87,69,65,83,68,73,85,79,74,75,76,38,37,40,39,32");

	TSet<FKey> FilteredKeys;
	void UpdateFilteredKeys();


	UPROPERTY(config, EditAnywhere, Category = Performance)
	int FramesPerSecond = 30;

	/**
	 * Delay app allowing stream adoption until after the 'Set App Ready to Stream' node is triggered in Game Mode blueprint. 
	 * 
	 * IMPORTANT: If enabled, this node MUST be called to allow cloud streaming to begin.
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud)
	bool DelayAppReadyToStream = false;

	/**
	 * Restart the app on user disconnect, used to reset state (better to reset state on disconnect event)
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud)
	bool bRebootAppOnDisconnect = false;

	/**
	 * Disable texture streaming in the app launch run arguments (to prevent texture's visibly loading in stream)
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud)
	bool bDisableTextureStreamingOnLaunch = false;

	/**
	 * When a state request is processing for more than the specified value (in seconds) a warning with processing info is logged
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud)
	float stateRequestWarningTime = 10.0;

	/**
	 * When a state request is processing for more than the specified value (in seconds) the request times out and reports a timeout to the web
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud, meta = (ClampMin = "10.0", UIMin = "10.0"))
	float stateRequestTimeout = 20.0;

	/**
	 * Timeout (in ms) for blocking on the render fence before continuing.
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud, meta = (ClampMin = "25000", ClampMax = "360000", UIMin = "25000", UIMax = "360000"))
	int timeoutForBlockOnRenderFence = 25000;

	/**
	 * Initial heartbeat timeout (in seconds) used for session startup. If left at default, this is omitted from runInfo.
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud, meta = (ClampMin = "10.0", UIMin = "10.0"))
	float initialHeartBeatTimeout = 260.0f;

	/**
	 * Heartbeat timeout (in seconds) used after startup. If left at default, this is omitted from runInfo.
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud, meta = (ClampMin = "10.0", UIMin = "10.0"))
	float heartBeatTimeout = 100.0f;

	/**
	 * Project override for the number of frames waited before capturing content generation screenshot jobs after state is matched. If set to 0, plugin defaults are used (8 frames by default, 32 frames by default for projects with r.RayTracing enabled.)
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud)
	int screenshotFrameWaitCountOverride = 0;

	/**
	 * Extra ticked frames to wait after an environment switch in HighResScreenshot jobs, after flushing streaming. If set to 0, the settle wait is skipped.
	 */
	UPROPERTY(config, EditAnywhere, Category = Cloud, meta = (ClampMin = "0", UIMin = "0"))
	int environmentSettleFrames = 0;

	/**
	 * Allows support for running the app in CloudXR mode. Requires OpenXR plugin to be enabled.
	 */
	UPROPERTY(config, EditAnywhere, Category = Engine)
	bool supportsVR = false;

	/**
	 * The app type for portal deployment.
	 */
	UPROPERTY(config, VisibleDefaultsOnly, Category = BuildAndDeploy)
	FString deployName;

	/**
	* The display name associated with the asset-line ID for deployment.
	*/
	UPROPERTY(config, VisibleDefaultsOnly, Category = BuildAndDeploy)
	FString displayName;

	/**
	* User assigned build ID for deployment structures
	*/
	UPROPERTY(config, VisibleDefaultsOnly, Category = BuildAndDeploy)
	FString buildId = "0000";


	/**
	* The directory this project has been built to
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy)
	FString buildFolder;

	/**
	* Autogenerated Portal asset line ID this project will upload to
	*/
	UPROPERTY(config, VisibleDefaultsOnly, Category = BuildAndDeploy)
	FString portalAssetLineId = "";

	/**
	* Server url for portal uploads. Readonly in editor, can be overriden in ini file
	*/
	UPROPERTY(config, VisibleDefaultsOnly, Category = BuildAndDeploy)
	FString portalServerUrl = "https://portal.zerolight.com";

	/**
	* Sets a manual override for specifying the http proxy used. Portal attempts auto detection, if set then this field will override automatic detection.
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy)
	FString httpProxyOverride = "";

	/**
	* Optional command line parameters appended to the run args in run_info.json for both Cloud and CloudXR run modes (e.g. "-LOG=unreal_log.txt").
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy)
	FString runInfoCommandLineParams = "";

	/**
	* Build configuration when building: "Development" or "Shipping". Default is Development.
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy)
	FString buildConfiguration = "Development";

	/**
	* Downloads FFmpeg when enabled, used for media on demand video generation.
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy, meta = (DisplayName = "Download ffmpeg for media on demand videos"))
	bool bDownloadFFmpegForMediaOnDemandVideos = false;

	/**
	* When disabled, Movie Render Queue jobs never apply the cinematic quality game overrides.
	*/
	UPROPERTY(config, EditAnywhere, Category = BuildAndDeploy, meta = (DisplayName = "Allow cinematic quality overrides in media on demand videos", EditCondition = "bDownloadFFmpegForMediaOnDemandVideos", EditConditionHides))
	bool bAllowCinematicQualityOverridesInMediaOnDemandVideos = true;

	/**
	* Specifies a filepath on-disk to a jpeg/png image that will be used as the thumbnail for the project in Portal
	*/
	UPROPERTY(config, EditAnywhere, Category = "Thumbnail", meta = (FilePathFilter = "Image Files (*.jpg, *.png, *.jpeg)|*.jpg;*.png;*.jpeg"))
	FFilePath thumbnailImagePath;

	/**
	 * Enables beta + experimental features in OmniStream
	 */
	UPROPERTY(config, EditAnywhere, Category = Experimental)
	bool showExperimentalFeatures = false;

	FString GetDeployName()
	{
		return deployName;
	};

	FString GetBuildID()
	{
		return buildId;
	};

	FString GetHttpProxyOverride()
	{
		return httpProxyOverride;
	};

	FString GetThumbnailFilePath()
	{
		return thumbnailImagePath.FilePath;
	}

	/**
	* Resolves a path stored in DefaultZLCloudPluginSettings.ini to an absolute path.
	* Relative values are always resolved against the project directory - FPaths resolves them against the
	* engine binaries directory, which corrupts the stored value a little more on every save.
	*/
	static FString ResolveStoredPath(const FString& storedPath);

	/**
	* Converts a path to the form written to DefaultZLCloudPluginSettings.ini. Project relative when that
	* resolves back to the same location on disk, absolute otherwise.
	*/
	static FString MakeStoredPath(const FString& path);

	/**
	* True when a stored path can no longer describe a real location, e.g. the repeated directory names left
	* behind when a relative path has been resolved against the wrong base directory repeatedly.
	*/
	static bool IsStoredPathCorrupt(const FString& storedPath);

	/**
	* Validates buildFolder and thumbnailImagePath after load, resetting them to defaults when corrupt.
	*/
	void SanitizePathSettings();

	// Begin UDeveloperSettings Interface
	virtual FName GetCategoryName() const override;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	// END UDeveloperSettings Interface

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	void SaveToCustomIni();

	void SaveConfig(uint64 Flags = CPF_Config, const TCHAR* Filename = nullptr, FConfigCacheIni* Config = GConfig)
	{
		SaveToCustomIni();
	}
#endif
};

#if WITH_EDITOR
class FZLCloudPluginSettingsCustomization : public IDetailCustomization
{
private:
	FString lastLoadedThumbnailPath;
	FSlateBrush ThumbnailImageBrush;
	TSharedPtr<SImage> ThumbnailImageWidget;
	TSharedPtr<IPropertyHandle> ThumbnailPathProperty;

public:
	inline static TSharedRef<IDetailCustomization> MakeInstance()
	{
		return MakeShareable(new FZLCloudPluginSettingsCustomization);
	}

	static void OpenFilteredKeysEditor();

	inline void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
	{
		TSharedRef<IPropertyHandle> FilteredKeysHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, filteredKeyList));
		if (IDetailPropertyRow* FilteredKeysRow = DetailBuilder.EditDefaultProperty(FilteredKeysHandle))
		{
			FilteredKeysRow->CustomWidget()
				.NameContent()
				[
					FilteredKeysHandle->CreatePropertyNameWidget()
				]
				.ValueContent()
				.MinDesiredWidth(320.0f)
				.MaxDesiredWidth(600.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						FilteredKeysHandle->CreatePropertyValueWidget()
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(6.0f, 0.0f, 0.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.Text(FText::FromString("Open Editor"))
						.ToolTipText(FText::FromString("Open the Input Key Filter to record, toggle, or edit allowed keys"))
						.OnClicked_Lambda([]()
						{
							FZLCloudPluginSettingsCustomization::OpenFilteredKeysEditor();
							return FReply::Handled();
						})
					]
				];
		}

		IDetailCategoryBuilder& ThumbnailCategory = DetailBuilder.EditCategory("Thumbnail");

		ThumbnailPathProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UZLCloudPluginSettings, thumbnailImagePath));

		ThumbnailCategory.AddCustomRow(FText::FromString("Thumbnail"))
			.NameContent()
			[
				SNew(STextBlock).Text(FText::FromString("Thumbnail Image"))
			]
			.ValueContent()
			.MinDesiredWidth(64.0f)
			.MaxDesiredWidth(512.0f)
			[
				SNew(SBox)
				.MinDesiredHeight(64.0f)
				.MinDesiredWidth(64.0f)
				.MaxDesiredHeight(512.0f)
				.MaxDesiredWidth(512.0f)
				[
					SAssignNew(ThumbnailImageWidget, SImage)
					.Image(this, &FZLCloudPluginSettingsCustomization::GetThumbnailBrush)
				]
			];

	}

	inline const FSlateBrush* GetThumbnailBrush() const
	{
		FSlateBrush* tempBrushPtr = const_cast<FZLCloudPluginSettingsCustomization*>(this)->UpdateThumbnailBrush(); // Cast away constness 
		return (tempBrushPtr == nullptr) ? nullptr : &ThumbnailImageBrush;
	}

	inline FSlateBrush* UpdateThumbnailBrush()
	{
		if (ThumbnailPathProperty.IsValid())
		{
			TArray<void*> RawDataPtrs;
			ThumbnailPathProperty->AccessRawData(RawDataPtrs);

			if (RawDataPtrs.IsValidIndex(0))
			{
				FFilePath* imageFilePath = static_cast<FFilePath*>(RawDataPtrs[0]);

				FString thumbnailImagePath = UZLCloudPluginSettings::ResolveStoredPath(imageFilePath->FilePath);

				if (thumbnailImagePath == lastLoadedThumbnailPath)
				{
					return (lastLoadedThumbnailPath.IsEmpty() || !FPaths::FileExists(lastLoadedThumbnailPath)) ? nullptr : &ThumbnailImageBrush;
				}

				lastLoadedThumbnailPath = imageFilePath->FilePath;

				if (thumbnailImagePath.IsEmpty() || !FPaths::FileExists(thumbnailImagePath))
				{
					return nullptr;
				}

				UTexture2D* LoadedTexture = FImageUtils::ImportFileAsTexture2D(thumbnailImagePath);
				if (LoadedTexture)
				{
					FSlateBrush* newThumbnailBrush = new FSlateBrush();

					newThumbnailBrush->SetResourceObject(LoadedTexture);

					if (ThumbnailImageWidget.IsValid())
					{
						int desiredWidth = LoadedTexture->GetSizeX(), desiredHeight = LoadedTexture->GetSizeY();
						int maxDim = 512;

						while (desiredWidth > maxDim || desiredHeight > maxDim)
						{
							desiredWidth /= 2;
							desiredHeight /= 2;
						}
						
						ThumbnailImageWidget->SetDesiredSizeOverride(FVector2D(desiredWidth, desiredHeight));
					}

					ThumbnailImageBrush = *newThumbnailBrush;
					return newThumbnailBrush;
				}
			}
		}

		return nullptr;
	}
};
#endif
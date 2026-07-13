// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ZLOmniStreamAutoPopulateLibrary.generated.h"

USTRUCT(BlueprintType)
struct FZLAutoPopulateAdvancedCheckboxDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString Id;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    bool bDefaultChecked = false;
};

USTRUCT(BlueprintType)
struct FZLAutoPopulateAdvancedLevelSelectorDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString Id;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString RequiredActorClassPath;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString RequiredActorComponentClassPath;
};

USTRUCT(BlueprintType)
struct FZLAutoPopulateAdvancedOptionDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString OptionName;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString FoldoutTitle;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    TArray<FZLAutoPopulateAdvancedCheckboxDesc> Checkboxes;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    TArray<FZLAutoPopulateAdvancedLevelSelectorDesc> LevelSelectors;
};

/** Descriptor for a single auto-populate plugin/module exposed via
 *  IZLOmniStream_SchemaAutoPopulate. Mirrors the data Python needs to
 *  render one foldout per implementation in the advanced auto-populate window. */
USTRUCT(BlueprintType)
struct FZLAutoPopulateModuleDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString PluginName;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    TArray<FString> OptionNames;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    TArray<FZLAutoPopulateAdvancedOptionDesc> AdvancedOptionDescs;
};

USTRUCT(BlueprintType)
struct FZLAutoPopulateLevelActorDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString ActorDisplayName;

    UPROPERTY(BlueprintReadOnly, Category = "ZL OmniStream Auto-Populate")
    FString LevelPackageName;
};

/**
 * Static helpers exposed to Python for driving the OmniStream schema-editor
 * advanced auto-populate window. All methods are no-ops outside of the
 * editor environment.
 */
UCLASS()
class ZLCLOUDPLUGIN_API UZLOmniStreamAutoPopulateLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Returns one descriptor per registered IZLOmniStream_SchemaAutoPopulate implementation. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static TArray<FZLAutoPopulateModuleDesc> GetAutoPopulateModules();

    /** Returns the auto-populate option strings persisted for the advanced auto-populate window. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static TArray<FString> GetCurrentlySelectedOptions();

    /** Persists the auto-populate option selection from the advanced auto-populate window. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool SetSelectedAutoPopulateOptions(const TArray<FString>& SelectedOptions);

    /** True when a FZLStateEditorV2 instance is currently constructed. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool IsSchemaEditorOpen();

    /** Invokes auto-populate on the live V2 schema editor with the given selections, levels and additive flag.
     *  When LevelPackageNames is empty the current editor world is used. Returns false when no editor is open. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool RunAutoPopulate(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive);

    /** Advanced-settings aware variant. AdvancedSettingsJson format:
     *  {"OptionName":{"checkboxes":{"id":true},"levelSelections":{"selectorId":["/Game/MyLevel"]},"actorSelections":{"selectorId":["ModelA"]}}} */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool RunAutoPopulateWithSettings(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, const FString& AdvancedSettingsJson);

    /** Returns the currently loaded schema path from the live V2 editor instance. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static FString GetLoadedSchemaPath();

    /** Clears any staged preview state used by advanced auto-populate confirm flow. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool ClearAutoPopulatePreview();

    /** Builds a non-mutating preview and returns JSON-Schema-compliant text in OutProposedSchemaText. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool PreviewAutoPopulate(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, FString& OutProposedSchemaText);

    /** Advanced-settings aware preview variant. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool PreviewAutoPopulateWithSettings(const TArray<FString>& SelectedOptions, const TArray<FString>& LevelPackageNames, bool bAdditive, const FString& AdvancedSettingsJson, FString& OutProposedSchemaText);

    /** Applies the currently staged preview to the live editor state. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool ApplyAutoPopulatePreview();

    /** Returns staged preview schema text produced by PreviewAutoPopulate (if available). */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static FString GetPendingPreviewSchemaText();

    /** True when ZLEditorTools is available in this build/project. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static bool IsZLEditorToolsAvailable();

    /** Returns levels that contain at least one actor matching the provided class filters. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static TArray<FString> FilterLevelsByClassRequirements(
        const TArray<FString>& CandidateLevelPackageNames,
        const FString& RequiredActorClassPath,
        const FString& RequiredActorComponentClassPath);

    /** Returns actor display-name entries for matching actors, preserving their owning level package names. */
    UFUNCTION(BlueprintCallable, Category = "ZL OmniStream Auto-Populate")
    static TArray<FZLAutoPopulateLevelActorDesc> GetLevelActorDisplayEntriesByClassRequirements(
        const TArray<FString>& CandidateLevelPackageNames,
        const FString& RequiredActorClassPath,
        const FString& RequiredActorComponentClassPath);
};

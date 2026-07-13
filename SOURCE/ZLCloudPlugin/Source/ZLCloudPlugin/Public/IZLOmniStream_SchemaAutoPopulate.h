// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Dom/JsonValue.h"
#include "ZLStateKeyInfo.h"

class UWorld;
class FJsonObject;

struct FZLAutoPopulateFieldMeta
{
	FString KeyPath;
	FString DataType;
	TSharedPtr<FJsonValue> DefaultValue;
	bool bAllowNullValue = false;
	bool bDefaultValueIsNull = false;
	bool bLimitValues = false;
	TArray<TSharedPtr<FJsonValue>> AcceptedValues;
	bool bIgnoredInDataHashes = false;
};

struct FZLAutoPopulateResult
{
	TSharedPtr<FJsonObject> JsonContribution = MakeShared<FJsonObject>();
	TMap<FString, FZLAutoPopulateFieldMeta> FieldMetaByKey;
	TArray<FDIMEModelMetadata> DimeModelData;
};

struct FZLAutoPopulateAdvancedCheckboxDef
{
	FString Id;
	FString Label;
	bool bDefaultChecked = false;
};

struct FZLAutoPopulateAdvancedLevelSelectorDef
{
	FString Id;
	FString Label;
	FString RequiredActorClassPath;
	FString RequiredActorComponentClassPath;
};

struct FZLAutoPopulateAdvancedOptionDef
{
	FString OptionName;
	FString FoldoutTitle;
	TArray<FZLAutoPopulateAdvancedCheckboxDef> Checkboxes;
	TArray<FZLAutoPopulateAdvancedLevelSelectorDef> LevelSelectors;
};

struct FZLAutoPopulateAdvancedOptionSettings
{
	TMap<FString, bool> CheckboxValues;
	TMap<FString, TArray<FString>> LevelSelectionsBySelectorId;
	TMap<FString, TArray<FString>> ActorSelectionsBySelectorId;
};

using FZLAutoPopulateAdvancedSettingsMap = TMap<FString, FZLAutoPopulateAdvancedOptionSettings>;

/**
 * Modular feature interface implemented by plugins that want to contribute
 * options to the OmniStream Schemas Editor auto-populate dropdown.
 *
 * Each implementation:
 *  - returns one or more dropdown labels via GetAutoPopulateOptionNames(),
 *  - generates a JSON object describing the schema entries it owns when
 *    AutoPopulate() is invoked with the labels selected by the user.
 *
 * Plugins register an instance with IModularFeatures using
 * GetModularFeatureName() during their module's StartupModule() and
 * unregister it during ShutdownModule().
 */
class IZLOmniStream_SchemaAutoPopulate : public IModularFeature
{
public:
	virtual ~IZLOmniStream_SchemaAutoPopulate() = default;

	static FName GetModularFeatureName()
	{
		static const FName FeatureName(TEXT("ZLOmniStream_SchemaAutoPopulate"));
		return FeatureName;
	}

	/** Display name of the plugin/module providing this implementation. Used by the advanced
	 * auto-populate window to group option ticks under foldouts of the form
	 * "[Plugin Name] - [option, option, ...]". Conventionally the Unreal module name. */
	virtual FString GetPluginName() const = 0;

	/** Labels that should appear in the auto-populate dropdown for this implementation. */
	virtual TArray<FString> GetAutoPopulateOptionNames() const = 0;

	/** Optional advanced per-option UI descriptors shown in the Python advanced auto-populate window. */
	virtual TArray<FZLAutoPopulateAdvancedOptionDef> GetAdvancedOptionDefs() const
	{
		return {};
	}

	/**
	 * Generates the JSON contribution for the given selected labels by inspecting
	 * the provided level assets. Implementations should only act on labels that
	 * they themselves declared via GetAutoPopulateOptionNames().
	 */
	virtual FZLAutoPopulateResult AutoPopulate(const TArray<UWorld*>& Levels, const TArray<FString>& SelectedOptions) = 0;

	/**
	 * Advanced-settings aware variant used by the Python advanced auto-populate window.
	 * Default implementation preserves backwards compatibility by forwarding to AutoPopulate().
	 */
	virtual FZLAutoPopulateResult AutoPopulateWithAdvancedSettings(
		const TArray<UWorld*>& Levels,
		const TArray<FString>& SelectedOptions,
		const FZLAutoPopulateAdvancedSettingsMap& AdvancedSettingsByOption)
	{
		return AutoPopulate(Levels, SelectedOptions);
	}

	/**
	 * Appends metadata for ALL models the implementing plugin can parse from the
	 * project (groups, codes and description lookups), independent of any schema
	 * asset auto-populate selection. This lets build metadata always include the
	 * full set of DIME model data even when no schema asset has been populated
	 * with it. Implementations should append to OutModelData rather than reset it.
	 * Default implementation contributes nothing.
	 */
	virtual void GetAllProjectModelData(TArray<FDIMEModelMetadata>& OutModelData) const
	{
	}
};

// Copyright ZeroLight ltd. All Rights Reserved.

#if WITH_EDITOR

#include "ZeroLightEditorStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Interfaces/IPluginManager.h"

FZeroLightEditorStyle FZeroLightEditorStyle::ZeroLightEditorStyle;


FZeroLightEditorStyle::FZeroLightEditorStyle()
	:FSlateStyleSet("ZeroLightEditorStyle")
{
	FString contentRoot = IPluginManager::Get().FindPlugin("ZLCloudPlugin")->GetContentDir() / "Editor/Icons";
	SetContentRoot(contentRoot);

	const FVector2D Icon40x40(40.0f, 40.0f);
	Set("MainButton.ZeroLight", new FSlateImageBrush(RootToContentDir(L"ZeroLight.png"), Icon40x40));
	Set("ZeroLightAboutScreen.Background", new FSlateImageBrush(RootToContentDir(L"ZeroLightAboutBackground.png"), FVector2D(600, 332), FLinearColor::White, ESlateBrushTileType::Both));

	this->Set("ZeroLight.Logo", new FSlateVectorImageBrush(RootToContentDir(L"zllogo.svg"), FVector2D(20.0f, 20.0f)));

	FSlateStyleRegistry::RegisterSlateStyle(*this);
}

FZeroLightEditorStyle::~FZeroLightEditorStyle()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*this);
}

#endif

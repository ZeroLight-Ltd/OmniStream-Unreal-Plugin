// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "Styling/SlateStyle.h"

class FZeroLightEditorStyle
	: public FSlateStyleSet
{
private:
	FZeroLightEditorStyle();
	~FZeroLightEditorStyle();

	static FZeroLightEditorStyle ZeroLightEditorStyle;
};

#endif

// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Layout/Margin.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class ITableRow;
class SButton;
class STableViewBase;
struct FSlateBrush;

/**
 * About screen contents widget                   
 */
class SZeroLightAboutScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZeroLightAboutScreen)
		{}
	SLATE_END_ARGS()

	/**
	 * Constructs the about screen widgets                   
	 */
	void Construct(const FArguments& InArgs);

private:
	TSharedPtr<SButton> ZeroLightButton;
	FText GetZLVersionText(bool bIncludeHeader = true);
	FString GetZLLicensePath();
	FReply OnCopyToClipboard();
	FReply OnGetLogsZip();
	FReply OnClose();
};

#endif


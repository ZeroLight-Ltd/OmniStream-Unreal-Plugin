// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SEditableTextBox.h"

class SWindow;

struct FZLFilteredKeyItem
{
	int32 Code = 0;
	FString Name;
};

class SZLFilteredKeysEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZLFilteredKeysEditor) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SZLFilteredKeysEditor();

	static void OpenWindow();

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnPreviewKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnKeyChar(const FGeometry& MyGeometry, const FCharacterEvent& InCharacterEvent) override;

private:
	void LoadFromSettings();
	void SaveToSettings();
	void RefreshListFromCodes();
	void SetCodes(const TArray<int32>& NewCodes, bool bSave);
	void AddKeyCodes(const TArray<int32>& CodesToAdd);
	void RemoveKeyCodes(const TArray<int32>& CodesToRemove);
	bool IsPresetActive(const TCHAR* CommaSeparatedCodes) const;
	void TogglePreset(const TCHAR* CommaSeparatedCodes);
	void RemoveKeyCode(int32 Code);
	void TryAddRecordedKey(const FKey& Key);

	void StartRecording();
	void StopRecording();
	void HandleApplicationKeyDown(const FKeyEvent& InKeyEvent);
	bool IsEditorWindowActive() const;

	FReply OnRecordClicked();
	FReply OnClearAllClicked();
	FText GetRecordButtonText() const;
	FText GetStatusText() const;
	FSlateColor GetStatusColor() const;
	void UpdateRawCodesBox();
	void OnRawCodesCommitted(const FText& NewText, ETextCommit::Type CommitType);

	TSharedRef<ITableRow> GenerateKeyRow(TSharedPtr<FZLFilteredKeyItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<SWidget> MakePresetButton(const FText& Label, const TCHAR* CommaSeparatedCodes);

	TArray<int32> CurrentCodes;
	TArray<TSharedPtr<FZLFilteredKeyItem>> KeyItems;
	TSharedPtr<SListView<TSharedPtr<FZLFilteredKeyItem>>> KeyListView;
	TSharedPtr<SEditableTextBox> RawCodesBox;

	bool bRecording = false;
	FDelegateHandle PreInputKeyDownHandle;

	static TWeakPtr<SWindow> s_Window;
};

#endif

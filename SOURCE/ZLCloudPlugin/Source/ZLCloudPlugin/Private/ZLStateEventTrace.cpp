// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLStateEventTrace.h"
#include "ZLCloudPluginVersion.h"

#if WITH_EDITOR
#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#endif
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace ZLStateEventTrace
{
static FString SerializeObject(const TSharedPtr<FJsonObject>& Object)
{
	if (!Object.IsValid())
	{
		return TEXT("{}");
	}

	FString Result;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
	Writer->Close();
	return Result;
}

static void CollectLeafPaths(
	const TSharedPtr<FJsonObject>& Object,
	const FString& ParentPath,
	TArray<FString>& OutPaths)
{
	if (!Object.IsValid())
	{
		return;
	}

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
	{
		const FString Path = ParentPath.IsEmpty() ? Pair.Key : ParentPath + TEXT(".") + Pair.Key;
		if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object)
		{
			CollectLeafPaths(Pair.Value->AsObject(), Path, OutPaths);
			continue;
		}

		OutPaths.Add(Path);
	}
}

static TSharedPtr<FJsonObject> ParseObject(const FString& Json)
{
	TSharedPtr<FJsonObject> Result;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	FJsonSerializer::Deserialize(Reader, Result);
	return Result;
}
}

void FZLStateEventRecorder::Record(
	EZLStateEventType Type,
	const FString& Source,
	const FString& Summary,
	const TSharedPtr<FJsonObject>& Payload,
	const TSharedPtr<FJsonObject>& CurrentState)
{
	if (!IsRecording())
	{
		return;
	}

	FZLStateEvent Event;
	Event.Type = Type;
	Event.Source = Source;
	Event.Summary = Summary;
	Event.PayloadJson = ZLStateEventTrace::SerializeObject(Payload);
	Event.StateSnapshotJson = ZLStateEventTrace::SerializeObject(CurrentState);
	ZLStateEventTrace::CollectLeafPaths(Payload, FString(), Event.TouchedPaths);
	AddEvent(MoveTemp(Event));
}

void FZLStateEventRecorder::RecordJsonString(
	EZLStateEventType Type,
	const FString& Source,
	const FString& Summary,
	const FString& PayloadJson,
	const TSharedPtr<FJsonObject>& CurrentState)
{
	if (!IsRecording())
	{
		return;
	}

	FZLStateEvent Event;
	Event.Type = Type;
	Event.Source = Source;
	Event.Summary = Summary;
	Event.PayloadJson = PayloadJson;
	Event.StateSnapshotJson = ZLStateEventTrace::SerializeObject(CurrentState);
	ZLStateEventTrace::CollectLeafPaths(
		ZLStateEventTrace::ParseObject(PayloadJson),
		FString(),
		Event.TouchedPaths);
	AddEvent(MoveTemp(Event));
}

void FZLStateEventRecorder::AddEvent(FZLStateEvent&& Event)
{
	FScopeLock Lock(&Mutex);
	const double Now = FPlatformTime::Seconds();
	if (StartTimeSeconds <= 0.0)
	{
		StartTimeSeconds = Now;
	}

	Event.Sequence = NextSequence++;
	Event.FrameNumber = GFrameCounter;
	Event.TimeSeconds = Now - StartTimeSeconds;
	Events.Add(MoveTemp(Event));
	if (Events.Num() > MaxEvents)
	{
#if UNREAL_5_8_OR_NEWER
		Events.RemoveAt(0, Events.Num() - MaxEvents, EAllowShrinking::No);
#else
		Events.RemoveAt(0, Events.Num() - MaxEvents, false);
#endif
	}
	++Revision;
}

TArray<FZLStateEvent> FZLStateEventRecorder::GetEvents() const
{
	FScopeLock Lock(&Mutex);
	return Events;
}

uint64 FZLStateEventRecorder::GetRevision() const
{
	FScopeLock Lock(&Mutex);
	return Revision;
}

void FZLStateEventRecorder::Clear()
{
	FScopeLock Lock(&Mutex);
	Events.Reset();
	StartTimeSeconds = 0.0;
	NextSequence = 1;
	++Revision;
}

void FZLStateEventRecorder::StartRecording()
{
	bRecording.Store(true);
}

void FZLStateEventRecorder::StopRecording()
{
	bRecording.Store(false);
}

bool FZLStateEventRecorder::IsRecording() const
{
	return bRecording.Load();
}

void FZLStateEventRecorder::LoadEvents(TArray<FZLStateEvent>&& InEvents)
{
	FScopeLock Lock(&Mutex);
	Events = MoveTemp(InEvents);
	NextSequence = 1;
	for (const FZLStateEvent& Event : Events)
	{
		NextSequence = FMath::Max(NextSequence, Event.Sequence + 1);
	}
	StartTimeSeconds = 0.0;
	++Revision;
}

bool FZLStateEventRecorder::SaveToFile(FString& OutFilePath) const
{
	auto TypeToString = [](EZLStateEventType Type) -> const TCHAR*
	{
		switch (Type)
		{
		case EZLStateEventType::Inbound: return TEXT("Received");
		case EZLStateEventType::Processing: return TEXT("Processing");
		case EZLStateEventType::Mutation: return TEXT("StateChange");
		case EZLStateEventType::Outbound: return TEXT("SentToWeb");
		default: return TEXT("Lifecycle");
		}
	};

	const TArray<FZLStateEvent> Snapshot = GetEvents();

	TArray<TSharedPtr<FJsonValue>> EventValues;
	EventValues.Reserve(Snapshot.Num());
	for (const FZLStateEvent& Event : Snapshot)
	{
		TSharedRef<FJsonObject> EventObject = MakeShared<FJsonObject>();
		EventObject->SetNumberField(TEXT("sequence"), static_cast<double>(Event.Sequence));
		EventObject->SetNumberField(TEXT("frame"), static_cast<double>(Event.FrameNumber));
		EventObject->SetNumberField(TEXT("timeSeconds"), Event.TimeSeconds);
		EventObject->SetStringField(TEXT("type"), TypeToString(Event.Type));
		EventObject->SetStringField(TEXT("source"), Event.Source);
		EventObject->SetStringField(TEXT("summary"), Event.Summary);

		TArray<TSharedPtr<FJsonValue>> PathValues;
		PathValues.Reserve(Event.TouchedPaths.Num());
		for (const FString& Path : Event.TouchedPaths)
		{
			PathValues.Add(MakeShared<FJsonValueString>(Path));
		}
		EventObject->SetArrayField(TEXT("touchedPaths"), PathValues);

		TSharedPtr<FJsonValue> PayloadValue;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Event.PayloadJson);
			if (!FJsonSerializer::Deserialize(Reader, PayloadValue) || !PayloadValue.IsValid())
			{
				PayloadValue = MakeShared<FJsonValueString>(Event.PayloadJson);
			}
		}
		EventObject->SetField(TEXT("payload"), PayloadValue);

		TSharedPtr<FJsonValue> SnapshotValue;
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Event.StateSnapshotJson);
			if (!FJsonSerializer::Deserialize(Reader, SnapshotValue) || !SnapshotValue.IsValid())
			{
				SnapshotValue = MakeShared<FJsonValueString>(Event.StateSnapshotJson);
			}
		}
		EventObject->SetField(TEXT("stateSnapshot"), SnapshotValue);

		EventValues.Add(MakeShared<FJsonValueObject>(EventObject));
	}

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("eventCount"), Snapshot.Num());
	Root->SetArrayField(TEXT("events"), EventValues);

	FString JsonString;
	const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(Root, Writer);
	Writer->Close();

	const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ZLStateEventTimeline"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString Filename = FString::Printf(
		TEXT("timeline_%s.json"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	OutFilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, Filename));

	return FFileHelper::SaveStringToFile(JsonString, *OutFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

#if WITH_EDITOR

#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Text/STextBlock.h"

namespace ZLStateEventTimeline
{
static int32 LaneForType(EZLStateEventType Type)
{
	switch (Type)
	{
	case EZLStateEventType::Inbound: return 0;
	case EZLStateEventType::Processing: return 1;
	case EZLStateEventType::Mutation: return 2;
	case EZLStateEventType::Outbound: return 3;
	default: return 1;
	}
}

static FLinearColor ColorForType(EZLStateEventType Type)
{
	switch (Type)
	{
	case EZLStateEventType::Inbound: return FLinearColor(0.20f, 0.60f, 1.00f);
	case EZLStateEventType::Processing: return FLinearColor(0.72f, 0.42f, 1.00f);
	case EZLStateEventType::Mutation: return FLinearColor(0.15f, 0.85f, 0.55f);
	case EZLStateEventType::Outbound: return FLinearColor(1.00f, 0.48f, 0.20f);
	default: return FLinearColor(0.65f, 0.65f, 0.65f);
	}
}

static const TCHAR* TypeToString(EZLStateEventType Type)
{
	switch (Type)
	{
	case EZLStateEventType::Inbound: return TEXT("Received");
	case EZLStateEventType::Processing: return TEXT("Processing");
	case EZLStateEventType::Mutation: return TEXT("StateChange");
	case EZLStateEventType::Outbound: return TEXT("SentToWeb");
	default: return TEXT("Lifecycle");
	}
}

static EZLStateEventType TypeFromString(const FString& TypeName)
{
	if (TypeName.Equals(TEXT("Received"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Inbound"), ESearchCase::IgnoreCase))
	{
		return EZLStateEventType::Inbound;
	}
	if (TypeName.Equals(TEXT("Processing"), ESearchCase::IgnoreCase))
	{
		return EZLStateEventType::Processing;
	}
	if (TypeName.Equals(TEXT("StateChange"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Mutation"), ESearchCase::IgnoreCase))
	{
		return EZLStateEventType::Mutation;
	}
	if (TypeName.Equals(TEXT("SentToWeb"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("Outbound"), ESearchCase::IgnoreCase))
	{
		return EZLStateEventType::Outbound;
	}
	return EZLStateEventType::Lifecycle;
}

static FString JsonValueToString(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->IsNull())
	{
		return TEXT("{}");
	}
	if (Value->Type == EJson::String)
	{
		return Value->AsString();
	}

	FString Result;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Result);
	FJsonSerializer::Serialize(Value.ToSharedRef(), TEXT(""), Writer);
	Writer->Close();
	return Result;
}

class SZLStateEventTimeline : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SZLStateEventTimeline) {}
		SLATE_ARGUMENT(FZLStateEventRecorder*, Recorder)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Recorder = Args._Recorder;

		ChildSlot
		[
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.025f, 0.03f, 0.04f, 1.0f))
			.Padding(8.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 7.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 14.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("STATE EVENT GRAPH")))
						.ColorAndOpacity(FLinearColor(0.65f, 0.85f, 1.0f))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.6f)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SAssignNew(KeyFilterBox, SEditableTextBox)
						.HintText(FText::FromString(TEXT("JSON key (cameraState.springArm.distance)")))
						.OnTextChanged(this, &SZLStateEventTimeline::OnFilterChanged)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(0.4f)
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SAssignNew(ValueFilterBox, SEditableTextBox)
						.HintText(FText::FromString(TEXT("Optional value (0)")))
						.OnTextChanged(this, &SZLStateEventTimeline::OnFilterChanged)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSearchScopeToggle(
							TEXT("Paths"),
							TEXT("Keys/paths this event changed or claimed (left detail list)"),
							bSearchTouchedPaths)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSearchScopeToggle(
							TEXT("Payload"),
							TEXT("Only this event's payload JSON"),
							bSearchPayload)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						MakeSearchScopeToggle(
							TEXT("Snapshot"),
							TEXT("Full current-state snapshot. Stays highlighted on later events once a value exists."),
							bSearchSnapshot)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						MakeSearchScopeToggle(
							TEXT("Meta"),
							TEXT("Source function name and summary text"),
							bSearchMeta)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SButton)
						.ToolTipText(FText::FromString(TEXT("Start or pause state timeline recording")))
						.OnClicked(this, &SZLStateEventTimeline::ToggleRecording)
						[
							SNew(STextBlock)
								.Text_Lambda([this]()
								{
									return FText::FromString(Recorder && Recorder->IsRecording()
										? TEXT("■ STOP")
										: TEXT("● REC"));
								})
								.ColorAndOpacity_Lambda([this]()
								{
									return Recorder && Recorder->IsRecording()
										? FLinearColor(1.0f, 0.12f, 0.08f, 1.0f)
										: FLinearColor(0.72f, 0.22f, 0.18f, 1.0f);
								})
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Clear filters")))
						.OnClicked(this, &SZLStateEventTimeline::ClearFilters)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Clear events")))
						.OnClicked(this, &SZLStateEventTimeline::ClearEvents)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Save JSON")))
						.ToolTipText(FText::FromString(TEXT("Write the full timeline to Saved/ZLStateEventTimeline and copy the path")))
						.OnClicked(this, &SZLStateEventTimeline::DumpTimelineJson)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(FText::FromString(TEXT("Load JSON")))
						.ToolTipText(FText::FromString(TEXT("Replace the timeline with a previously dumped JSON file")))
						.OnClicked(this, &SZLStateEventTimeline::LoadTimelineJson)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBox)
					.HeightOverride(LaneHeight * 4.0f + ScrollbarHeight)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Top)
						.Padding(0.0f, 0.0f, 8.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(92.0f)
							.HeightOverride(LaneHeight * 4.0f)
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()[MakeLaneLabel(TEXT("RECEIVED"))]
								+ SVerticalBox::Slot().AutoHeight()[MakeLaneLabel(TEXT("PROCESSING"))]
								+ SVerticalBox::Slot().AutoHeight()[MakeLaneLabel(TEXT("STATE CHANGE"))]
								+ SVerticalBox::Slot().AutoHeight()[MakeLaneLabel(TEXT("SENT TO WEB"))]
							]
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.VAlign(VAlign_Top)
						[
							SAssignNew(TimelineScrollBox, SScrollBox)
							.Orientation(Orient_Horizontal)
							.ScrollBarThickness(FVector2D(ScrollbarHeight, ScrollbarHeight))
							+ SScrollBox::Slot()
							[
								SAssignNew(TimelineHost, SBox)
							]
						]
					]
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.Padding(0.0f, 7.0f, 0.0f, 0.0f)
				[
					SNew(SSplitter)
					.Orientation(Orient_Horizontal)
					+ SSplitter::Slot()
					.Value(0.34f)
					[
						MakeJsonPanel(TEXT("TOUCHED PATHS"), EventChanges)
					]
					+ SSplitter::Slot()
					.Value(0.33f)
					[
						MakeJsonPanel(TEXT("EVENT PAYLOAD"), EventPayload)
					]
					+ SSplitter::Slot()
					.Value(0.33f)
					[
						MakeJsonPanel(TEXT("STATE SNAPSHOT"), EventSnapshot)
					]
				]
			]
		];

		RebuildTimeline();
	}

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
		if (Recorder && Recorder->GetRevision() != DisplayedRevision)
		{
			RebuildTimeline();
		}
	}

private:
	static constexpr float LaneHeight = 44.0f;
	static constexpr float ScrollbarHeight = 14.0f;

	TSharedRef<SWidget> MakeLaneLabel(const FString& Label)
	{
		return SNew(SBox)
			.HeightOverride(LaneHeight)
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity(FLinearColor(0.46f, 0.52f, 0.62f))
			];
	}

	TSharedRef<SWidget> MakeJsonPanel(const FString& Label, TSharedPtr<SMultiLineEditableTextBox>& OutTextBox)
	{
		return SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0.06f, 0.07f, 0.09f, 1.0f))
			.Padding(6.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					SNew(STextBlock)
						.Text(FText::FromString(Label))
						.ColorAndOpacity(FLinearColor(0.46f, 0.68f, 0.82f))
				]
				+ SVerticalBox::Slot().FillHeight(1.0f).Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SAssignNew(OutTextBox, SMultiLineEditableTextBox)
						.IsReadOnly(true)
				]
			];
	}

	TSharedRef<SWidget> MakeSearchScopeToggle(const FString& Label, const FString& Tooltip, bool& ScopeFlag)
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([&ScopeFlag]()
			{
				return ScopeFlag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, &ScopeFlag](ECheckBoxState NewState)
			{
				ScopeFlag = NewState == ECheckBoxState::Checked;
				RebuildTimeline();
			})
			.ToolTipText(FText::FromString(Tooltip))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ColorAndOpacity(FLinearColor(0.70f, 0.76f, 0.84f))
			];
	}

	bool MatchesFilter(const FZLStateEvent& Event) const
	{
		const FString Key = KeyFilterBox.IsValid() ? KeyFilterBox->GetText().ToString().TrimStartAndEnd() : FString();
		const FString Value = ValueFilterBox.IsValid() ? ValueFilterBox->GetText().ToString().TrimStartAndEnd() : FString();
		if (Key.IsEmpty() && Value.IsEmpty())
		{
			return true;
		}

		if (!bSearchTouchedPaths && !bSearchPayload && !bSearchSnapshot && !bSearchMeta)
		{
			return false;
		}

		auto ContainsBoth = [&](const FString& Haystack) -> bool
		{
			const bool bKeyOk = Key.IsEmpty() || Haystack.Contains(Key, ESearchCase::IgnoreCase);
			const bool bValueOk = Value.IsEmpty() || Haystack.Contains(Value, ESearchCase::IgnoreCase);
			return bKeyOk && bValueOk;
		};

		if (bSearchTouchedPaths)
		{
			FString PathsText = FString::Join(Event.TouchedPaths, TEXT("\n"));
			if (ContainsBoth(PathsText))
			{
				return true;
			}
		}
		if (bSearchPayload && ContainsBoth(Event.PayloadJson))
		{
			return true;
		}
		if (bSearchSnapshot && ContainsBoth(Event.StateSnapshotJson))
		{
			return true;
		}
		if (bSearchMeta && ContainsBoth(Event.Source + TEXT("\n") + Event.Summary))
		{
			return true;
		}

		return false;
	}

	struct FInvalidStateWarning
	{
		TArray<FString> TimedOutPaths;
		TArray<FString> UnmatchedPaths;

		bool HasWarnings() const
		{
			return TimedOutPaths.Num() > 0 || UnmatchedPaths.Num() > 0;
		}
	};

	void BuildInvalidStateWarnings()
	{
		InvalidStateWarnings.Reset();
		TMap<FString, uint64> LatestRequestEventByPath;

		for (const FZLStateEvent& Event : CachedEvents)
		{
			if (Event.Type == EZLStateEventType::Inbound
				|| Event.Type == EZLStateEventType::Processing)
			{
				for (const FString& Path : Event.TouchedPaths)
				{
					LatestRequestEventByPath.Add(Path.ToLower(), Event.Sequence);
				}
				continue;
			}

			if (Event.Type != EZLStateEventType::Outbound)
			{
				continue;
			}

			const TSharedPtr<FJsonObject> ResponseObject = ZLStateEventTrace::ParseObject(Event.PayloadJson);
			if (!ResponseObject.IsValid())
			{
				continue;
			}

			TArray<FString> TimedOutPaths;
			TArray<FString> UnmatchedPaths;
			TFunction<void(const TSharedPtr<FJsonObject>&)> CollectFailureObjects;
			CollectFailureObjects = [&](const TSharedPtr<FJsonObject>& Object)
			{
				if (!Object.IsValid())
				{
					return;
				}

				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
				{
					if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Object)
					{
						continue;
					}

					const TSharedPtr<FJsonObject> ChildObject = Pair.Value->AsObject();
					if (Pair.Key.Equals(TEXT("timeout_state"), ESearchCase::IgnoreCase))
					{
						ZLStateEventTrace::CollectLeafPaths(ChildObject, FString(), TimedOutPaths);
					}
					else if (Pair.Key.Equals(TEXT("unmatched_state"), ESearchCase::IgnoreCase)
						|| Pair.Key.Equals(TEXT("unprocessed_state"), ESearchCase::IgnoreCase))
					{
						ZLStateEventTrace::CollectLeafPaths(ChildObject, FString(), UnmatchedPaths);
					}
					else
					{
						CollectFailureObjects(ChildObject);
					}
				}
			};
			CollectFailureObjects(ResponseObject);

			auto AttachWarnings = [&](const TArray<FString>& Paths, bool bTimedOut)
			{
				for (const FString& Path : Paths)
				{
					if (const uint64* OriginSequence = LatestRequestEventByPath.Find(Path.ToLower()))
					{
						FInvalidStateWarning& Warning = InvalidStateWarnings.FindOrAdd(*OriginSequence);
						(bTimedOut ? Warning.TimedOutPaths : Warning.UnmatchedPaths).AddUnique(Path);
					}
				}
			};
			AttachWarnings(TimedOutPaths, true);
			AttachWarnings(UnmatchedPaths, false);
		}
	}

	static FString FormatInvalidStateWarning(const FInvalidStateWarning& Warning)
	{
		FString Result;
		if (Warning.TimedOutPaths.Num() > 0)
		{
			Result += TEXT("TIMED OUT:\n  ") + FString::Join(Warning.TimedOutPaths, TEXT("\n  "));
		}
		if (Warning.UnmatchedPaths.Num() > 0)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT("\n\n");
			}
			Result += TEXT("UNMATCHED / IGNORED:\n  ") + FString::Join(Warning.UnmatchedPaths, TEXT("\n  "));
		}
		return Result;
	}

	void RebuildTimeline()
	{
		if (!Recorder || !TimelineHost.IsValid())
		{
			return;
		}

		CachedEvents = Recorder->GetEvents();
		DisplayedRevision = Recorder->GetRevision();
		BuildInvalidStateWarnings();

		const bool bFiltering =
			(KeyFilterBox.IsValid() && !KeyFilterBox->GetText().IsEmpty())
			|| (ValueFilterBox.IsValid() && !ValueFilterBox->GetText().IsEmpty());

		constexpr float TimelineHeight = LaneHeight * 4.0f;
		constexpr float DotSize = 26.0f;
		constexpr float EventSpacing = 4.0f;
		constexpr float FramePadding = 6.0f;

		struct FEventLayout
		{
			const FZLStateEvent* Event = nullptr;
			float X = 0.0f;
		};
		struct FFrameLayout
		{
			uint64 FrameNumber = 0;
			float StartX = 0.0f;
			float EndX = 0.0f;
		};

		TArray<FEventLayout> EventLayouts;
		TArray<FFrameLayout> FrameLayouts;
		float CursorX = 18.0f;
		double PreviousEventTime = CachedEvents.Num() > 0 ? CachedEvents[0].TimeSeconds : 0.0;
		uint64 CurrentFrame = MAX_uint64;

		for (const FZLStateEvent& Event : CachedEvents)
		{
			if (Event.FrameNumber != CurrentFrame)
			{
				if (FrameLayouts.Num() > 0)
				{
					FrameLayouts.Last().EndX = CursorX + FramePadding;
					const float InterFrameGap = FMath::Clamp(
						static_cast<float>((Event.TimeSeconds - PreviousEventTime) * 90.0),
						10.0f,
						180.0f);
					CursorX += InterFrameGap;
				}

				CurrentFrame = Event.FrameNumber;
				FrameLayouts.Add({CurrentFrame, CursorX - FramePadding, 0.0f});
			}

			// Preserve ordering within a frame and retain a small visual hint of sub-frame timing.
			const float FineTimeOffset = FMath::Clamp(
				static_cast<float>((Event.TimeSeconds - PreviousEventTime) * 1000.0),
				0.0f,
				8.0f);
			const float EventX = CursorX + FineTimeOffset;
			EventLayouts.Add({&Event, EventX});
			CursorX = EventX + DotSize + EventSpacing;
			PreviousEventTime = Event.TimeSeconds;
		}

		if (FrameLayouts.Num() > 0)
		{
			FrameLayouts.Last().EndX = CursorX + FramePadding;
		}

		// Stretch each frame band up to the next frame line so gaps don't flash the plain lane color.
		for (int32 FrameIndex = 0; FrameIndex < FrameLayouts.Num(); ++FrameIndex)
		{
			const float NextStartX = FrameIndex + 1 < FrameLayouts.Num()
				? FrameLayouts[FrameIndex + 1].StartX
				: FMath::Max(FrameLayouts[FrameIndex].EndX, CursorX + 60.0f);
			FrameLayouts[FrameIndex].EndX = FMath::Max(FrameLayouts[FrameIndex].EndX, NextStartX);
		}

		const float Width = FMath::Max(640.0f, CursorX + 60.0f);
		TSharedRef<SConstraintCanvas> OverlayCanvas = SNew(SConstraintCanvas);
		TSharedRef<SConstraintCanvas> LaneCanvases[4] = {
			SNew(SConstraintCanvas),
			SNew(SConstraintCanvas),
			SNew(SConstraintCanvas),
			SNew(SConstraintCanvas)
		};

		for (int32 FrameIndex = 0; FrameIndex < FrameLayouts.Num(); ++FrameIndex)
		{
			const FFrameLayout& Frame = FrameLayouts[FrameIndex];
			const float FrameWidth = FMath::Max(1.0f, Frame.EndX - Frame.StartX);
			const FLinearColor BandColor = FrameIndex % 2 == 0
				? FLinearColor(0.10f, 0.32f, 0.48f, 0.12f)
				: FLinearColor(0.32f, 0.18f, 0.46f, 0.12f);

			// Stretch top-to-bottom so frame markers always cover all lanes,
			// even if the overlay canvas's desired height is wrong.
			OverlayCanvas->AddSlot()
				.Anchors(FAnchors(0.0f, 0.0f, 0.0f, 1.0f))
				.Offset(FMargin(Frame.StartX, 0.0f, FrameWidth, 0.0f))
				.Alignment(FVector2D(0.0f, 0.0f))
				[
					SNew(SColorBlock)
					.Color(BandColor)
					.ToolTipText(FText::FromString(FString::Printf(TEXT("Frame %llu"), Frame.FrameNumber)))
				];

			OverlayCanvas->AddSlot()
				.Anchors(FAnchors(0.0f, 0.0f, 0.0f, 1.0f))
				.Offset(FMargin(Frame.StartX, 0.0f, 3.0f, 0.0f))
				.Alignment(FVector2D(0.0f, 0.0f))
				[
					SNew(SColorBlock)
					.Color(FLinearColor(0.28f, 0.58f, 0.82f, 0.90f))
				];
		}

		for (const FEventLayout& Layout : EventLayouts)
		{
			const FZLStateEvent& Event = *Layout.Event;
			const int32 LaneIndex = LaneForType(Event.Type);
			const bool bMatches = MatchesFilter(Event);
			FLinearColor DotColor = ColorForType(Event.Type);
			if (bFiltering)
			{
				DotColor = bMatches
					? FLinearColor(3.0f, 1.8f, 0.08f, 1.0f)
					: FLinearColor(DotColor.R, DotColor.G, DotColor.B, 0.08f);
			}
			const bool bSelected = Event.Sequence == SelectedSequence;
			if (bSelected)
			{
				DotColor = FLinearColor(0.20f, 2.8f, 3.2f, 1.0f);
			}
			const FInvalidStateWarning* InvalidWarning = InvalidStateWarnings.Find(Event.Sequence);
			const bool bHasInvalidWarning = InvalidWarning && InvalidWarning->HasWarnings();
			if (bHasInvalidWarning)
			{
				DotColor = FLinearColor(3.0f, 0.02f, 0.01f, 1.0f);
			}
			const FString Marker = bHasInvalidWarning
				? TEXT("!")
				: (bSelected ? TEXT("◆") : (bFiltering && bMatches ? TEXT("★") : TEXT("●")));
			const FLinearColor MarkerColor = bHasInvalidWarning || bSelected || (bFiltering && bMatches)
					? FLinearColor::White
					: FLinearColor(0.75f, 0.80f, 0.86f, 1.0f);

			FString Tooltip = FString::Printf(
				TEXT("#%llu  Frame %llu  +%.3fs\n%s\n%s\n\n%s"),
				Event.Sequence,
				Event.FrameNumber,
				Event.TimeSeconds,
				*Event.Source,
				*Event.Summary,
				*FString::Join(Event.TouchedPaths, TEXT("\n")));
			if (bHasInvalidWarning)
			{
				Tooltip += TEXT("\n\n⚠ INVALID STATE REQUEST\n") + FormatInvalidStateWarning(*InvalidWarning);
			}

			// Full-lane-height slot so VAlign_Center puts the button in the middle of the row.
			LaneCanvases[LaneIndex]->AddSlot()
				.Offset(FMargin(Layout.X, 0.0f, DotSize, LaneHeight))
				.Anchors(FAnchors(0.0f, 0.0f))
				.Alignment(FVector2D(0.0f, 0.0f))
				[
					SNew(SBox)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(SBox)
								.WidthOverride(DotSize)
								.HeightOverride(DotSize)
								[
									SNew(SOverlay)
									+ SOverlay::Slot()
									[
										SNew(SButton)
											.ButtonColorAndOpacity(DotColor)
											.ContentPadding(FMargin(2.0f))
											.ToolTipText(FText::FromString(Tooltip))
											.OnClicked(this, &SZLStateEventTimeline::SelectEvent, Event.Sequence)
											[
												SNew(SBox)
											]
									]
									+ SOverlay::Slot()
									.HAlign(HAlign_Center)
									.VAlign(VAlign_Center)
									[
										SNew(SBox)
											.Visibility(EVisibility::HitTestInvisible)
											[
												SNew(STextBlock)
													.Text(FText::FromString(Marker))
													.Font(bHasInvalidWarning
														? FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16)
														: FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
													.ColorAndOpacity(MarkerColor)
													.Justification(ETextJustify::Center)
											]
									]
								]
						]
				];
		}

		auto MakeLaneBackground = [&](const FLinearColor& LaneColor) -> TSharedRef<SWidget>
		{
			return SNew(SBox)
				.HeightOverride(LaneHeight)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[
						SNew(SColorBlock).Color(LaneColor)
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Top)
					[
						SNew(SBox)
						.HeightOverride(2.0f)
						[
							SNew(SColorBlock).Color(FLinearColor(0.34f, 0.44f, 0.56f, 1.0f))
						]
					]
					+ SOverlay::Slot()
					.VAlign(VAlign_Bottom)
					[
						SNew(SBox)
						.HeightOverride(2.0f)
						[
							SNew(SColorBlock).Color(FLinearColor(0.34f, 0.44f, 0.56f, 1.0f))
						]
					]
				];
		};

		auto MakeLaneEvents = [&](int32 LaneIndex) -> TSharedRef<SWidget>
		{
			return SNew(SBox)
				.HeightOverride(LaneHeight)
				[
					LaneCanvases[LaneIndex]
				];
		};

		TimelineHost->SetContent(
			SNew(SBox)
			.WidthOverride(Width)
			.HeightOverride(TimelineHeight)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneBackground(FLinearColor(0.10f, 0.12f, 0.15f, 1.0f))]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneBackground(FLinearColor(0.035f, 0.040f, 0.050f, 1.0f))]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneBackground(FLinearColor(0.10f, 0.12f, 0.15f, 1.0f))]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneBackground(FLinearColor(0.035f, 0.040f, 0.050f, 1.0f))]
				]
				+ SOverlay::Slot()
				[
					SNew(SBox)
					.WidthOverride(Width)
					.HeightOverride(TimelineHeight)
					.Visibility(EVisibility::HitTestInvisible)
					[
						OverlayCanvas
					]
				]
				+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneEvents(0)]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneEvents(1)]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneEvents(2)]
					+ SVerticalBox::Slot().AutoHeight()[MakeLaneEvents(3)]
				]
			]);

		if (SelectedSequence != 0)
		{
			ShowSelectedEvent();
		}
	}

	void ShowSelectedEvent()
	{
		const FZLStateEvent* Selected = CachedEvents.FindByPredicate(
			[this](const FZLStateEvent& Event) { return Event.Sequence == SelectedSequence; });
		if (!Selected)
		{
			SelectedSequence = 0;
			return;
		}

		FString EventDetails = FString::Printf(
			TEXT("#%llu  Frame %llu  +%.3fs\n%s\n%s\n\n%s"),
			Selected->Sequence,
			Selected->FrameNumber,
			Selected->TimeSeconds,
			*Selected->Source,
			*Selected->Summary,
			*FString::Join(Selected->TouchedPaths, TEXT("\n")));
		if (const FInvalidStateWarning* Warning = InvalidStateWarnings.Find(Selected->Sequence))
		{
			if (Warning->HasWarnings())
			{
				EventDetails += TEXT("\n\n⚠ INVALID STATE REQUEST\n") + FormatInvalidStateWarning(*Warning);
			}
		}
		EventChanges->SetText(FText::FromString(EventDetails));
		EventPayload->SetText(FText::FromString(Selected->PayloadJson));
		EventSnapshot->SetText(FText::FromString(Selected->StateSnapshotJson));
	}

	FReply SelectEvent(uint64 Sequence)
	{
		SelectedSequence = Sequence;
		ShowSelectedEvent();
		RebuildTimeline();
		return FReply::Handled();
	}

	void OnFilterChanged(const FText&)
	{
		RebuildTimeline();
	}

	FReply ToggleRecording()
	{
		if (Recorder)
		{
			if (Recorder->IsRecording())
			{
				Recorder->StopRecording();
			}
			else
			{
				Recorder->StartRecording();
			}
		}
		return FReply::Handled();
	}

	FReply ClearFilters()
	{
		KeyFilterBox->SetText(FText::GetEmpty());
		ValueFilterBox->SetText(FText::GetEmpty());
		RebuildTimeline();
		return FReply::Handled();
	}

	FReply ClearEvents()
	{
		if (Recorder)
		{
			Recorder->Clear();
		}
		SelectedSequence = 0;
		EventChanges->SetText(FText::GetEmpty());
		EventPayload->SetText(FText::GetEmpty());
		EventSnapshot->SetText(FText::GetEmpty());
		RebuildTimeline();
		return FReply::Handled();
	}

	FReply DumpTimelineJson()
	{
		if (!Recorder)
		{
			return FReply::Handled();
		}

		FString FilePath;
		if (Recorder->SaveToFile(FilePath))
		{
			FPlatformApplicationMisc::ClipboardCopy(*FilePath);
			if (EventChanges.IsValid())
			{
				EventChanges->SetText(FText::FromString(FString::Printf(
					TEXT("Saved %d events\n%s\n(path copied to clipboard)"),
					Recorder->GetEvents().Num(),
					*FilePath)));
			}
			UE_LOG(LogTemp, Display, TEXT("[ZLStateEventTimeline] Saved %d events to %s"), Recorder->GetEvents().Num(), *FilePath);
		}
		else if (EventChanges.IsValid())
		{
			EventChanges->SetText(FText::FromString(FString::Printf(TEXT("Failed to write dump:\n%s"), *FilePath)));
		}

		return FReply::Handled();
	}

	FReply LoadTimelineJson()
	{
		if (!Recorder)
		{
			return FReply::Handled();
		}

		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (!DesktopPlatform)
		{
			return FReply::Handled();
		}

		const FString DefaultPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ZLStateEventTimeline"));
		TArray<FString> OutFiles;
		const bool bOpened = DesktopPlatform->OpenFileDialog(
			FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			TEXT("Load State Event Timeline"),
			DefaultPath,
			TEXT(""),
			TEXT("Timeline JSON (*.json)|*.json"),
			EFileDialogFlags::None,
			OutFiles);

		if (!bOpened || OutFiles.Num() == 0)
		{
			return FReply::Handled();
		}

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *OutFiles[0]))
		{
			if (EventChanges.IsValid())
			{
				EventChanges->SetText(FText::FromString(FString::Printf(TEXT("Failed to read:\n%s"), *OutFiles[0])));
			}
			return FReply::Handled();
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			if (EventChanges.IsValid())
			{
				EventChanges->SetText(FText::FromString(TEXT("Failed to parse timeline JSON")));
			}
			return FReply::Handled();
		}

		const TArray<TSharedPtr<FJsonValue>>* EventValues = nullptr;
		if (!Root->TryGetArrayField(TEXT("events"), EventValues) || EventValues == nullptr)
		{
			if (EventChanges.IsValid())
			{
				EventChanges->SetText(FText::FromString(TEXT("Timeline JSON missing events array")));
			}
			return FReply::Handled();
		}

		TArray<FZLStateEvent> LoadedEvents;
		LoadedEvents.Reserve(EventValues->Num());
		for (const TSharedPtr<FJsonValue>& EventValue : *EventValues)
		{
			const TSharedPtr<FJsonObject> EventObject = EventValue.IsValid() ? EventValue->AsObject() : nullptr;
			if (!EventObject.IsValid())
			{
				continue;
			}

			FZLStateEvent Event;
			Event.Sequence = static_cast<uint64>(EventObject->GetNumberField(TEXT("sequence")));
			Event.FrameNumber = static_cast<uint64>(EventObject->GetNumberField(TEXT("frame")));
			Event.TimeSeconds = EventObject->GetNumberField(TEXT("timeSeconds"));
			Event.Type = TypeFromString(EventObject->GetStringField(TEXT("type")));
			Event.Source = EventObject->GetStringField(TEXT("source"));
			Event.Summary = EventObject->GetStringField(TEXT("summary"));
			Event.PayloadJson = JsonValueToString(EventObject->TryGetField(TEXT("payload")));
			Event.StateSnapshotJson = JsonValueToString(EventObject->TryGetField(TEXT("stateSnapshot")));

			const TArray<TSharedPtr<FJsonValue>>* PathValues = nullptr;
			if (EventObject->TryGetArrayField(TEXT("touchedPaths"), PathValues) && PathValues != nullptr)
			{
				for (const TSharedPtr<FJsonValue>& PathValue : *PathValues)
				{
					if (PathValue.IsValid() && PathValue->Type == EJson::String)
					{
						Event.TouchedPaths.Add(PathValue->AsString());
					}
				}
			}

			LoadedEvents.Add(MoveTemp(Event));
		}

		LoadedEvents.Sort([](const FZLStateEvent& A, const FZLStateEvent& B)
		{
			return A.Sequence < B.Sequence;
		});

		const int32 LoadedCount = LoadedEvents.Num();
		Recorder->LoadEvents(MoveTemp(LoadedEvents));
		SelectedSequence = 0;
		if (EventChanges.IsValid())
		{
			EventChanges->SetText(FText::FromString(FString::Printf(
				TEXT("Loaded %d events\n%s"),
				LoadedCount,
				*OutFiles[0])));
		}
		EventPayload->SetText(FText::GetEmpty());
		EventSnapshot->SetText(FText::GetEmpty());
		RebuildTimeline();
		return FReply::Handled();
	}

	FZLStateEventRecorder* Recorder = nullptr;
	uint64 DisplayedRevision = MAX_uint64;
	uint64 SelectedSequence = 0;
	bool bSearchTouchedPaths = true;
	bool bSearchPayload = true;
	bool bSearchSnapshot = false;
	bool bSearchMeta = false;
	TMap<uint64, FInvalidStateWarning> InvalidStateWarnings;
	TArray<FZLStateEvent> CachedEvents;
	TSharedPtr<SEditableTextBox> KeyFilterBox;
	TSharedPtr<SEditableTextBox> ValueFilterBox;
	TSharedPtr<SScrollBox> TimelineScrollBox;
	TSharedPtr<SBox> TimelineHost;
	TSharedPtr<SMultiLineEditableTextBox> EventChanges;
	TSharedPtr<SMultiLineEditableTextBox> EventPayload;
	TSharedPtr<SMultiLineEditableTextBox> EventSnapshot;
};
}

TSharedRef<SWidget> CreateZLStateEventTimelineWidget(FZLStateEventRecorder* Recorder)
{
	return SNew(ZLStateEventTimeline::SZLStateEventTimeline).Recorder(Recorder);
}

#endif

// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginInputHandler.h"
#include "InputStructures.h"
#include "ZLCloudPluginProtocol.h"
#include "ZLCloudPluginModule.h"
#include "JavaScriptKeyCodes.inl"
#include "Layout/ArrangedChildren.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SViewport.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Utils.h"
#include "ZLCloudPluginInputComponent.h"
#include "Engine/Engine.h"
#include "Framework/Application/SlateUser.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Misc/CoreMiscDefines.h"
#include "Input/HittestGrid.h"
#include "ZLCloudPluginDelegates.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "Slate/SceneViewport.h"
#include "SLevelViewport.h"
#endif

DECLARE_LOG_CATEGORY_EXTERN(LogZLCloudPluginInputHandler, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(LogZLCloudPluginInputHandler);

#define USE_NORMALISED_POS 1

#if USE_NORMALISED_POS
const float g_NormalisedScale = 65535.0f;
#else
const float g_NormalisedScale = 1.0f;
#endif

// TODO: Gesture recognition is moving to the browser, so add handlers for the gesture events.
// The gestures supported will be swipe, pinch,

namespace ZLCloudPlugin
{
    FZLCloudPluginInputHandler::FZLCloudPluginInputHandler(TSharedPtr<FZLCloudPluginApplicationWrapper> InApplicationWrapper, const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler)
    : TargetViewport(nullptr)
    , NumActiveTouches(0)
    , bIsMouseActive(false)
    , MessageHandler(InTargetHandler)
    , ZLCloudPluginModule(FZLCloudPluginModule::GetModule())
    , ZLCloudPluginApplicationWrapper(InApplicationWrapper)
    , FocusedPos(FVector2D(-1.0f, -1.0f))
    , UnfocusedPos(FVector2D(-1.0f, -1.0f))
    {
		
        //RegisterMessageHandler("KeyPress", [this](TArray<FString>& message) { HandleOnKeyChar(message); });
        RegisterMessageHandler("KeyUp", [this](TArray<FString>& message) { HandleOnKeyUp(message); });
        RegisterMessageHandler("KeyDown", [this](TArray<FString>& message) { HandleOnKeyDown(message); });

		
        RegisterMessageHandler("TouchStart", [this](TArray<FString>& message) { HandleOnTouchStarted(message); });
        RegisterMessageHandler("TouchMove", [this](TArray<FString>& message) { HandleOnTouchMoved(message); });
        RegisterMessageHandler("TouchEnd", [this](TArray<FString>& message) { HandleOnTouchEnded(message); });

		/*
        RegisterMessageHandler("GamepadAnalog", [this](TArray<FString>& message) { HandleOnControllerAnalog(message); });
        RegisterMessageHandler("GamepadButtonPressed", [this](TArray<FString>& message) { HandleOnControllerButtonPressed(message); });
        RegisterMessageHandler("GamepadButtonReleased", [this](TArray<FString>& message) { HandleOnControllerButtonReleased(message); });
		*/

        RegisterMessageHandler("MouseEnter", [this](TArray<FString>& message) { HandleOnMouseEnter(message); });
        RegisterMessageHandler("MouseLeave", [this](TArray<FString>& message) { HandleOnMouseLeave(message); });
        RegisterMessageHandler("MouseUp", [this](TArray<FString>& message) { HandleOnMouseUp(message); });
        RegisterMessageHandler("MouseDown", [this](TArray<FString>& message) { HandleOnMouseDown(message); });
        RegisterMessageHandler("MouseMove", [this](TArray<FString>& message) { HandleOnMouseMove(message); });
        RegisterMessageHandler("MouseWheel", [this](TArray<FString>& message) { HandleOnMouseWheel(message); });
		RegisterMessageHandler("MouseDouble", [this](TArray<FString>& message) { HandleOnMouseDoubleClick(message); });

		RegisterMessageHandler("JsonData", [this](TArray<FString>& message) { HandleJsonData(message); });
//        RegisterMessageHandler("Command", [this](TArray<FString>& message) { HandleCommand(Ar); });
//        RegisterMessageHandler("UIInteraction", [this](TArray<FString>& message) { HandleUIInteraction(Ar); });
    }

    FZLCloudPluginInputHandler::~FZLCloudPluginInputHandler()
    {
    }

    void FZLCloudPluginInputHandler::RegisterMessageHandler(const FString& MessageType, const TFunction<void(TArray<FString>&)>& Handler)
    {
        TMap<FString, ZLProtocol::FZLCloudPluginInputMessage> Protocol = ZLCloudPluginModule->GetProtocol().ToStreamerProtocol;
        DispatchTable.Add(Protocol.Find(MessageType)->Id, Handler);
    }

	TFunction<void(TArray<FString>&)> FZLCloudPluginInputHandler::FindMessageHandler(const FString& MessageType)
	{
		TMap<FString, ZLProtocol::FZLCloudPluginInputMessage> Protocol = ZLCloudPluginModule->GetProtocol().ToStreamerProtocol;
		return DispatchTable.FindRef(Protocol.Find(MessageType)->Id);
	}

    void FZLCloudPluginInputHandler::Tick(const float InDeltaTime)
    {
#if WITH_EDITOR //Standalone level play still compiles this so GIsEditor also needed
		if (GIsEditor && FZLCloudPluginModule::GetModule()->IsPIERunning())
		{
			bool inputFromPIEViewport = false; //If we dont find a bespoke PIE viewport then set via default logic
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					FSlatePlayInEditorInfo* SlatePlayInEditorSession = GEditor->SlatePlayInEditorMap.Find(Context.ContextHandle);
					if (SlatePlayInEditorSession)
					{
						TWeakPtr<SWindow> currWindow = GetTargetWindow();
						if (SlatePlayInEditorSession->SlatePlayInEditorWindow.IsValid())
						{
							SetTargetViewport(SlatePlayInEditorSession->SlatePlayInEditorWindowViewport->GetViewportWidget());
							SetTargetWindow(SlatePlayInEditorSession->SlatePlayInEditorWindow);
							inputFromPIEViewport = true;
						}
					}
				}
			}

			if (!inputFromPIEViewport)
			{
				FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
				TSharedPtr<SLevelViewport> ActiveLevelViewport = LevelEditorModule.GetFirstActiveLevelViewport();
				if (!ActiveLevelViewport.IsValid())
				{
					return;
				}

				FLevelEditorViewportClient& LevelViewportClient = ActiveLevelViewport->GetLevelViewportClient();
				FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(LevelViewportClient.Viewport);
				SetTargetViewport(SceneViewport->GetViewportWidget());
				SetTargetWindow(SceneViewport->FindWindow());
			}
		}
#endif
    	TouchIndicesProcessedThisFrame.Reset();
    	
        FMessage Message;
		while (Messages.Dequeue(Message))
		{
            (*Message.Handler)(Message.Data);
        }

    	BroadcastActiveTouchMoveEvents();
    }

	void FZLCloudPluginInputHandler::OnMessage(const char* data)
	{
		if (!ZLCloudPlugin::CloudStream2::IsInputHandling())
			return;

		using namespace ZLProtocol;

		//convert from ZL message to UE message
		FString messageData = FString(data);

		TArray<FString> arguments;
		FString TempStr = messageData;
		FString arg;


		//get first arg
		if (TempStr.Split(FString(":"), &arg, &TempStr, ESearchCase::IgnoreCase, ESearchDir::FromStart))
		{
			arguments.Add(arg);
		}
		else
		{
			arguments.Add(TempStr);
		}

		const uint8 MsgType = FCString::Atoi(*arguments[0]);

		//only split the rest if not json data
		if (MsgType != 52)//NOTE: update this if the protocal changes, there may be a better way to do this
		{
			while (TempStr.Split(FString(":"), &arg, &TempStr, ESearchCase::IgnoreCase, ESearchDir::FromStart))
			{
				arguments.Add(arg);
			}
		}
		//Add the rest
		arguments.Add(TempStr);

		TFunction<void(TArray<FString>&)>* Handler = DispatchTable.Find(MsgType);//ZL finds registered message
        if (Handler != nullptr)
        {
            FMessage Message = {
                Handler,   // The function to call
				arguments // The message data
            };
            Messages.Enqueue(Message);
        }
        else
        {
            UE_LOG(LogZLCloudPluginInputHandler, Warning, TEXT("No handler registered for message with id %d"), MsgType);
        }
    }

	void FZLCloudPluginInputHandler::SetTargetWindow(TWeakPtr<SWindow> InWindow)
	{
		TargetWindow = InWindow;
		ZLCloudPluginApplicationWrapper->SetTargetWindow(InWindow);

		if (TargetWindow == nullptr)
		{
			FSlateApplication::Get().OverridePlatformApplication(ZLCloudPluginApplicationWrapper->WrappedApplication);
		}
	}

	TWeakPtr<SWindow> FZLCloudPluginInputHandler::GetTargetWindow()
	{
		return TargetWindow;
	}

	void FZLCloudPluginInputHandler::SetTargetScreenSize(TWeakPtr<FIntPoint> InScreenSize)
	{
		TargetScreenSize = InScreenSize;
	}

	TWeakPtr<FIntPoint> FZLCloudPluginInputHandler::GetTargetScreenSize()
	{
		return TargetScreenSize;
	}

	void FZLCloudPluginInputHandler::SetTargetViewport(TWeakPtr<SViewport> InViewport)
	{
		TargetViewport = InViewport;
	}

	TWeakPtr<SViewport> FZLCloudPluginInputHandler::GetTargetViewport()
	{
		return TargetViewport;
	}

	void FZLCloudPluginInputHandler::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InTargetHandler)
	{
		MessageHandler = InTargetHandler;
	}

	bool FZLCloudPluginInputHandler::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
	{
		return GEngine->Exec(InWorld, Cmd, Ar);
	}

	void FZLCloudPluginInputHandler::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		// TODO: Implement FFB
	}

	void FZLCloudPluginInputHandler::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values)
	{
		// TODO: Implement FFB
	}

/*
    void FZLCloudPluginInputHandler::HandleOnKeyChar(TArray<FString>& message)
    {
        TPayloadOneParam<TCHAR> Payload(Ar);
        UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("KEY_PRESSED: Character = '%c'"), Payload.Param1);
        // A key char event is never repeated, so set it to false. It's value  
        // ultimately doesn't matter as this paramater isn't used later
        MessageHandler->OnKeyChar(Payload.Param1, false);
    }
	*/

	bool FZLCloudPluginInputHandler::FocusIsInputField(TSharedPtr<SWidget> &outFocusWidget)
	{
		bool focusIsInput = false;

		FSlateApplication& SlateApp = FSlateApplication::Get();
		TSharedPtr<SWidget> FocusedWidget = SlateApp.GetUserFocusedWidget(SlateApp.GetUserIndexForKeyboard());

		static FName SEditableTextType(TEXT("SEditableText"));
		static FName SMultiLineEditableTextType(TEXT("SMultiLineEditableText"));
		
		if (FocusedWidget.IsValid())
		{
			focusIsInput = FocusedWidget && (FocusedWidget->GetType() == SEditableTextType || FocusedWidget->GetType() == SMultiLineEditableTextType);
			outFocusWidget = FocusedWidget;
		}

		return focusIsInput;
	}

    void FZLCloudPluginInputHandler::HandleOnKeyDown(TArray<FString>& message)
    {
		int KeyCode = FCString::Atoi(*message[1]);
		bool Repeat = FCString::Atoi(*message[2]) == 1;

		bool bIsRepeat = Repeat;
		const FKey* AgnosticKey = JavaScriptKeyCodeToFKey[KeyCode];

		//TextBoxEntry ref made generic
		TSharedPtr<SWidget> inputFieldWidget = nullptr;
		if (FocusIsInputField(inputFieldWidget))
		{
			HandleOnTextboxEntry(message, inputFieldWidget);
		}
		else if (FilterKey(*AgnosticKey))
		{
			const uint32* KeyPtr;
			const uint32* CharacterPtr;
			FInputKeyManager::Get().GetCodesFromKey(*AgnosticKey, KeyPtr, CharacterPtr);
			uint32 Key = KeyPtr ? *KeyPtr : 0;
			uint32 Character = CharacterPtr ? *CharacterPtr : 0;

			UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("KEY_DOWN: Key = %d; Character = %d; IsRepeat = %s"), Key, Character, bIsRepeat ? TEXT("True") : TEXT("False"));
			MessageHandler->OnKeyDown((int32)Key, (int32)Character, bIsRepeat);
		}
	}

    void FZLCloudPluginInputHandler::HandleOnKeyUp(TArray<FString>& message)
    {
		int KeyCode = FCString::Atoi(*message[1]);

        const FKey* AgnosticKey = JavaScriptKeyCodeToFKey[KeyCode];
        if (FilterKey(*AgnosticKey))
		{
			const uint32* KeyPtr;
			const uint32* CharacterPtr;
			FInputKeyManager::Get().GetCodesFromKey(*AgnosticKey, KeyPtr, CharacterPtr);
			uint32 Key = KeyPtr ? *KeyPtr : 0;
			uint32 Character = CharacterPtr ? *CharacterPtr : 0;

			UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("KEY_UP: Key = %d; Character = %d"), Key, Character);
			MessageHandler->OnKeyUp((int32)Key, (int32)Character, false);
		}
	}

	void FZLCloudPluginInputHandler::HandleOnTextboxEntry(TArray<FString>& message, TSharedPtr<SWidget> inputWidget)
	{
		if (message.Num() > 3) //Old LibZL wont have key characters
		{
			int KeyCode = FCString::Atoi(*message[1]);
			bool Repeat = FCString::Atoi(*message[2]) == 1;
			FString inputChar = message[3];
			const FKey* AgnosticKey = JavaScriptKeyCodeToFKey[KeyCode];

			if (inputWidget.IsValid())
			{
				bool bIsBackspace = KeyCode == 8;
				bool bIsTabOrEnter = KeyCode == 13 || KeyCode == 9;

				FModifierKeysState modKeysState = FModifierKeysState();

				FSlateApplication& SlateApp = FSlateApplication::Get();

				const uint32* KeyCodePtr;
				const uint32* CharCodePtr;
				FInputKeyManager::Get().GetCodesFromKey(*AgnosticKey, KeyCodePtr, CharCodePtr);

				uint32 KeyCodeInt = KeyCodePtr ? *KeyCodePtr : 0;
				uint32 CharCodeInt = CharCodePtr ? *CharCodePtr : 0;

				if (bIsBackspace)
				{
					// Slate editable text fields handle backspace as a special character entry
					FString BackspaceString = FString(TEXT("\b"));
					for (int32 CharIndex = 0; CharIndex < BackspaceString.Len(); CharIndex++)
					{
						TCHAR CharKey = BackspaceString[CharIndex];
						const bool bRepeat = Repeat;
						FCharacterEvent CharacterEvent(CharKey, modKeysState, SlateApp.GetUserIndexForKeyboard(), bRepeat);
						FSlateApplication::Get().ProcessKeyCharEvent(CharacterEvent);
					}
				}
				else if (CharCodeInt != 0)
				{
					FCharacterEvent CharEvent(
						(inputChar.Len() == 1) ? (TCHAR)inputChar[0] : (TCHAR)CharCodeInt,
						modKeysState,
						SlateApp.GetUserIndexForKeyboard(),
						Repeat
					);

					FSlateApplication::Get().ProcessKeyCharEvent(CharEvent);
				}
				else
				{
					FKeyEvent KeyEvent(
						*AgnosticKey,
						modKeysState,
						SlateApp.GetUserIndexForKeyboard(),
						Repeat,
						CharCodeInt,
						KeyCodeInt // Key code (hardware key code)
					);

					FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
					FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
				}
			}
		}
	}

    void FZLCloudPluginInputHandler::HandleOnTouchStarted(TArray<FString>& message)
    {
		uint8 NumTouches = FCString::Atoi(*message[1]);

		for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
		{
			int offset = TouchIdx * 5;

			uint16 PosX = FCString::Atoi(*message[offset + 2]);
			uint16 PosY = FCString::Atoi(*message[offset + 3]);
			uint8 TouchIndexV = FCString::Atoi(*message[offset + 4]);
			uint8 Force = FCString::Atoi(*message[offset + 5]);
			uint8 Valid = FCString::Atoi(*message[offset + 6]);


			// If Touch is valid
			if (Valid != 0)
			{
				FVector2D TouchLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
				const int32 TouchIndex = TouchIndexV;
				const float TouchForce = Force / 255.0f;
				UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("TOUCH_START: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d); Force = %.3f"), TouchIndex, PosX, PosY, static_cast<int>(TouchLocation.X), static_cast<int>(TouchLocation.Y), TouchForce);

				if(InputType == EZLCloudPluginInputType::RouteToWidget)
				{
					// TouchLocation = TouchLocation - TargetViewport.Pin()->GetCachedGeometry().GetAbsolutePosition();
					FWidgetPath WidgetPath = FindRoutingMessageWidget(TouchLocation);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, TouchLocation, TouchLocation, TouchForce, true);
						FSlateApplication::Get().RoutePointerDownEvent(WidgetPath, PointerEvent);
					}
				}
				else if(InputType == EZLCloudPluginInputType::RouteToWindow)
				{
					if (NumActiveTouches == 0 && !bIsMouseActive)
					{
						FSlateApplication::Get().OnCursorSet();
						// Make sure the application is active.
						FSlateApplication::Get().ProcessApplicationActivationEvent(true);

						FVector2D OldCursorLocation = ZLCloudPluginApplicationWrapper->WrappedApplication->Cursor->GetPosition();
						ZLCloudPluginApplicationWrapper->Cursor->SetPosition(OldCursorLocation.X, OldCursorLocation.Y);
						FSlateApplication::Get().OverridePlatformApplication(ZLCloudPluginApplicationWrapper);
					}

					// We must update the user cursor position explicitly before updating the application cursor position
					// as if there's a delta between them, when the touch event is started it will trigger a move
					// resulting in a large 'drag' across the screen
					TSharedPtr<FSlateUser> User = FSlateApplication::Get().GetCursorUser();
					User->SetCursorPosition(TouchLocation);
					ZLCloudPluginApplicationWrapper->Cursor->SetPosition(TouchLocation.X, TouchLocation.Y);
					ZLCloudPluginApplicationWrapper->WrappedApplication->Cursor->SetPosition(TouchLocation.X, TouchLocation.Y);

					MessageHandler->OnTouchStarted(ZLCloudPluginApplicationWrapper->GetWindowUnderCursor(), TouchLocation, TouchForce, TouchIndex, 0); // TODO: ControllerId?
				}

				NumActiveTouches++;
			}
		}
		
		FindFocusedWidget();
    }

    void FZLCloudPluginInputHandler::HandleOnTouchMoved(TArray<FString>& message)
    {
		uint8 NumTouches = FCString::Atoi(*message[1]);

		for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
		{
			int offset = TouchIdx * 5;

			uint16 PosX = FCString::Atoi(*message[offset + 2]);
			uint16 PosY = FCString::Atoi(*message[offset + 3]);
			uint8 TouchIndexV = FCString::Atoi(*message[offset + 4]);
			uint8 Force = FCString::Atoi(*message[offset + 5]);
			uint8 Valid = FCString::Atoi(*message[offset + 6]);

			if (Valid != 0)
			{
				FVector2D TouchLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
				const int32 TouchIndex = TouchIndexV;
				const float TouchForce = Force / 255.0f;
                UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("TOUCH_MOVE: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d); Force = %.3f"), TouchIndex, PosX, PosY, static_cast<int>(TouchLocation.X), static_cast<int>(TouchLocation.Y), TouchForce);

				FCachedTouchEvent& TouchEvent = CachedTouchEvents.FindOrAdd(TouchIndex);
				TouchEvent.Force = TouchForce;
				TouchEvent.ControllerIndex = 0;
					
				if(InputType == EZLCloudPluginInputType::RouteToWidget)
				{
					// TouchLocation = TouchLocation - TargetViewport.Pin()->GetCachedGeometry().GetAbsolutePosition();
					TouchEvent.Location = TouchLocation;
					FWidgetPath WidgetPath = FindRoutingMessageWidget(TouchLocation);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, TouchLocation, LastTouchLocation, TouchForce, true);
						FSlateApplication::Get().RoutePointerMoveEvent(WidgetPath, PointerEvent, false);
					}

					LastTouchLocation = TouchLocation;
				}
				else if(InputType == EZLCloudPluginInputType::RouteToWindow)
				{
					TouchEvent.Location = TouchLocation;
					MessageHandler->OnTouchMoved(TouchEvent.Location, TouchEvent.Force, TouchIndex, TouchEvent.ControllerIndex); // TODO: ControllerId?	
				}
				TouchIndicesProcessedThisFrame.Add(TouchIndex);
            }
        }
    }

    void FZLCloudPluginInputHandler::HandleOnTouchEnded(TArray<FString>& message)
    {
		uint8 NumTouches = FCString::Atoi(*message[1]);

		for (uint8 TouchIdx = 0; TouchIdx < NumTouches; TouchIdx++)
		{
			int offset = TouchIdx * 5;

			uint16 PosX = FCString::Atoi(*message[offset + 2]);
			uint16 PosY = FCString::Atoi(*message[offset + 3]);
			uint8 TouchIndexV = FCString::Atoi(*message[offset + 4]);
			uint8 Force = FCString::Atoi(*message[offset + 5]);
			uint8 Valid = FCString::Atoi(*message[offset + 6]);


			// Always allowing the "up" events regardless of in or outside the valid region so
			// states aren't stuck "down". Might want to uncomment this if it causes other issues.
			// if(Touch.Param5 != 0)
			{
				FVector2D TouchLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
				const int32 TouchIndex = TouchIndexV;
				UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("TOUCH_END: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d)"), TouchIndexV, PosX, PosY, static_cast<int>(TouchLocation.X), static_cast<int>(TouchLocation.Y));

				if(InputType == EZLCloudPluginInputType::RouteToWidget)
				{
					// TouchLocation = TouchLocation - TargetViewport.Pin()->GetCachedGeometry().GetAbsolutePosition();
					FWidgetPath WidgetPath = FindRoutingMessageWidget(TouchLocation);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						float TouchForce = 0.0f;
						FPointerEvent PointerEvent(0, TouchIndex, TouchLocation, TouchLocation, TouchForce, true);
						FSlateApplication::Get().RoutePointerUpEvent(WidgetPath, PointerEvent);
					}
				}
				else if(InputType == EZLCloudPluginInputType::RouteToWindow)
				{
					MessageHandler->OnTouchEnded(TouchLocation, TouchIndex, 0); // TODO: ControllerId?
				}

            	CachedTouchEvents.Remove(TouchIndex);
				NumActiveTouches--;
			}
		}

		// If there's no remaining touches, and there is also no mouse over the player window
		// then set the platform application back to its default. We need to set it back to default
		// so that people using the editor (if editor streaming) can click on buttons outside the target window
		// and also have the correct cursor (ZLCloudStream forces default cursor)
		if (NumActiveTouches == 0 && !bIsMouseActive && InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			FVector2D OldCursorLocation = ZLCloudPluginApplicationWrapper->Cursor->GetPosition();
			ZLCloudPluginApplicationWrapper->WrappedApplication->Cursor->SetPosition(OldCursorLocation.X, OldCursorLocation.Y);
			FSlateApplication::Get().OverridePlatformApplication(ZLCloudPluginApplicationWrapper->WrappedApplication);
		}
	}

	/*

	void FZLCloudPluginInputHandler::HandleOnControllerAnalog(FMemoryReader Ar)
	{
		TPayloadThreeParam<uint8, uint8, double> Payload(Ar);

		FInputDeviceId ControllerId = FInputDeviceId::CreateFromInternalId((int32)Payload.Param1);
		FGamepadKeyNames::Type Button = ConvertAxisIndexToGamepadAxis(Payload.Param2);
		float AnalogValue = (float)Payload.Param3;
		FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();

		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("GAMEPAD_ANALOG: ControllerId = %d; KeyName = %s; AnalogValue = %.4f;"), ControllerId.GetId(), *Button.ToString(), AnalogValue);
        MessageHandler->OnControllerAnalog(Button, UserId, ControllerId, AnalogValue);
	}

    void FZLCloudPluginInputHandler::HandleOnControllerButtonPressed(FMemoryReader Ar)
    {
        TPayloadThreeParam<uint8, uint8, uint8> Payload(Ar);

        FInputDeviceId ControllerId = FInputDeviceId::CreateFromInternalId((int32) Payload.Param1);
        FGamepadKeyNames::Type Button = ConvertButtonIndexToGamepadButton(Payload.Param2);
        bool bIsRepeat = Payload.Param3 != 0;
        FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();
        
        UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("GAMEPAD_PRESSED: ControllerId = %d; KeyName = %s; IsRepeat = %s;"), ControllerId.GetId(), *Button.ToString(), bIsRepeat ? TEXT("True") : TEXT("False"));
        MessageHandler->OnControllerButtonPressed(Button, UserId, ControllerId, bIsRepeat);
    }

    void FZLCloudPluginInputHandler::HandleOnControllerButtonReleased(FMemoryReader Ar)
    {
        TPayloadTwoParam<uint8, uint8> Payload(Ar);

        FInputDeviceId ControllerId = FInputDeviceId::CreateFromInternalId((int32) Payload.Param1);
        FGamepadKeyNames::Type Button = ConvertButtonIndexToGamepadButton(Payload.Param2);
        FPlatformUserId UserId = IPlatformInputDeviceMapper::Get().GetPrimaryPlatformUser();

        UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("GAMEPAD_RELEASED: ControllerId = %d; KeyName = %s;"), ControllerId.GetId(), *Button.ToString());
        MessageHandler->OnControllerButtonReleased(Button, UserId, ControllerId, false);
    }

	*/

    /**
     * Mouse events
     */
    void FZLCloudPluginInputHandler::HandleOnMouseEnter(TArray<FString>& message)
    {
        if(NumActiveTouches == 0 && !bIsMouseActive)
        {
            FSlateApplication::Get().OnCursorSet();
            FSlateApplication::Get().OverridePlatformApplication(ZLCloudPluginApplicationWrapper);
            // Make sure the application is active.
            FSlateApplication::Get().ProcessApplicationActivationEvent(true);
        }
        
        bIsMouseActive = true;
        UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_ENTER"));
    }

    void FZLCloudPluginInputHandler::HandleOnMouseLeave(TArray<FString>& message)
    {
        if(NumActiveTouches == 0)
        {
            // Restore normal application layer if there is no active touches and MouseEnter hasn't been triggered
		    FSlateApplication::Get().OverridePlatformApplication(ZLCloudPluginApplicationWrapper->WrappedApplication);
        }
        bIsMouseActive = false;
	    UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_LEAVE"));
    }

    void FZLCloudPluginInputHandler::HandleOnMouseUp(TArray<FString>& message)
    {
		// Ensure we have wrapped the slate application at this point
		if(!bIsMouseActive)
		{
			HandleOnMouseEnter(message);
		}

		int PosX = FCString::Atoi(*(message[1]));
		int PosY = FCString::Atoi(*(message[2]));
		
		int ButtonV = 0;
		if (message.Num() > 3)
		{
			ButtonV = FCString::Atoi(*(message[3]));
		}

        EMouseButtons::Type Button = static_cast<EMouseButtons::Type>(ButtonV);
        UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_UP: Button = %d"), Button);

		if(InputType == EZLCloudPluginInputType::RouteToWidget)
		{
			FSlateApplication& SlateApplication = FSlateApplication::Get();
			FWidgetPath WidgetPath = FindRoutingMessageWidget(SlateApplication.GetCursorPos());
			
			if (WidgetPath.IsValid())
			{
				FScopedSwitchWorldHack SwitchWorld(WidgetPath);
				FKey Key = TranslateMouseButtonToKey(Button);

				FPointerEvent MouseEvent(
					SlateApplication.GetUserIndexForMouse(),
					FSlateApplicationBase::CursorPointerIndex,
					SlateApplication.GetCursorPos(),
					SlateApplication.GetLastCursorPos(),
					SlateApplication.GetPressedMouseButtons(),
					Key,
					0,
					SlateApplication.GetPlatformApplication()->GetModifierKeys()
				);

				SlateApplication.RoutePointerUpEvent(WidgetPath, MouseEvent);
			}
		}
		else if(InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			if(Button != EMouseButtons::Type::Invalid)
			{
				MessageHandler->OnMouseUp(Button);
			}
		}
        
    }

    void FZLCloudPluginInputHandler::HandleOnMouseDown(TArray<FString>& message)
    {
		// Ensure we have wrapped the slate application at this point
		if(!bIsMouseActive)
		{
			HandleOnMouseEnter(message);
		}

		int PosX = FCString::Atoi(*(message[1]));
		int PosY = FCString::Atoi(*(message[2]));

		int ButtonV = 0;
		if (message.Num() > 3)
		{
			ButtonV = FCString::Atoi(*(message[3]));
		}

        FVector2D ScreenLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
        EMouseButtons::Type Button = static_cast<EMouseButtons::Type>(ButtonV);
		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_DOWN: Button = %d; Pos = (%.4f, %.4f)"), Button, ScreenLocation.X, ScreenLocation.Y);

		// Set cursor pos on mouse down - we may not have moved if this is the very first click
		FSlateApplication& SlateApplication = FSlateApplication::Get();
		SlateApplication.OnCursorSet();
		// Force window focus
		SlateApplication.ProcessApplicationActivationEvent(true);

		if(InputType == EZLCloudPluginInputType::RouteToWidget)
		{
			FWidgetPath WidgetPath = FindRoutingMessageWidget(ScreenLocation);
			
			if (WidgetPath.IsValid())
			{
				FScopedSwitchWorldHack SwitchWorld(WidgetPath);

				FKey Key = TranslateMouseButtonToKey(Button);

				FPointerEvent MouseEvent(
					SlateApplication.GetUserIndexForMouse(),
					FSlateApplicationBase::CursorPointerIndex,
					ScreenLocation,
					SlateApplication.GetLastCursorPos(),
					SlateApplication.GetPressedMouseButtons(),
					Key,
					0,
					SlateApplication.GetPlatformApplication()->GetModifierKeys()
				);

				SlateApplication.RoutePointerDownEvent(WidgetPath, MouseEvent);
			}
		}
		else if(InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			MessageHandler->OnMouseDown(ZLCloudPluginApplicationWrapper->GetWindowUnderCursor(), Button, ScreenLocation);
		}		
	}

    void FZLCloudPluginInputHandler::HandleOnMouseMove(TArray<FString>& message)
    {
		int PosX = FCString::Atoi(*(message[1]));
		int PosY = FCString::Atoi(*(message[2]));
		int DeltaX = FCString::Atoi(*(message[3]));
		int DeltaY = FCString::Atoi(*(message[4]));

        FIntPoint ScreenLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
        FIntPoint Delta = ConvertFromNormalizedScreenLocation(FVector2D(DeltaX / g_NormalisedScale, DeltaY / g_NormalisedScale), false);

		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_MOVE: Pos = (%d, %d); Delta = (%d, %d)"), ScreenLocation.X, ScreenLocation.Y, Delta.X, Delta.Y);
		FSlateApplication& SlateApplication = FSlateApplication::Get();
		SlateApplication.OnCursorSet();
		ZLCloudPluginApplicationWrapper->Cursor->SetPosition(ScreenLocation.X, ScreenLocation.Y);

		if(InputType == EZLCloudPluginInputType::RouteToWidget)
		{
			FWidgetPath WidgetPath = FindRoutingMessageWidget(ScreenLocation);
			
			if (WidgetPath.IsValid())
			{
				FScopedSwitchWorldHack SwitchWorld(WidgetPath);

				FPointerEvent MouseEvent(
					SlateApplication.GetUserIndexForMouse(),
					FSlateApplicationBase::CursorPointerIndex,
					SlateApplication.GetCursorPos(),
					SlateApplication.GetLastCursorPos(),
					FVector2D(Delta.X, Delta.Y), 
					SlateApplication.GetPressedMouseButtons(),
					SlateApplication.GetPlatformApplication()->GetModifierKeys()
				);

				SlateApplication.RoutePointerMoveEvent(WidgetPath, MouseEvent, false);
			}
		}
		else if(InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			MessageHandler->OnRawMouseMove(Delta.X, Delta.Y);
		}
	}

    void FZLCloudPluginInputHandler::HandleOnMouseWheel(TArray<FString>& message)
    {

		int PosX = FCString::Atoi(*(message[1]));
		int PosY = FCString::Atoi(*(message[2]));
		float Delta = FCString::Atof(*(message[3]));

        FIntPoint ScreenLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
        const float SpinFactor = 1 / 120.0f;
		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_WHEEL: Delta = %f; Pos = (%d, %d)"), Delta, ScreenLocation.X, ScreenLocation.Y);

		if(InputType == EZLCloudPluginInputType::RouteToWidget)
		{
			FWidgetPath WidgetPath = FindRoutingMessageWidget(ScreenLocation);
			
			if (WidgetPath.IsValid())
			{
				FScopedSwitchWorldHack SwitchWorld(WidgetPath);

				FSlateApplication& SlateApplication = FSlateApplication::Get();

				FPointerEvent MouseEvent(
					SlateApplication.GetUserIndexForMouse(),
					FSlateApplicationBase::CursorPointerIndex,
					SlateApplication.GetCursorPos(),
					SlateApplication.GetCursorPos(),
					SlateApplication.GetPressedMouseButtons(),
					EKeys::Invalid,
					Delta * SpinFactor,
					SlateApplication.GetPlatformApplication()->GetModifierKeys()
				);

				SlateApplication.RouteMouseWheelOrGestureEvent(WidgetPath, MouseEvent, nullptr);
			}
		}
		else if(InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			MessageHandler->OnMouseWheel(Delta * SpinFactor, ScreenLocation);
		}
    }

	void FZLCloudPluginInputHandler::HandleOnMouseDoubleClick(TArray<FString>& message)
	{
		int PosX = FCString::Atoi(*(message[1]));
		int PosY = FCString::Atoi(*(message[2]));
		int ButtonV = 0;// FCString::Atoi(*(message[3]));

        FVector2D ScreenLocation = ConvertFromNormalizedScreenLocation(FVector2D(PosX / g_NormalisedScale, PosY / g_NormalisedScale));
        EMouseButtons::Type Button = static_cast<EMouseButtons::Type>(ButtonV);

		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("MOUSE_DOWN: Button = %d; Pos = (%.4f, %.4f)"), Button, ScreenLocation.X, ScreenLocation.Y);
		// Force window focus
		FSlateApplication& SlateApplication = FSlateApplication::Get();
		SlateApplication.ProcessApplicationActivationEvent(true);

		if(InputType == EZLCloudPluginInputType::RouteToWidget)
		{
			FWidgetPath WidgetPath = FindRoutingMessageWidget(ScreenLocation);
			
			if (WidgetPath.IsValid())
			{
				FScopedSwitchWorldHack SwitchWorld(WidgetPath);
				FKey Key = TranslateMouseButtonToKey( Button );

				FPointerEvent MouseEvent(
					SlateApplication.GetUserIndexForMouse(),
					FSlateApplicationBase::CursorPointerIndex,
					SlateApplication.GetCursorPos(),
					SlateApplication.GetLastCursorPos(),
					SlateApplication.GetPressedMouseButtons(),
					Key,
					0,
					SlateApplication.GetPlatformApplication()->GetModifierKeys()
				);

				SlateApplication.RoutePointerDoubleClickEvent(WidgetPath, MouseEvent);
			}
		}
		else if(InputType == EZLCloudPluginInputType::RouteToWindow)
		{
			MessageHandler->OnMouseDoubleClick(ZLCloudPluginApplicationWrapper->GetWindowUnderCursor(), Button, ScreenLocation);
		}
	}

	/**
	 * Json data handling
	 */
	 void FZLCloudPluginInputHandler::HandleJsonData(TArray<FString>& message)
	 {
		 FString jsonData = *message[1];

		 if (UZLCloudPluginDelegates* Delegates = UZLCloudPluginDelegates::GetZLCloudPluginDelegates())
		 {
			 Delegates->OnRecieveData.Broadcast(jsonData);
		 }
	 }

    /**
     * Command handling
     */

	/*
    void FZLCloudPluginInputHandler::HandleCommand(FMemoryReader Ar)
    {
        FString Res;
        Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
        Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());

		FString Descriptor = Res.Mid(MessageHeaderOffset);
		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("Command: %s"), *Descriptor);

		bool bSuccess = false;
		

	}
	*/

    /**
     * UI Interaction handling
     */

	/*
    void FZLCloudPluginInputHandler::HandleUIInteraction(FMemoryReader Ar)
    {
        FString Res;
        Res.GetCharArray().SetNumUninitialized(Ar.TotalSize() / 2 + 1);
        Ar.Serialize(Res.GetCharArray().GetData(), Ar.TotalSize());

		FString Descriptor = Res.Mid(MessageHeaderOffset);

		UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("UIInteraction: %s"), *Descriptor);
		for (UZLCloudPluginInput* InputComponent : ZLCloudPluginModule->GetInputComponents())
		{
			InputComponent->OnInputEvent.Broadcast(Descriptor);
		}
	}
	*/

	FIntPoint FZLCloudPluginInputHandler::ConvertFromNormalizedScreenLocation(const FVector2D& ScreenLocation, bool bIncludeOffset)
	{
		FIntPoint OutVector((int32)ScreenLocation.X, (int32)ScreenLocation.Y);

		if (TSharedPtr<SWindow> ApplicationWindow = TargetWindow.Pin())
		{
			FVector2D WindowOrigin = ApplicationWindow->GetPositionInScreen();
			if (TSharedPtr<SViewport> ViewportWidget = TargetViewport.Pin())
			{
				FGeometry InnerWindowGeometry = ApplicationWindow->GetWindowGeometryInWindow();

				// Find the widget path relative to the window
				FArrangedChildren JustWindow(EVisibility::Visible);
				JustWindow.AddWidget(FArrangedWidget(ApplicationWindow.ToSharedRef(), InnerWindowGeometry));

				FWidgetPath PathToWidget(ApplicationWindow.ToSharedRef(), JustWindow);
				if (PathToWidget.ExtendPathTo(FWidgetMatcher(ViewportWidget.ToSharedRef()), EVisibility::Visible))
				{
					FArrangedWidget ArrangedWidget = PathToWidget.FindArrangedWidget(ViewportWidget.ToSharedRef()).Get(FArrangedWidget::GetNullWidget());

					FVector2D WindowClientOffset = ArrangedWidget.Geometry.GetAbsolutePosition();
					FVector2D WindowClientSize = ArrangedWidget.Geometry.GetAbsoluteSize();
				#if USE_NORMALISED_POS
					FVector2D OutTemp = bIncludeOffset
						? (ScreenLocation * WindowClientSize) + WindowOrigin + WindowClientOffset
						: (ScreenLocation * WindowClientSize);
				#else
					FVector2D OutTemp = bIncludeOffset ? WindowOrigin + WindowClientOffset + (ScreenLocation /* WindowClientSize*/) : (ScreenLocation /*WindowClientSize*/);	
				#endif
					UE_LOG(LogZLCloudPluginInputHandler, Verbose, TEXT("%.4f, %.4f"), ScreenLocation.X, ScreenLocation.Y);
					OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
				}
			}
			else
			{
				FVector2D SizeInScreen = ApplicationWindow->GetSizeInScreen();
			#if USE_NORMALISED_POS
				FVector2D OutTemp = bIncludeOffset
					? (SizeInScreen * ScreenLocation) + ApplicationWindow->GetPositionInScreen()
					: (SizeInScreen * ScreenLocation);
			#else
				FVector2D OutTemp = /*SizeInScreen */ ScreenLocation;
			#endif
				OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
			}
		}
		/*
		else if (TSharedPtr<FIntRect> ScreenRectPtr = TargetScreenRect.Pin())
		{
			FIntRect ScreenRect = *ScreenRectPtr;
			FIntPoint SizeInScreen = ScreenRect.Max - ScreenRect.Min;
			FVector2D OutTemp = FVector2D(SizeInScreen.X, SizeInScreen.Y) * ScreenLocation + (bIncludeOffset ? FVector2D(ScreenRect.Min.X, ScreenRect.Min.Y) : FVector2D(0, 0));
			OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
		}*/
		else if (TSharedPtr<FIntPoint> ScreenSize = TargetScreenSize.Pin())
		{
			//UE_LOG(LogZLCloudPluginInputHandler, Warning, TEXT("You're using deprecated functionality by setting a target screen size. This functionality will be removed in later versions. Please use SetTargetScreenRect instead!"));
			FIntPoint SizeInScreen = *ScreenSize;
		#if USE_NORMALISED_POS
			FVector2D OutTemp = FVector2D(SizeInScreen) * ScreenLocation;
		#else
			FVector2D OutTemp = /*FVector2D(SizeInScreen) */ ScreenLocation;
		#endif
			OutVector = FIntPoint((int32)OutTemp.X, (int32)OutTemp.Y);
		}

		return OutVector;
	}

	bool FZLCloudPluginInputHandler::FilterKey(const FKey& Key)
	{
		const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();

		if (Settings && Settings->FilteredKeys.Contains(Key))
		{
			return true;
		}

		return false;
	}

	FGamepadKeyNames::Type FZLCloudPluginInputHandler::ConvertAxisIndexToGamepadAxis(uint8 AnalogAxis)
	{
		switch (AnalogAxis)
		{
			case 1:
			{
				return FGamepadKeyNames::LeftAnalogX;
			}
			break;
			case 2:
			{
				return FGamepadKeyNames::LeftAnalogY;
			}
			break;
			case 3:
			{
				return FGamepadKeyNames::RightAnalogX;
			}
			break;
			case 4:
			{
				return FGamepadKeyNames::RightAnalogY;
			}
			break;
			case 5:
			{
				return FGamepadKeyNames::LeftTriggerAnalog;
			}
			break;
			case 6:
			{
				return FGamepadKeyNames::RightTriggerAnalog;
			}
			break;
			default:
			{
				return FGamepadKeyNames::Invalid;
			}
			break;
		}
	}

	FGamepadKeyNames::Type FZLCloudPluginInputHandler::ConvertButtonIndexToGamepadButton(uint8 ButtonIndex)
	{
		switch (ButtonIndex)
		{
			case 0:
			{
				return FGamepadKeyNames::FaceButtonBottom;
			}
			case 1:
			{
				return FGamepadKeyNames::FaceButtonRight;
			}
			break;
			case 2:
			{
				return FGamepadKeyNames::FaceButtonLeft;
			}
			break;
			case 3:
			{
				return FGamepadKeyNames::FaceButtonTop;
			}
			break;
			case 4:
			{
				return FGamepadKeyNames::LeftShoulder;
			}
			break;
			case 5:
			{
				return FGamepadKeyNames::RightShoulder;
			}
			break;
			case 6:
			{
				return FGamepadKeyNames::LeftTriggerThreshold;
			}
			break;
			case 7:
			{
				return FGamepadKeyNames::RightTriggerThreshold;
			}
			break;
			case 8:
			{
				return FGamepadKeyNames::SpecialLeft;
			}
			break;
			case 9:
			{
				return FGamepadKeyNames::SpecialRight;
			}
			case 10:
			{
				return FGamepadKeyNames::LeftThumb;
			}
			break;
			case 11:
			{
				return FGamepadKeyNames::RightThumb;
			}
			break;
			case 12:
			{
				return FGamepadKeyNames::DPadUp;
			}
			break;
			case 13:
			{
				return FGamepadKeyNames::DPadDown;
			}
			case 14:
			{
				return FGamepadKeyNames::DPadLeft;
			}
			break;
			case 15:
			{
				return FGamepadKeyNames::DPadRight;
			}
			break;
			default:
			{
				return FGamepadKeyNames::Invalid;
			}
			break;
		}
	}

	void FZLCloudPluginInputHandler::BroadcastActiveTouchMoveEvents()
	{
    	if (!ensure(MessageHandler))
    	{
    		return;
    	}
    	
    	for (TPair<int32, FCachedTouchEvent> CachedTouchEvent : CachedTouchEvents)
    	{
    		const int32& TouchIndex = CachedTouchEvent.Key;
    		const FCachedTouchEvent& TouchEvent = CachedTouchEvent.Value;

    		// Only broadcast events that haven't already been fired this frame
    		if (!TouchIndicesProcessedThisFrame.Contains(TouchIndex))
    		{
				if(InputType == EZLCloudPluginInputType::RouteToWidget)
				{
					FWidgetPath WidgetPath = FindRoutingMessageWidget(TouchEvent.Location);
					
					if (WidgetPath.IsValid())
					{
						FScopedSwitchWorldHack SwitchWorld(WidgetPath);
						FPointerEvent PointerEvent(0, TouchIndex, TouchEvent.Location, LastTouchLocation, TouchEvent.Force, true);
						FSlateApplication::Get().RoutePointerMoveEvent(WidgetPath, PointerEvent, false);
					}
				}
				else if(InputType == EZLCloudPluginInputType::RouteToWindow)
				{
					MessageHandler->OnTouchMoved(TouchEvent.Location, TouchEvent.Force, TouchIndex, TouchEvent.ControllerIndex);
				}
    		}
    	}
	}

	FKey FZLCloudPluginInputHandler::TranslateMouseButtonToKey(const EMouseButtons::Type Button)
	{
		FKey Key = EKeys::Invalid;

		switch( Button )
		{
		case EMouseButtons::Left:
			Key = EKeys::LeftMouseButton;
			break;
		case EMouseButtons::Middle:
			Key = EKeys::MiddleMouseButton;
			break;
		case EMouseButtons::Right:
			Key = EKeys::RightMouseButton;
			break;
		case EMouseButtons::Thumb01:
			Key = EKeys::ThumbMouseButton;
			break;
		case EMouseButtons::Thumb02:
			Key = EKeys::ThumbMouseButton2;
			break;
		}

		return Key;
	}

	void FZLCloudPluginInputHandler::FindFocusedWidget()
	{
		FSlateApplication::Get().ForEachUser([this](FSlateUser& User) {
			TSharedPtr<SWidget> FocusedWidget = User.GetFocusedWidget();

			static FName SEditableTextType(TEXT("SEditableText"));
			static FName SMultiLineEditableTextType(TEXT("SMultiLineEditableText"));
			bool bEditable = FocusedWidget && (FocusedWidget->GetType() == SEditableTextType || FocusedWidget->GetType() == SMultiLineEditableTextType);

			// Check to see if the focus has changed.
			FVector2D Pos = bEditable ? FocusedWidget->GetCachedGeometry().GetAbsolutePosition() : UnfocusedPos;
			if (Pos != FocusedPos)
			{
				FocusedPos = Pos;

				// Tell the browser that the focus has changed.
				TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
				JsonObject->SetStringField(TEXT("command"), TEXT("onScreenKeyboard"));
				JsonObject->SetBoolField(TEXT("showOnScreenKeyboard"), bEditable);

				if (bEditable)
				{
					FVector2D NormalizedLocation;
					TSharedPtr<SWindow> ApplicationWindow = TargetWindow.Pin();
					if (ApplicationWindow.IsValid())
					{
						FVector2D WindowOrigin = ApplicationWindow->GetPositionInScreen();
						if (TargetViewport.IsValid())
						{
							TSharedPtr<SViewport> ViewportWidget = TargetViewport.Pin();

							if (ViewportWidget.IsValid())
							{
								FGeometry InnerWindowGeometry = ApplicationWindow->GetWindowGeometryInWindow();

								// Find the widget path relative to the window
								FArrangedChildren JustWindow(EVisibility::Visible);
								JustWindow.AddWidget(FArrangedWidget(ApplicationWindow.ToSharedRef(), InnerWindowGeometry));

								FWidgetPath PathToWidget(ApplicationWindow.ToSharedRef(), JustWindow);
								if (PathToWidget.ExtendPathTo(FWidgetMatcher(ViewportWidget.ToSharedRef()), EVisibility::Visible))
								{
									FArrangedWidget ArrangedWidget = PathToWidget.FindArrangedWidget(ViewportWidget.ToSharedRef()).Get(FArrangedWidget::GetNullWidget());

									FVector2D WindowClientOffset = ArrangedWidget.Geometry.GetAbsolutePosition();
									FVector2D WindowClientSize = ArrangedWidget.Geometry.GetAbsoluteSize();

									Pos = Pos - WindowClientOffset;
									NormalizedLocation = FVector2D(Pos / WindowClientSize);
								}
							}
						}
						else
						{
							FVector2D SizeInScreen = ApplicationWindow->GetSizeInScreen();
							NormalizedLocation = FVector2D(Pos / SizeInScreen);
						}
					}

					NormalizedLocation *= uint16_MAX;
					// ConvertToNormalizedScreenLocation(Pos, NormalizedLocation);
					JsonObject->SetNumberField(TEXT("x"), static_cast<uint16>(NormalizedLocation.X));
					JsonObject->SetNumberField(TEXT("y"), static_cast<uint16>(NormalizedLocation.Y));
				}

				FString Descriptor;
				TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Descriptor);
				FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

				//ZLTODO..
				/*
				ZLCloudPluginModule->ForEachStreamer([&Descriptor, this](TSharedPtr<IZLCloudPluginStreamer> Streamer){
					Streamer->SendPlayerMessage(ZLCloudPluginModule->GetProtocol().FromStreamerProtocol.Find("Command")->Id, Descriptor);
				});
				*/
			}
		});
    }

	FWidgetPath FZLCloudPluginInputHandler::FindRoutingMessageWidget(const FVector2D& Location) const
	{
		if (TSharedPtr<SWindow> PlaybackWindowPinned = TargetWindow.Pin())
		{
			if (PlaybackWindowPinned->AcceptsInput())
			{
				bool bIgnoreEnabledStatus = false;
				TArray<FWidgetAndPointer> WidgetsAndCursors = PlaybackWindowPinned->GetHittestGrid().GetBubblePath(Location, FSlateApplication::Get().GetCursorRadius(), bIgnoreEnabledStatus);
				return FWidgetPath(MoveTemp(WidgetsAndCursors));
			}
		}
		return FWidgetPath();
	}
} // namespace ZLCloudPlugin

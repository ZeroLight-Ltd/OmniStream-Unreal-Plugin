// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "GenericPlatform/GenericApplicationMessageHandler.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
//#include "WebRTCIncludes.h"
#include "IZLCloudPluginModule.h"
#include "InputCoreTypes.h"
#include "ZLCloudPluginApplicationWrapper.h"
#include "IZLCloudPluginInputHandler.h"

namespace ZLCloudPlugin
{
	class FZLCloudPluginInputHandler : public IZLCloudPluginInputHandler
	{
	public:
		FZLCloudPluginInputHandler(TSharedPtr<FZLCloudPluginApplicationWrapper> InApplicationWrapper, const TSharedPtr<FGenericApplicationMessageHandler>& InTargetHandler);

		virtual ~FZLCloudPluginInputHandler();

		virtual void Tick(float DeltaTime) override;

		/** Poll for controller state and send events if needed */
		virtual void SendControllerEvents() override {};

		/** Set which MessageHandler will route input  */
		virtual void SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InTargetHandler) override;

		/** Exec handler to allow console commands to be passed through for debugging */
		virtual bool Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) override;
		/**
		 * IInputInterface pass through functions
		 */
		virtual void SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override;

		virtual void SetChannelValues(int32 ControllerId, const FForceFeedbackValues& values) override;

		virtual void OnMessage(const char* data) override;

		virtual void SetTargetWindow(TWeakPtr<SWindow> InWindow) override;
		virtual TWeakPtr<SWindow> GetTargetWindow() override;

		virtual void SetTargetViewport(TWeakPtr<SViewport> InViewport) override;
		virtual TWeakPtr<SViewport> GetTargetViewport() override;

		virtual void SetTargetScreenSize(TWeakPtr<FIntPoint> InScreenSize) override;
		virtual TWeakPtr<FIntPoint> GetTargetScreenSize() override;

		virtual bool IsFakingTouchEvents() const override { return bFakingTouchEvents; }

        virtual void RegisterMessageHandler(const FString& MessageType, const TFunction<void(TArray<FString>&)>& Handler) override;
		virtual TFunction<void(TArray<FString>&)> FindMessageHandler(const FString& MessageType) override;

		virtual void SetInputType(EZLCloudPluginInputType InInputType) override { InputType = InInputType; };

    protected:

		struct FMessage
		{
			TFunction<void(TArray<FString>&)>* Handler;
			TArray<FString> Data;
		};

        /**
         * Key press handling
         */
        //virtual void HandleOnKeyChar(TArray<FString>& message;
        virtual void HandleOnKeyDown(TArray<FString>& message);
		virtual void HandleOnTextboxEntry(TArray<FString>& message, TSharedPtr<SWidget> inputWidget);
        virtual void HandleOnKeyUp(TArray<FString>& message);
        /**
         * Touch handling
         */
        virtual void HandleOnTouchStarted(TArray<FString>& message);
        virtual void HandleOnTouchMoved(TArray<FString>& message);
        virtual void HandleOnTouchEnded(TArray<FString>& message);
        /**
         * Controller handling
         */
//        virtual void HandleOnControllerAnalog(FMemoryReader Ar);
//        virtual void HandleOnControllerButtonPressed(FMemoryReader Ar);
///        virtual void HandleOnControllerButtonReleased(FMemoryReader Ar);
        /**
         * Mouse handling
         */
        virtual void HandleOnMouseEnter(TArray<FString>& message);
        virtual void HandleOnMouseLeave(TArray<FString>& message);
        virtual void HandleOnMouseUp(TArray<FString>& message);
        virtual void HandleOnMouseDown(TArray<FString>& message);
        virtual void HandleOnMouseMove(TArray<FString>& message);
        virtual void HandleOnMouseWheel(TArray<FString>& message);
		virtual void HandleOnMouseDoubleClick(TArray<FString>& message);
		/**
		 * JsonData handling
		 */
        virtual void HandleJsonData(TArray<FString>& message);
        /**
         * Command handling
         */
 //       virtual void HandleCommand(FMemoryReader Ar);
        /**
         * UI Interaction handling
         */
 //       virtual void HandleUIInteraction(FMemoryReader Ar);

		FIntPoint ConvertFromNormalizedScreenLocation(const FVector2D& ScreenLocation, bool bIncludeOffset = true);
		FWidgetPath FindRoutingMessageWidget(const FVector2D& Location) const;

		FGamepadKeyNames::Type ConvertAxisIndexToGamepadAxis(uint8 AnalogAxis);
		FGamepadKeyNames::Type ConvertButtonIndexToGamepadButton(uint8 ButtonIndex);
		FKey TranslateMouseButtonToKey(const EMouseButtons::Type Button);

		struct FCachedTouchEvent
		{
			FVector2D Location;
			float Force;
			int32 ControllerIndex;
		};

		// Keep a cache of the last touch events as we need to fire Touch Moved every frame while touch is down
		TMap<int32, FCachedTouchEvent> CachedTouchEvents;

		// Track which touch events we processed this frame so we can avoid re-processing them
		TSet<int32> TouchIndicesProcessedThisFrame;

		// Sends Touch Moved events for any touch index which is currently down but wasn't already updated this frame
		void BroadcastActiveTouchMoveEvents();

		void FindFocusedWidget();
		bool FocusIsInputField(TSharedPtr<SWidget> &outFocusWidget);
		bool FilterKey(const FKey& Key);


		TWeakPtr<SWindow> 			TargetWindow;
		TWeakPtr<SViewport> 		TargetViewport;
		TWeakPtr<FIntPoint> 		TargetScreenSize; // Manual size override used when we don't have a single window/viewport target
		uint8 						NumActiveTouches;
		bool 						bIsMouseActive;
		TQueue<FMessage> 			Messages;
		EZLCloudPluginInputType	InputType = EZLCloudPluginInputType::RouteToWindow;
		FVector2D					LastTouchLocation = FVector2D(EForceInit::ForceInitToZero);
		TMap<uint8, TFunction<void(TArray<FString>&)>> DispatchTable;

		/** Reference to the message handler which events should be passed to. */
		TSharedPtr<FGenericApplicationMessageHandler> MessageHandler;

		/** For convenience we keep a reference to the ZLCloudStream plugin. */
		IZLCloudPluginModule* ZLCloudPluginModule;

		/** For convenience, we keep a reference to the application wrapper owned by the input channel */
		TSharedPtr<FZLCloudPluginApplicationWrapper> ZLCloudPluginApplicationWrapper;

		/**
		 * Is the application faking touch events by dragging the mouse along
		 * the canvas? If so then we must put the browser canvas in a special
		 * state to replicate the behavior of the application.
		 */
		bool bFakingTouchEvents;

		/**
		 * Touch only. Location of the focused UI widget. If no UI widget is focused
		 * then this has the UnfocusedPos value.
		 */
		FVector2D FocusedPos;

		/**
		 * Touch only. A special position which indicates that no UI widget is
		 * focused.
		 */
		const FVector2D UnfocusedPos;

		/*
		 * Padding for string parsing when handling messages.
		 * 1 character for the actual message and then
		 * 2 characters for the length which are skipped
		 */
		const size_t MessageHeaderOffset = 1;
	
	private:
		float uint16_MAX = (float) UINT16_MAX;
		float int16_MAX = (float) SHRT_MAX;
	};
} // namespace ZLCloudPlugin
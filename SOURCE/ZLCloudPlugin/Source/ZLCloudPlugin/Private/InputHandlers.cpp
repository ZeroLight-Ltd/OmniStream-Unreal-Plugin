// Copyright ZeroLight ltd. All Rights Reserved.

#include "InputHandlers.h"
#include "IZLCloudPluginModule.h"
#include "Framework/Application/SlateApplication.h"
#include "CloudStream2.h"

namespace ZLCloudPlugin
{
        FInputHandlers::FInputHandlers(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
        {
            // This is imperative for editor streaming as when a modal is open or we've hit a BP breakpoint, the engine tick loop will not run, so instead we rely on this delegate to tick for us
		    FSlateApplication::Get().OnPreTick().AddRaw(this, &FInputHandlers::Tick);
        }
    
		void FInputHandlers::Tick(float DeltaTime)
        {
            if(CloudStream2::IsReady() && CloudStream2::GetInputHandler())
                CloudStream2::GetInputHandler()->Tick(DeltaTime);
        }

		void FInputHandlers::SendControllerEvents()
        {
            if (CloudStream2::IsReady() && CloudStream2::GetInputHandler())
                CloudStream2::GetInputHandler()->SendControllerEvents();
        }

		void FInputHandlers::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
        {
            if (CloudStream2::IsReady() && CloudStream2::GetInputHandler())
                CloudStream2::GetInputHandler()->SetMessageHandler(InMessageHandler);
        }

		bool FInputHandlers::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar) 
        {
            if (CloudStream2::IsReady() && CloudStream2::GetInputHandler())
            {
                return CloudStream2::GetInputHandler()->Exec(InWorld, Cmd, Ar);
            }
            else
            {
                return false;
            }
        }

		void FInputHandlers::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) 
        {
            if (CloudStream2::IsReady() && CloudStream2::GetInputHandler())
                CloudStream2::GetInputHandler()->SetChannelValue(ControllerId, ChannelType, Value);
        }

		void FInputHandlers::SetChannelValues(int32 ControllerId, const FForceFeedbackValues& Values)
        {
            if (CloudStream2::IsReady() && CloudStream2::GetInputHandler())
                CloudStream2::GetInputHandler()->SetChannelValues(ControllerId, Values);
        }

} // namespace ZLCloudPlugin

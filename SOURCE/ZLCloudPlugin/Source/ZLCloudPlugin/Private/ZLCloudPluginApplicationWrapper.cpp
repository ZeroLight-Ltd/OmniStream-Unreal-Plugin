// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginApplicationWrapper.h"
#include "ZLCloudPluginInputComponent.h"
#include "EditorZLCloudPluginSettings.h"

namespace ZLCloudPlugin
{
    FZLCloudPluginApplicationWrapper::FZLCloudPluginApplicationWrapper(TSharedPtr<GenericApplication> InWrappedApplication)
    	: GenericApplication(MakeShareable(new FCursor()))
		, WrappedApplication(InWrappedApplication)
	{
		// Whether we want to always consider the mouse as attached. This allow
		// us to run ZLCloudStream on a machine which has no physical mouse
		// and just let the browser supply mouse positions.
		const UZLCloudPluginSettings* Settings = GetMutableDefault<UZLCloudPluginSettings>();
		check(Settings);
		bMouseAlwaysAttached = Settings->bMouseAlwaysAttached;
	}

    void FZLCloudPluginApplicationWrapper::SetTargetWindow(TWeakPtr<SWindow> InTargetWindow)
    {
        TargetWindow = InTargetWindow;
    }

    TSharedPtr<FGenericWindow> FZLCloudPluginApplicationWrapper::GetWindowUnderCursor()
    { 
        TSharedPtr<SWindow> Window = TargetWindow.Pin();
		if (Window.IsValid())
	    {
            FVector2D CursorPosition = Cursor->GetPosition();
            FGeometry WindowGeometry = Window->GetWindowGeometryInScreen();

            FVector2D WindowOffset = WindowGeometry.GetAbsolutePosition();
            FVector2D WindowSize = WindowGeometry.GetAbsoluteSize();

            FBox2D WindowRect(WindowOffset, WindowSize); 
            if(WindowRect.IsInside(CursorPosition))
            {
                return Window->GetNativeWindow();
            }
		}

		return WrappedApplication->GetWindowUnderCursor();
    }
}
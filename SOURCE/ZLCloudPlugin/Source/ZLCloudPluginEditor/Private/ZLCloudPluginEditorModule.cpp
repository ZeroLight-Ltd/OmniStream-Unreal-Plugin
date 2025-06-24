// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLCloudPluginEditorModule.h"
#include "EdGraphUtilities.h"

void FZLCloudPluginEditorModule::StartupModule()
{
	FEdGraphUtilities::RegisterVisualPinFactory(MakeShareable(new FSchemaKeyPinFactory()));
}

void FZLCloudPluginEditorModule::ShutdownModule()
{

}

IMPLEMENT_MODULE(FZLCloudPluginEditorModule, ZLCloudPluginEditor)
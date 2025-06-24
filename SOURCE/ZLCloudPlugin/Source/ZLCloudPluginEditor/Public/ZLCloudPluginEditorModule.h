// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "ZLK2Nodes.h"

class FZLCloudPluginEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TSharedPtr<FSchemaKeyPinFactory> CustomPinFactory;
};
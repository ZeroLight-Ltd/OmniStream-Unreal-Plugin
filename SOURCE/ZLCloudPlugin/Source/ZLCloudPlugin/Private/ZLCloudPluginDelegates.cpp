// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginDelegates.h"

UZLCloudPluginDelegates* UZLCloudPluginDelegates::Singleton = nullptr;

UZLCloudPluginDelegates* UZLCloudPluginDelegates::CreateInstance()
{
	if (Singleton == nullptr)
	{
		Singleton = NewObject<UZLCloudPluginDelegates>();
		Singleton->AddToRoot();
		return Singleton;
	}
	return Singleton;
}

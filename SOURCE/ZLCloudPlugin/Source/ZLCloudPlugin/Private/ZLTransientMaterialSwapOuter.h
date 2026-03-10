// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ZLTransientMaterialSwapOuter.generated.h"

/**
 * Minimal concrete UObject used as Outer for transparent-screenshot MIDs
 * so they can be GC'd when the job is reset (UObject is abstract in some builds).
 */
UCLASS(MinimalAPI, NotBlueprintable, Transient)
class UZLTransientMaterialSwapOuter : public UObject
{
	GENERATED_BODY()
};

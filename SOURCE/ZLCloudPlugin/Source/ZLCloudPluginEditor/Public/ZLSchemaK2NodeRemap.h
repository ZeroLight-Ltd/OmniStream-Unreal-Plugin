// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UK2Node;

/** When a ZL state K2 node's schema asset is missing (e.g. after copying a Blueprint to another project), try to bind an in-project UStateKeyInfoAsset that defines all keys used on the node. */
ZLCLOUDPLUGINEDITOR_API void ZL_TryRemapStateSchemaOnZLK2Node(UK2Node* Node);

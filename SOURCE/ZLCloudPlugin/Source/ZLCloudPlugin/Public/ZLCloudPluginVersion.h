// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "Runtime/Launch/Resources/Version.h"

#define UNREAL_5_8_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 8)
#define UNREAL_5_7_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 7)
#define UNREAL_5_6_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6)
#define UNREAL_5_5_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 5)
#define UNREAL_5_4_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4)
#define UNREAL_5_3_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3)
#define UNREAL_5_2_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2)
#define UNREAL_5_1_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1)
#define UNREAL_5_0_OR_NEWER (ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 0)


#if UNREAL_5_7_OR_NEWER
#define ZL_TRANSIENT_PACKAGE_OUTER GetTransientPackageAsObject()
#else
#define ZL_TRANSIENT_PACKAGE_OUTER ((UObject*)GetTransientPackage())
#endif

// UE 5.1/5.2 UAT deletes injected game Target.cs when the project is not hybrid; 5.3+ uses NativeProjects hybrid handling.
#define ZL_USE_LEGACY_BP_PACKAGING (!UNREAL_5_3_OR_NEWER)

// UE 5.8 FJsonObject::Values keys are UE::FSharedString; older engines use FString.
#if UNREAL_5_8_OR_NEWER
#define ZL_JSON_KEY_TO_FSTRING(Key) FString((Key).ToView())
#else
#define ZL_JSON_KEY_TO_FSTRING(Key) (Key)
#endif

// 5.8: fenced CapturerRHI. One intermediate texture, registered once via SetTexture.
// DispatchToRHIThread, then OnFrameUE only after that copy's fence has signaled.
// The capture that sees the fence submits it and starts the next copy immediately.
// A newer frame that arrives while the copy is in flight is kept and sent next.
// The render thread does not wait, and there is no per-frame FlushRHIThread.
// On by default for UE 5.8.
#ifndef ZL_SAFE_CUDA_FRAME_SUBMIT
#define ZL_SAFE_CUDA_FRAME_SUBMIT UNREAL_5_8_OR_NEWER
#endif


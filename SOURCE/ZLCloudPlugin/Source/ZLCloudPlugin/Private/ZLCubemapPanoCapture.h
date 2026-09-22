// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class UTextureRenderTarget2D;

namespace ZLCloudPlugin
{
	/** Holds GPU face RTs for cubemap pano capture and runs ZLCubeToEquirect composite. */
	class FZLCubemapGpuStitcher
	{
	public:
		bool Begin(int32 InEquirectWidth, int32 InFaceSize, float InOverscan);

		// RigRotation is the rig-relative orientation that actually produced this face's
		// captured image (camera orientation at the moment the screenshot was issued,
		// expressed relative to the rig/start rotation). This is the source of truth for
		// the composite basis — it must match the pixels, not a pre-baked face table.
		// Takes ownership of Pixels (move preferred) so the render-thread upload can keep the buffer.
		void UploadFace(int32 FaceIndex, TArray<FColor> Pixels, int32 SizeX, int32 SizeY, const FQuat& RigRotation);

		bool AreAllFacesUploaded() const { return FacesUploaded >= 6; }

		bool CompositeToEquirect(TArray<FColor>& OutColors, int32& OutWidth, int32& OutHeight, double& OutCompositeSeconds);

		void Cleanup();

		int32 EquirectWidth = 0;
		int32 FaceSize = 0;
		float Overscan = 0.f;
		FQuat FaceRotations[6];
		int32 FacesUploaded = 0;

		TStrongObjectPtr<UTextureRenderTarget2D> FaceTargets[6];
		TStrongObjectPtr<UTextureRenderTarget2D> EquirectTarget;
	};
}

// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLCubemapPanoCapture.h"
#include "ZLCloudPluginPrivate.h"
#include "ZLCloudPluginVersion.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "TextureResource.h"
#include "ZLCloudPluginShaders.h"

namespace ZLCloudPlugin
{
	namespace
	{
		static UTextureRenderTarget2D* CreateFaceRenderTarget(int32 SizeX, int32 SizeY)
		{
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(ZL_TRANSIENT_PACKAGE_OUTER);
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->ClearColor = FLinearColor::Black;
			RT->bAutoGenerateMips = false;
			RT->bGPUSharedFlag = false;
			RT->TargetGamma = 1.0f;
			RT->InitAutoFormat(SizeX, SizeY);
			RT->UpdateResourceImmediate(true);
			return RT;
		}

		static UTextureRenderTarget2D* CreateEquirectRenderTarget(int32 SizeX, int32 SizeY)
		{
			UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>(ZL_TRANSIENT_PACKAGE_OUTER);
			// Display-space 8-bit out for direct FColor readback.
			RT->RenderTargetFormat = RTF_RGBA8;
			RT->ClearColor = FLinearColor::Black;
			RT->bAutoGenerateMips = false;
			RT->bGPUSharedFlag = false;
			RT->TargetGamma = 1.0f;
			RT->InitAutoFormat(SizeX, SizeY);
			RT->UpdateResourceImmediate(true);
			return RT;
		}
	}

	bool FZLCubemapGpuStitcher::Begin(int32 InEquirectWidth, int32 InFaceSize, float InOverscan)
	{
		if (InEquirectWidth <= 0 || InFaceSize <= 0)
		{
			return false;
		}

		EquirectWidth = InEquirectWidth;
		FaceSize = InFaceSize;
		Overscan = FMath::Clamp(InOverscan, 0.f, 1.f);
		FacesUploaded = 0;

		for (int32 i = 0; i < 6; ++i)
		{
			FaceRotations[i] = FQuat::Identity;
			FaceTargets[i].Reset(CreateFaceRenderTarget(FaceSize, FaceSize));
		}
		EquirectTarget.Reset(CreateEquirectRenderTarget(EquirectWidth, EquirectWidth / 2));

		UE_LOG(LogZLCloudPlugin, Verbose,
			TEXT("[CubemapGpu] Begin stitcher: equirect=%dx%d face=%d overscan=%.2f"),
			EquirectWidth, EquirectWidth / 2, FaceSize, Overscan);
		return true;
	}

	void FZLCubemapGpuStitcher::UploadFace(int32 FaceIndex, TArray<FColor> Pixels, int32 SizeX, int32 SizeY, const FQuat& RigRotation)
	{
		if (FaceIndex < 0 || FaceIndex >= 6 || !FaceTargets[FaceIndex].IsValid())
		{
			return;
		}

		if (SizeX != FaceSize || SizeY != FaceSize || Pixels.Num() < FaceSize * FaceSize)
		{
			UE_LOG(LogZLCloudPlugin, Warning,
				TEXT("[CubemapGpu] Face %d size mismatch (got %dx%d need %d) — skipping upload"),
				FaceIndex, SizeX, SizeY, FaceSize);
			return;
		}

		// Record the orientation that actually produced these pixels so the composite
		// basis matches the image (fixes flipped/upside-down faces).
		FaceRotations[FaceIndex] = RigRotation.GetNormalized();

		FTextureRenderTargetResource* DstRes = FaceTargets[FaceIndex]->GameThread_GetRenderTargetResource();
		const int32 W = FaceSize;
		const int32 H = FaceSize;
		// Do not flush per face — uploads stay ordered on the render thread and CompositeToEquirect
		// / ReadPixels flush once before readback.
		ENQUEUE_RENDER_COMMAND(ZLCubemapUploadFace)(
			[DstRes, PixelData = MoveTemp(Pixels), W, H](FRHICommandListImmediate& RHICmdList)
			{
				if (DstRes == nullptr || PixelData.Num() < W * H)
				{
					return;
				}

				FRHITexture* Texture = DstRes->GetRenderTargetTexture();
				if (Texture == nullptr)
				{
					return;
				}

				FUpdateTextureRegion2D Region(0, 0, 0, 0, W, H);
#if UNREAL_5_8_OR_NEWER
				RHICmdList.UpdateTexture2D(Texture, 0, Region, W * sizeof(FColor), reinterpret_cast<const uint8*>(PixelData.GetData()));
#else
				RHIUpdateTexture2D(Texture, 0, Region, W * sizeof(FColor), reinterpret_cast<const uint8*>(PixelData.GetData()));
#endif
			});

		FacesUploaded = FMath::Max(FacesUploaded, FaceIndex + 1);
	}

	bool FZLCubemapGpuStitcher::CompositeToEquirect(
		TArray<FColor>& OutColors, int32& OutWidth, int32& OutHeight, double& OutCompositeSeconds)
	{
		if (!EquirectTarget.IsValid() || FacesUploaded < 6)
		{
			return false;
		}

		const double CompositeStartSeconds = FPlatformTime::Seconds();

		FTextureRenderTargetResource* OutRes = EquirectTarget->GameThread_GetRenderTargetResource();
		FTextureRenderTargetResource* FaceRes[6];
		for (int32 i = 0; i < 6; ++i)
		{
			FaceRes[i] = FaceTargets[i].IsValid() ? FaceTargets[i]->GameThread_GetRenderTargetResource() : nullptr;
			if (FaceRes[i] == nullptr || OutRes == nullptr)
			{
				return false;
			}
		}

		FVector4f FaceBasis[18];
		for (int32 f = 0; f < 6; ++f)
		{
			const FQuat Q = FaceRotations[f];
			const FVector Fwd = Q.RotateVector(FVector::ForwardVector);
			const FVector Right = Q.RotateVector(FVector::RightVector);
			const FVector Up = Q.RotateVector(FVector::UpVector);
			FaceBasis[f * 3 + 0] = FVector4f((float)Fwd.X, (float)Fwd.Y, (float)Fwd.Z, 0.f);
			FaceBasis[f * 3 + 1] = FVector4f((float)Right.X, (float)Right.Y, (float)Right.Z, 0.f);
			FaceBasis[f * 3 + 2] = FVector4f((float)Up.X, (float)Up.Y, (float)Up.Z, 0.f);
		}

		const float OverscanTan = 1.0f + Overscan;
		const int32 OutW = EquirectWidth;
		const int32 OutH = EquirectWidth / 2;

		FZLCubeToEquirectComposite::Enqueue(OutRes, FaceRes, FaceBasis, OverscanTan, OutW, OutH);

		// Equirect RT is RGBA8 display-space (faces uploaded as FColor, shader resamples without
		// linearize/sRGB). ReadPixels copies 8-bit directly; SetLinearToGamma(false) matches the
		// previous ToFColor(false) path and avoids a wash-out double-gamma. ReadPixels flushes.
		FReadSurfaceDataFlags ReadFlags(RCM_MinMax);
		ReadFlags.SetLinearToGamma(false);
		if (!OutRes->ReadPixels(OutColors, ReadFlags) || OutColors.Num() < OutW * OutH)
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("[CubemapGpu] ReadPixels failed"));
			return false;
		}

		OutWidth = OutW;
		OutHeight = OutH;
		OutCompositeSeconds = FPlatformTime::Seconds() - CompositeStartSeconds;
		UE_LOG(LogZLCloudPlugin, Verbose, TEXT("[CubemapGpu] Composite complete: %dx%d (%.2fs)"), OutW, OutH, OutCompositeSeconds);
		return true;
	}

	void FZLCubemapGpuStitcher::Cleanup()
	{
		for (int32 i = 0; i < 6; ++i)
		{
			FaceTargets[i].Reset();
		}
		EquirectTarget.Reset();
		FacesUploaded = 0;
	}
}

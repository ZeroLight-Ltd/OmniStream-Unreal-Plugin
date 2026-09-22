// Copyright ZeroLight ltd. All Rights Reserved.
#include "ZLCloudPluginShaders.h"

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "PixelShaderUtils.h"
#include "CommonRenderResources.h"
#include "TextureResource.h"
#include "RHIStaticStates.h"
#include "RenderingThread.h"

class FZLCubeToEquirectPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FZLCubeToEquirectPS);
	SHADER_USE_PARAMETER_STRUCT(FZLCubeToEquirectPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_ARRAY(FVector4f, FaceBasis, [18])
		SHADER_PARAMETER(float, OverscanTan)
		SHADER_PARAMETER(FVector2f, OutputExtent)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face1)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face2)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face3)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face4)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Face5)
		SHADER_PARAMETER_SAMPLER(SamplerState, FaceSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FZLCubeToEquirectPS, "/Plugin/ZLCloudPlugin/Private/ZLCubeToEquirect.usf", "MainPS", SF_Pixel);

void FZLCubeToEquirectComposite::Enqueue(
	FTextureRenderTargetResource* OutRes,
	FTextureRenderTargetResource* const* FaceRes,
	const FVector4f* FaceBasis,
	float OverscanTan,
	int32 OutW,
	int32 OutH)
{
	if (OutRes == nullptr || FaceRes == nullptr || FaceBasis == nullptr)
	{
		return;
	}

	FTextureRenderTargetResource* FaceResCopy[6];
	for (int32 i = 0; i < 6; ++i)
	{
		FaceResCopy[i] = FaceRes[i];
	}
	FVector4f FaceBasisCopy[18];
	for (int32 i = 0; i < 18; ++i)
	{
		FaceBasisCopy[i] = FaceBasis[i];
	}

	ENQUEUE_RENDER_COMMAND(ZLCubeToEquirectComposite)(
		[OutRes, FaceResCopy, FaceBasisCopy, OverscanTan, OutW, OutH](FRHICommandListImmediate& RHICmdList)
		{
			FRDGBuilder GraphBuilder(RHICmdList);

			FRDGTextureRef OutTex = GraphBuilder.RegisterExternalTexture(
				CreateRenderTarget(OutRes->GetRenderTargetTexture(), TEXT("ZLEquirectOut")));

			FRDGTextureRef FaceTex[6];
			for (int32 i = 0; i < 6; ++i)
			{
				FaceTex[i] = GraphBuilder.RegisterExternalTexture(
					CreateRenderTarget(FaceResCopy[i]->GetRenderTargetTexture(), TEXT("ZLPanoFace")));
			}

			FZLCubeToEquirectPS::FParameters* PassParams = GraphBuilder.AllocParameters<FZLCubeToEquirectPS::FParameters>();
			for (int32 i = 0; i < 18; ++i)
			{
				PassParams->FaceBasis[i] = FaceBasisCopy[i];
			}
			PassParams->OverscanTan = OverscanTan;
			PassParams->OutputExtent = FVector2f((float)OutW, (float)OutH);
			PassParams->Face0 = FaceTex[0];
			PassParams->Face1 = FaceTex[1];
			PassParams->Face2 = FaceTex[2];
			PassParams->Face3 = FaceTex[3];
			PassParams->Face4 = FaceTex[4];
			PassParams->Face5 = FaceTex[5];
			PassParams->FaceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			PassParams->RenderTargets[0] = FRenderTargetBinding(OutTex, ERenderTargetLoadAction::ENoAction);

			const FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FZLCubeToEquirectPS> PixelShader(ShaderMap);

			FPixelShaderUtils::AddFullscreenPass(
				GraphBuilder,
				ShaderMap,
				RDG_EVENT_NAME("ZLCubeToEquirect"),
				PixelShader,
				PassParams,
				FIntRect(0, 0, OutW, OutH));

			GraphBuilder.Execute();
		});
}

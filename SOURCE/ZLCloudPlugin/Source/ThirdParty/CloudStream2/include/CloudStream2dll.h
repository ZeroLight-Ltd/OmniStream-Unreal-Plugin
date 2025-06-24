// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#if defined _WIN32 || defined _WIN64
#define CLOUDSTREAM2DLL_IMPORT __declspec(dllimport)
#elif defined __linux__
#define CLOUDSTREAM2DLL_IMPORT __attribute__((visibility("default")))
#else
#define CLOUDSTREAM2DLL_IMPORT
#endif

namespace CloudStream2DLL
{
	enum FrameTypeRequirements
	{
		NO_FRAME = 0,
		YUV_FRAME = 1 << 0,
		RGBA_FRAME = 1 << 1,
		BOTH = YUV_FRAME | RGBA_FRAME
	};

	enum YUVColorSpace
	{
		BT_709Studio,
		BT_601Full,
		BT_601Studio
	};

	enum EncoderType
	{
		UNKNOWN,
		MJPEG,
		VP9,
		WEBRTC
	};

	struct EncoderRequirements
	{
		uint32_t Width;
		uint32_t Height;
		uint32_t FrameType;
		uint32_t Color;

		bool UseStreamPausing;
		bool UseDynamicResolution;
		uint32_t DynamicResolutionWidth;
		uint32_t DynamicResolutionHeight;

		void Clear()
		{
			Width = 0;
			Height = 0;
			FrameType = FrameTypeRequirements::NO_FRAME;
			Color = YUVColorSpace::BT_709Studio;
			UseStreamPausing = false;
			UseDynamicResolution = false;
			DynamicResolutionWidth = 0;
			DynamicResolutionHeight = 0;
		}
	};

	typedef void(*ClientsDisconnectedCallback)();
	typedef void(*ForceImageChangingCallback)(float);
	typedef void(*RichDataStreamConfigCallback)(int, bool);
	typedef void(*MousePositionCallback)(float, float, float, int);
	typedef void(*EncoderRequirementsCallback)(EncoderRequirements);
	typedef void(*FrameMetadataConfigCallback)(int, int, int, int, int);
	typedef void(*PluginMessageCallback)(const char*);
	typedef void(*SendFramesCallback)(int);

	typedef void(*UEFunctionPtr)(bool, const TCHAR*);

	extern "C"
	{
		CLOUDSTREAM2DLL_IMPORT void CloudStream2DebugPopup();
		CLOUDSTREAM2DLL_IMPORT void UnrealPluginLoad(void* devicePtr, bool dx12);
		CLOUDSTREAM2DLL_IMPORT void SetUEDebugFunction(UEFunctionPtr fp);

		CLOUDSTREAM2DLL_IMPORT void GetPluginVersion(int& major, int& minor, int& rev, int& build);
		CLOUDSTREAM2DLL_IMPORT void SetAppVersionInfo(const char* versionInfo);

		CLOUDSTREAM2DLL_IMPORT void OnFrameUE(int isDynamic, void* deviceCommandQueue);
		CLOUDSTREAM2DLL_IMPORT void Initialize(EncoderRequirementsCallback encoderCallback, MousePositionCallback mouseCallback, ClientsDisconnectedCallback clientsDisconnectedCallback, RichDataStreamConfigCallback richDataStreamConfigCallback, FrameMetadataConfigCallback frameMetaConfigCallback, ForceImageChangingCallback forceImageChangingCallback, PluginMessageCallback pluginMessageCallback, SendFramesCallback sendFramesCallback);

		CLOUDSTREAM2DLL_IMPORT void SetAverageFrameTime(float frameAverageTime);
		CLOUDSTREAM2DLL_IMPORT void GetMjpegQualityLevels(int& JPEGQualityMin, int& JPEGQualityFastMove, int& JPEGQualityMax);
		CLOUDSTREAM2DLL_IMPORT float GetMjpegQuality();
		CLOUDSTREAM2DLL_IMPORT void SetMjpegQuality(int JPEGQuality);

		CLOUDSTREAM2DLL_IMPORT void Destroy();

		CLOUDSTREAM2DLL_IMPORT void SetTexture(void* intermidiateTexture);

		CLOUDSTREAM2DLL_IMPORT void OnRichDataStream(int dataSourceID, char* data, long timestamp);
		CLOUDSTREAM2DLL_IMPORT void SendCommand(const char* id, const char* data);

		CLOUDSTREAM2DLL_IMPORT int GetNumConnections();

		CLOUDSTREAM2DLL_IMPORT void SetSettingsJSON(const char* json, bool runningFromEditor);
		CLOUDSTREAM2DLL_IMPORT void SetFPS(int fps);
		CLOUDSTREAM2DLL_IMPORT void DeletePeerConnection();
		CLOUDSTREAM2DLL_IMPORT void OnAudioData(float* audioData, int sampleRate, int channels, int frames);
		CLOUDSTREAM2DLL_IMPORT bool ShouldSendAudio();

		CLOUDSTREAM2DLL_IMPORT float GetEncoderAverageTime();
		CLOUDSTREAM2DLL_IMPORT EncoderType GetEncoderType();
		CLOUDSTREAM2DLL_IMPORT unsigned int GetProcessedFramesCount();

		CLOUDSTREAM2DLL_IMPORT float GetPluginFPS();
		CLOUDSTREAM2DLL_IMPORT float GetPluginFPS_requested();

		CLOUDSTREAM2DLL_IMPORT unsigned int GetPluginBitrate();
		CLOUDSTREAM2DLL_IMPORT unsigned int GetPluginBitrate_requested();

		CLOUDSTREAM2DLL_IMPORT bool GetPluginIsKeyFrame();

		CLOUDSTREAM2DLL_IMPORT void SetDebugFunction(void* debugFunctionPtr);

		CLOUDSTREAM2DLL_IMPORT void TextureToPlugin(void* texture, int size, int equiWidth, int faceSize, int faceID, unsigned char* outBuff);
		CLOUDSTREAM2DLL_IMPORT bool IsPanoImageReady();
	}

}

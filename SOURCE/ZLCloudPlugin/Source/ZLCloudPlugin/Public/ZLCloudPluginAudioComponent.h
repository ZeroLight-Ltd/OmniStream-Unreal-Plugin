// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SynthComponent.h"
#include "IZLCloudPluginAudioConsumer.h"
#include "IZLCloudPluginAudioSink.h"
#include "Sound/SoundGenerator.h"
#include "ZLCloudPluginAudioComponent.generated.h"

/*
* An `ISoundGenerator` implementation to pump some audio from WebRTC into this synth component
*/

class ZLWebRTCSoundGenerator : public ISoundGenerator
{
public:
	ZLWebRTCSoundGenerator();

	// Called when a new buffer is required.
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

	// Returns the number of samples to render per callback
	virtual int32 GetDesiredNumSamplesToRenderPerCallback() const { return Params.NumFramesPerCallback * Params.NumChannels; }

	// Optional. Called on audio generator thread right when the generator begins generating.
	virtual void OnBeginGenerate() { bGeneratingAudio = true; };

	// Optional. Called on audio generator thread right when the generator ends generating.
	virtual void OnEndGenerate() { bGeneratingAudio = false; };

	// Optional. Can be overridden to end the sound when generating is finished.
	virtual bool IsFinished() const { return false; };

	void AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames);

	int32 GetSampleRate() { return Params.SampleRate; }
	int32 GetNumChannels() { return Params.NumChannels; }
	bool UpdateChannelsAndSampleRate(int InNumChannels, int InSampleRate);
	void EmptyBuffers();
	void SetParameters(const FSoundGeneratorInitParams& InitParams);

private:
	FSoundGeneratorInitParams Params;
	TArray<int16_t> Buffer;
	FCriticalSection CriticalSection;

public:
	FThreadSafeBool bGeneratingAudio = false;
	FThreadSafeBool bShouldGenerateAudio = false;
};

/**
 * Allows in-engine playback of incoming WebRTC audio from a particular ZLCloudStream player/peer using their mic in the browser.
 * Note: Each audio component associates itself with a particular ZLCloudStream player/peer (using the the ZLCloudStream player id).
 */
UCLASS(Blueprintable, ClassGroup = (ZLCloudPlugin), meta = (BlueprintSpawnableComponent))
class ZLCLOUDPLUGIN_API UZLCloudPluginAudioComponent : public USynthComponent, public IZLCloudPluginAudioConsumer
{
	GENERATED_BODY()

protected:
	UZLCloudPluginAudioComponent(const FObjectInitializer& ObjectInitializer);

	//~ Begin USynthComponent interface
	virtual void OnBeginGenerate() override;
	virtual void OnEndGenerate() override;
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
	//~ End USynthComponent interface

	//~ Begin UObject interface
	virtual void BeginDestroy() override;
	//~ End UObject interface

	//~ Begin UActorComponent interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent interface

public:
	/** 
	*   The ZLCloudStream player/peer whose audio we wish to listen to.
	*   If this is left blank this component will listen to the first non-listened to peer that connects after this component is ready.
	*   Note: that when the listened to peer disconnects this component is reset to blank and will once again listen to the next non-listened to peer that connects.
	*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZLCloudStream Audio Component")
	FString PlayerToHear;

	/**
	 *  If not already listening to a player/peer will try to attach for listening to the "PlayerToHear" each tick.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ZLCloudStream Audio Component")
	bool bAutoFindPeer;

private:
	IZLCloudPluginAudioSink* AudioSink;
	TSharedPtr<ZLWebRTCSoundGenerator, ESPMode::ThreadSafe> SoundGenerator;

public:
	// Listen to a specific player. If the player is not found this component will be silent.
	UFUNCTION(BlueprintCallable, Category = "ZLCloudStream Audio Component")
	bool ListenTo(FString PlayerToListenTo);

	// True if listening to a connected WebRTC peer through ZLCloudStream.
	UFUNCTION(BlueprintCallable, Category = "ZLCloudStream Audio Component")
	bool IsListeningToPlayer();

	bool WillListenToAnyPlayer();

	// Stops listening to any connected player/peer and resets internal state so component is ready to listen again.
	UFUNCTION(BlueprintCallable, Category = "ZLCloudStream Audio Component")
	void Reset();

	//~ Begin IZLCloudPluginAudioConsumer interface
	void ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames);
	void OnConsumerAdded();
	void OnConsumerRemoved();
	//~ End IZLCloudPluginAudioConsumer interface
};
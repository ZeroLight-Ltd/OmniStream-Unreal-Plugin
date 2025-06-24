// Copyright ZeroLight ltd. All Rights Reserved.

#include "ZLCloudPluginAudioComponent.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginPrivate.h"
#include "CoreMinimal.h"

/*
* Component that receives audio from a remote webrtc connection and outputs it into UE using a "synth component".
*/
UZLCloudPluginAudioComponent::UZLCloudPluginAudioComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, PlayerToHear(FZLCloudPluginPlayerId())
	, bAutoFindPeer(true)
	, AudioSink(nullptr)
	, SoundGenerator(MakeShared<ZLWebRTCSoundGenerator, ESPMode::ThreadSafe>())
{

	bool bZLCloudPluginLoaded = IZLCloudPluginModule::IsAvailable();

	// Only output this warning if we are actually running this component (not in commandlet).
	if (!bZLCloudPluginLoaded && !IsRunningCommandlet())
	{
		UE_LOG(LogZLCloudPlugin, Warning, TEXT("ZLCloudStream audio component will not tick because ZLCloudStream module is not loaded. This is expected on dedicated servers."));
	}

	NumChannels = 1; //2 channels seem to cause problems
	PreferredBufferLength = 512u;
	PrimaryComponentTick.bCanEverTick = bZLCloudPluginLoaded;
	SetComponentTickEnabled(bZLCloudPluginLoaded);
	bAutoActivate = true;
};

ISoundGeneratorPtr UZLCloudPluginAudioComponent::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	SoundGenerator->SetParameters(InParams);
	return SoundGenerator;
}

void UZLCloudPluginAudioComponent::OnBeginGenerate()
{
	SoundGenerator->bGeneratingAudio = true;
}

void UZLCloudPluginAudioComponent::OnEndGenerate()
{
	SoundGenerator->bGeneratingAudio = false;
}

void UZLCloudPluginAudioComponent::BeginDestroy()
{
	Super::BeginDestroy();
	Reset();
}

bool UZLCloudPluginAudioComponent::ListenTo(FString PlayerToListenTo)
{

	if (!IZLCloudPluginModule::IsAvailable())
	{
		UE_LOG(LogZLCloudPlugin, Verbose, TEXT("ZLCloudStream audio component could not listen to anything because ZLCloudStream module is not loaded. This is expected on dedicated servers."));
		return false;
	}

	IZLCloudPluginModule& ZLCloudPluginModule = IZLCloudPluginModule::Get();
	if (!ZLCloudPluginModule.IsReady())
	{
		return false;
	}

	PlayerToHear = PlayerToListenTo;

	IZLCloudPluginAudioSink* CandidateSink = WillListenToAnyPlayer() ? ZLCloudPluginModule.GetUnlistenedAudioSink() : ZLCloudPluginModule.GetPeerAudioSink(FZLCloudPluginPlayerId(PlayerToHear));

	if (CandidateSink == nullptr)
	{
		return false;
	}

	AudioSink = CandidateSink;
	AudioSink->AddAudioConsumer(this);

	return true;
}

void UZLCloudPluginAudioComponent::Reset()
{
	PlayerToHear = FString();
	SoundGenerator->bShouldGenerateAudio = false;
	if (AudioSink)
	{
		AudioSink->RemoveAudioConsumer(this);
	}
	AudioSink = nullptr;
	SoundGenerator->EmptyBuffers();
}

bool UZLCloudPluginAudioComponent::IsListeningToPlayer()
{
	return SoundGenerator->bShouldGenerateAudio;
}

bool UZLCloudPluginAudioComponent::WillListenToAnyPlayer()
{
	return PlayerToHear == FString();
}

void UZLCloudPluginAudioComponent::ConsumeRawPCM(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{
	if (SoundGenerator->GetSampleRate() != InSampleRate || SoundGenerator->GetNumChannels() != NChannels)
	{
		SoundGenerator->UpdateChannelsAndSampleRate(NChannels, InSampleRate);

		//this is the smallest buffer size we can set without triggering internal checks to fire
		PreferredBufferLength = FGenericPlatformMath::Max(512.0f, InSampleRate * NChannels / 100.0f);

		NumChannels = NChannels;
		Initialize(InSampleRate);
	}
	else
	{
		SoundGenerator->AddAudio(AudioData, InSampleRate, NChannels, NFrames);
	}
}

void UZLCloudPluginAudioComponent::OnConsumerAdded()
{
	SoundGenerator->bShouldGenerateAudio = true;
	Start();
}

void UZLCloudPluginAudioComponent::OnConsumerRemoved()
{
	Reset();
}

void UZLCloudPluginAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{

	// if auto connect turned off don't bother
	if (!bAutoFindPeer)
	{
		return;
	}

	// if listening to a peer don't auto connect
	if (IsListeningToPlayer())
	{
		return;
	}

	if (ListenTo(PlayerToHear))
	{
		UE_LOG(LogZLCloudPlugin, Log, TEXT("ZLCloudPlugin audio component found a WebRTC peer to listen to."));
	}
}

/*
* ---------------- ZLWebRTCSoundGenerator -------------------------
*/

ZLWebRTCSoundGenerator::ZLWebRTCSoundGenerator()
	: Params()
	, Buffer()
	, CriticalSection()
{
}

void ZLWebRTCSoundGenerator::SetParameters(const FSoundGeneratorInitParams& InitParams)
{
	Params = InitParams;
	UpdateChannelsAndSampleRate(Params.NumChannels, Params.SampleRate);
}

void ZLWebRTCSoundGenerator::EmptyBuffers()
{
	FScopeLock Lock(&CriticalSection);
	Buffer.Empty();
}

bool ZLWebRTCSoundGenerator::UpdateChannelsAndSampleRate(int InNumChannels, int InSampleRate)
{

	if (InNumChannels != Params.NumChannels || InSampleRate != Params.SampleRate)
	{

		// Critical Section - empty buffer because sample rate/num channels changed
		FScopeLock Lock(&CriticalSection);
		Buffer.Empty();

		Params.NumChannels = InNumChannels;
		Params.SampleRate = InSampleRate;

		return true;
	}

	return false;
}

void ZLWebRTCSoundGenerator::AddAudio(const int16_t* AudioData, int InSampleRate, size_t NChannels, size_t NFrames)
{
	if (!bGeneratingAudio)
	{
		return;
	}

	// Trigger a latent update for channels and sample rate, if required
	bool bSampleRateOrChannelMismatch = UpdateChannelsAndSampleRate(NChannels, InSampleRate);

	// If there was a mismatch then skip this incoming audio until this the samplerate/channels update is completed on the gamethread
	if (bSampleRateOrChannelMismatch)
	{
		return;
	}

	// Copy into our local TArray<int16_t> Buffer;
	int NSamples = NFrames * NChannels;

	// Critical Section
	{
		FScopeLock Lock(&CriticalSection);
		Buffer.Append(AudioData, NSamples);
		// checkf((uint32)Buffer.Num() < SampleRate,
		// 	TEXT("ZLCloudStream Audio Component internal buffer is getting too big, for some reason OnGenerateAudio is not consuming samples quickly enough."))
	}
}

// Called when a new buffer is required.
int32 ZLWebRTCSoundGenerator::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	// Not listening to peer, return zero'd buffer.
	if (!bShouldGenerateAudio || Buffer.Num() == 0)
	{
		return NumSamples;
	}

	// Critical section
	{
		FScopeLock Lock(&CriticalSection);

		int32 NumSamplesToCopy = FGenericPlatformMath::Min(NumSamples, Buffer.Num());

		// Copy from local buffer into OutAudio if we have enough samples
		for (int SampleIndex = 0; SampleIndex < NumSamplesToCopy; SampleIndex++)
		{
			// Convert from int16 to float audio
			*OutAudio = ((float)Buffer[SampleIndex]) / 32767.0f;
			OutAudio++;
		}

		// Remove front NumSamples from the local buffer
		Buffer.RemoveAt(0, NumSamplesToCopy, false);
		return NumSamplesToCopy;
	}
}

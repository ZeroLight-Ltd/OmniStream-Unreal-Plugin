// Copyright ZeroLight ltd. All Rights Reserved.
#include "LauncherComms.h"
#include "CoreMinimal.h"
#include "SocketSubsystem.h"
#include "ZLCloudPluginPrivate.h"
#include "MessageCallbacks.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/EngineVersion.h"


#define SERVERVECOMMSVERSION 6

LauncherComms::LauncherComms() :
	m_versionMatch(false),
	m_bWriteError(false),
	m_ReadThreadRunning(true)	
{
	
}

bool LauncherComms::InitComms()
{
	m_ServerVersion = -1;
	m_launcherMessages.Empty();

	MessageCallbacks::RegisterCallbacks(this);

	m_WriteSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("ZLWriteSocket"), false);
	if (!m_WriteSocket)
	{
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to create write socket"));
		return false;
	}

	FString ipAddress = TEXT("127.0.0.1");
	int writePort = 4785;
	m_SendBufferSize = 1 * 1024 * 1024; // 1mb

	FIPv4Address ip;
	FIPv4Address::Parse(ipAddress, ip);

	TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	bool bIsValid = false;
	internetAddr->SetIp(ip.Value);
	internetAddr->SetPort(writePort);

	m_WriteSocket->SetSendBufferSize(m_SendBufferSize, m_ActualSendBufferSize);

	bool connected = m_WriteSocket->Connect(*internetAddr);	

	if (connected)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Connected write socket to ZLServer"));
	}
	else
	{
		ESocketErrors LastErr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetLastErrorCode();
		UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to connect write socket, error code (%d) error (%s)"), LastErr, ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->GetSocketError(LastErr));

		return false;
	}

	int ReadPort = 4786;
	m_MessageReader = new MessageReader(ReadPort);

	m_thread = FRunnableThread::Create(this, TEXT("LauncherCommsMessageLoop"), 128 * 1024, TPri_Normal);
	m_ReadThreadRunning = true;

	SendLauncherMessage("VERSION", FString::FromInt(SERVERVECOMMSVERSION));

	return true;
}

void LauncherComms::Update()
{
	CheckLauncherMessages();

	if (m_ServerVersion == SERVERVECOMMSVERSION)
	{
		if (!m_versionMatch)
		{
			SendLauncherMessage("VERSIONMATCH");
			m_versionMatch = true;
			SendLauncherMessage("SYN", "");

			m_TimeSinceHeartbeat = 0.0f;
			m_TimeSinceRefreshRate = 999.0f;	//send ASAP

			SendLauncherMessage("VECONNECTED");

			FString gpuStr = FPlatformMisc::GetPrimaryGPUBrand();
			FString cpuStr = FPlatformMisc::GetCPUBrand();
			FString osStr = FPlatformMisc::GetOSVersion();
			FString unrealVerStr = FEngineVersion::Current().ToString();

			/*NvU32 DriverVersion = UINT32_MAX;

			if (NvStatus == NVAPI_OK)
			{
				NvAPI_ShortString BranchString("");
				if (NvAPI_SYS_GetDriverAndBranchVersion(&DriverVersion, BranchString) != NVAPI_OK)
				{
					UE_LOG(LogD3D12RHI, Warning, TEXT("Failed to query NVIDIA driver version"));
				}
			}*/

			TSharedPtr<FJsonObject> sentryTagData = MakeShareable(new FJsonObject);
			sentryTagData->SetStringField("GPU", gpuStr);
			sentryTagData->SetStringField("CPU", cpuStr);
			sentryTagData->SetStringField("Windows_Version", osStr);
			sentryTagData->SetStringField("Engine_Version", unrealVerStr);

			FString sentryTagDataStr;

			TSharedRef<TJsonWriter<>> writer = TJsonWriterFactory<>::Create(&sentryTagDataStr);

			FJsonSerializer::Serialize(sentryTagData.ToSharedRef(), writer);

			SendLauncherMessage("VEADDSENTRYTAGS", sentryTagDataStr);
		}
	}

	if (m_WriteSocket != nullptr && m_versionMatch)
	{
		float realtimeSeconds = FPlatformTime::Seconds();

		if (realtimeSeconds - m_TimeSinceHeartbeat >= 1.0f)
		{
			SendLauncherMessage("HEARTBEAT", "");
			m_TimeSinceHeartbeat = realtimeSeconds;
		}

		if (realtimeSeconds - m_TimeSinceRefreshRate >= 10.0f)
		{
			int fps = 30;
			SendLauncherMessage("REFRESHRATE:" + fps);
			m_TimeSinceRefreshRate = realtimeSeconds;
		}
	}

}

void LauncherComms::CheckLauncherMessages()
{
	if (m_WriteSocket != nullptr)
	{
		if (!m_launcherMessages.IsEmpty())
		{
			MessageWithData* msg;

			while (m_launcherMessages.Dequeue(msg))
			{		
				if (m_MessageCallbacks.Contains(msg->m_messageName) && ZLCloudPlugin::CloudStream2::IsMessageHandling())
				{
					LauncherCommsCallback callback = m_MessageCallbacks.FindRef(msg->m_messageName);
					if (callback != nullptr)
					{
						//Call the call back assigned to this command
						(*callback)(msg);

						//Has the call back added a reply to the message?
						if (msg->m_hasReply)
						{
							SendLauncherMessage(msg->m_replyMessageName, msg->m_replyMessageData);
						}
					}
				}
				else
				{
					UE_LOG(LogZLCloudPlugin, Display, TEXT("Unhandled Message: %s"), *msg->m_messageName);
				}
			}
		}
	}
}

void LauncherComms::SendLauncherMessage(FString message, FString arg1 /*= ""*/)
{
	if (m_WriteSocket != nullptr)
	{
		auto SendBuffer = [&](const TArray<uint8>& Buffer) -> bool
		{
			uint32 BufferSize = Buffer.Num();
			const uint8* Data = Buffer.GetData();

			uint32 TotalByteSent = 0;
			bool bSendSucceed = true;
			while (bSendSucceed && TotalByteSent < BufferSize)
			{
				int32 BytesSent = 0;
				bSendSucceed &= m_WriteSocket->Send(Data + TotalByteSent, BufferSize - TotalByteSent, BytesSent);
				TotalByteSent += (uint32)BytesSent;
			}
			return bSendSucceed && TotalByteSent == BufferSize;
		};

		if (!arg1.IsEmpty())
		{
			message = message + ":" + arg1;
		}

		TArray<uint8> BodyBuffer;
		auto ansiString = StringCast<ANSICHAR>(*message);
		const char* charMessage = ansiString.Get();
		int32 numBytes = strlen(charMessage);
		BodyBuffer.Append((uint8*)charMessage, numBytes);

		// The first 32 bits of this message are the number of bytes of the message.
		TArray<uint8> HeaderBuffer;
		int32 bigEndianNumBytes = _byteswap_ulong(numBytes);//Flip Endian for python server
		HeaderBuffer.Append((uint8 *)&bigEndianNumBytes, sizeof(int32));

		if (!SendBuffer(HeaderBuffer))
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("can't write header on socket"));
			m_bWriteError = true;
			return;
		}

		// Send the content
		if (!SendBuffer(BodyBuffer))
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("can't write content on socket"));
			m_bWriteError = true;
			return;
		}
	}
	
}

void LauncherComms::SendLauncherMessageBinary(FString message, TArray64<uint8> packet)
{
	if (m_WriteSocket != nullptr)
	{
		auto SendBuffer = [&](const TArray<uint8>& Buffer) -> bool
		{
			uint32 BufferSize = Buffer.Num();
			const uint8* Data = Buffer.GetData();

			uint32 TotalByteSent = 0;
			bool bSendSucceed = true;
			while (bSendSucceed && TotalByteSent < BufferSize)
			{
				int32 BytesSent = 0;
				bSendSucceed &= m_WriteSocket->Send(Data + TotalByteSent, BufferSize - TotalByteSent, BytesSent);
				TotalByteSent += (uint32)BytesSent;
			}
			return bSendSucceed && TotalByteSent == BufferSize;
		};

		message = message + ":";

		TArray<uint8> BodyBuffer;
		auto ansiString = StringCast<ANSICHAR>(*message);
		const char* charMessage = ansiString.Get();
		int32 numBytes = strlen(charMessage);
		BodyBuffer.Append((uint8*)charMessage, numBytes);
		BodyBuffer.Append(packet);

		// The first 32 bits of this message are the number of bytes of the message.
		TArray<uint8> HeaderBuffer;
		int32 bigEndianNumBytes = _byteswap_ulong(numBytes + packet.Num());//Flip Endian for python server
		HeaderBuffer.Append((uint8*)&bigEndianNumBytes, sizeof(int32));

		if (!SendBuffer(HeaderBuffer))
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("can't write header on socket"));
			m_bWriteError = true;
			return;
		}

		// Send the content
		if (!SendBuffer(BodyBuffer))
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("can't write content on socket"));
			m_bWriteError = true;
			return;
		}
	}

}

/* FRunnable interface
*****************************************************************************/

void LauncherComms::Exit()
{
	// do nothing
}


bool LauncherComms::Init()
{
	return true;
}


uint32 LauncherComms::Run()
{
	if (m_MessageReader && m_MessageReader->Start())
	{
		while (m_ReadThreadRunning && !m_MessageReader->Error())
		{
			MessageWithData* messageWithData = m_MessageReader->GetMessage();
			if (messageWithData != nullptr)
			{
				m_launcherMessages.Enqueue(messageWithData);
			}
			else
			{
				//////////////////////////////////////
				//see if there's anything else to read
				//and grab it all before we sleep
				int numReceived = 999;
				int totalReceived = 0;
				while ((numReceived > 0) && m_ReadThreadRunning)
				{
					numReceived = m_MessageReader->FillBuffer();
					totalReceived += numReceived;
				}
				//////////////////////////////////////

				if ((totalReceived == 0) && m_ReadThreadRunning)
				{
					//nothing to do, sleep for a bit
					FPlatformProcess::Sleep(0.001f);
				}
			}
		}

		UE_LOG(LogZLCloudPlugin, Display, TEXT("Begin close message reader socket"));
		m_MessageReader->Finish();
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Message reader socket closed"));
	}

	return 0;
}


void LauncherComms::Stop()
{
	m_ReadThreadRunning = false;
}

void LauncherComms::Continue()
{
	if (m_WriteSocket != nullptr && m_versionMatch)
	{
		m_ReadThreadRunning = true;
	}
}

/* 
*****************************************************************************/

void LauncherComms::Shutdown()
{
	if (m_WriteSocket)
	{
		m_WriteSocket->Close();
		m_WriteSocket = nullptr;
	}

	if (m_thread != nullptr)
	{
		Stop();
		m_thread->Kill(true);
		delete m_thread;
		m_thread = nullptr;
	}

	if (m_MessageReader)
	{
		m_MessageReader->Quit();
		delete m_MessageReader;
		m_MessageReader = nullptr;
	}

	m_versionMatch = false;
}


/*
*****************************************************************************/

void LauncherComms::RegisterMessageCallback(FString name, LauncherCommsCallback callback)
{
	m_MessageCallbacks.Add(name, callback);
}

// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CloudStream2dll.h"
#include "CloudStream2.h"
#include "CoreFwd.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "MessageReader.h"
#include "HAL/RunnableThread.h"

typedef void(*LauncherCommsCallback)(MessageWithData*);

class LauncherComms : FRunnable
{
	public:	
		LauncherComms();

		bool InitComms();
		bool IsReadRunning() { return m_ReadThreadRunning; };
		void Shutdown();
		void Continue();

		void Update();
		void SendLauncherMessage(FString message, FString arg1 = "");
		void SendLauncherMessageBinary(FString message, TArray64<uint8> arg1);

		void RegisterMessageCallback(FString name, LauncherCommsCallback callback);

		void SetZLServerVersion(int version) { m_ServerVersion = version; }

	public:

		//~ FRunnable interface

		virtual void Exit() override;
		virtual bool Init() override;
		virtual uint32 Run() override;
		virtual void Stop() override;

	private:
		int m_ServerVersion;
		bool m_versionMatch;
		bool m_bWriteError;

		float m_TimeSinceHeartbeat;
		float m_TimeSinceRefreshRate;

		class FSocket* m_WriteSocket = nullptr;
		int32 m_SendBufferSize;
		int32 m_ActualSendBufferSize;

		FRunnableThread* m_thread;
		volatile bool m_ReadThreadRunning;

		//Check messages from server
		void CheckLauncherMessages();
		TQueue<MessageWithData*> m_launcherMessages;

		//Handle messages
		TMap<FString, LauncherCommsCallback> m_MessageCallbacks;

		MessageReader* m_MessageReader = nullptr;
};


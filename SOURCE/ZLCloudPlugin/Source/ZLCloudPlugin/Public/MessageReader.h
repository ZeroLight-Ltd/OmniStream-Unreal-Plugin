// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "CloudStream2dll.h"
#include "CoreFwd.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "IPAddress.h"
#include "Sockets.h"
#include "MessageHandler.h"
#include "SocketSubsystem.h"


class MessageReader
{
private:
	class FSocket* m_listeningSocket;
	class FSocket* m_ReadSocket;
	unsigned char* m_messageBuffer;
	unsigned char* m_recvBuffer;
	int m_messageBufferSize;
	int m_recvBufferSize;

	int m_messageBufferWritePos;
	int m_messageBufferReadPos;
	int m_listenPort;
	bool m_socketError;

	void ShutdownSocket(FSocket* socket);

public:
	MessageReader(int listenPort);
	~MessageReader();

	bool Start();
	void Quit();
	void Finish();

	bool Error();
	MessageWithData* GetMessage();
	int FillBuffer();

};


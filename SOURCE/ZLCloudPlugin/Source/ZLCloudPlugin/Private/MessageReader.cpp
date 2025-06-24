// Copyright ZeroLight ltd. All Rights Reserved.
#include "MessageReader.h"
#include "CoreMinimal.h"
#include "ZLCloudPluginPrivate.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"

MessageReader::MessageReader(int listenPort)
{
	m_listenPort = listenPort;

	m_messageBufferSize = 16 * 1024 * 1024;//16mb
	m_recvBufferSize = 1024;

	m_messageBuffer = new unsigned char[m_messageBufferSize];
	m_messageBufferWritePos = 0;
	m_messageBufferReadPos = 0;
	m_socketError = false;
	m_recvBuffer = new unsigned char[m_recvBufferSize];
	m_ReadSocket = nullptr;
	m_listeningSocket = nullptr;
}

MessageReader::~MessageReader()
{
	if (m_messageBuffer)
	{
		delete[] m_messageBuffer;
		m_messageBuffer = nullptr;
	}

	if (m_recvBuffer)
	{
		delete[] m_recvBuffer;
		m_recvBuffer = nullptr;
	}
}


bool MessageReader::Start()
{
	if (m_listeningSocket == nullptr)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Opening Server Listen Socket: %d"), m_listenPort);

		TSharedRef<FInternetAddr> internetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
		bool bIsValid = false;
		internetAddr->SetIp(0x7f000001);	// 127.0.0.1 //internetAddr->SetIp(FIPv4Endpoint::Any);
		internetAddr->SetPort(m_listenPort);

		m_listeningSocket = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateSocket(NAME_Stream, TEXT("ZLListeningSocket"), false);
		if (!m_listeningSocket)
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to create write socket"));
			return false;
		}
		m_listeningSocket->SetReuseAddr(true);
		m_listeningSocket->SetLinger(false, 0);

		if (m_listeningSocket->Bind(*internetAddr))
		{
			if (m_listeningSocket->Listen(5))
			{
				UE_LOG(LogZLCloudPlugin, Display, TEXT("Listening socket active"));
			}
			else
			{
				UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to listen on m_listeningSocket"));
				return false;
			}
		}
		else
		{
			UE_LOG(LogZLCloudPlugin, Error, TEXT("Failed to bind m_listeningSocket"));
			return false;
		}

	}

	m_messageBufferWritePos = 0;
	m_messageBufferReadPos = 0;

	m_socketError = false;

	TSharedRef<FInternetAddr> RemoteAddress = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
	m_ReadSocket = m_listeningSocket->Accept(*RemoteAddress, TEXT("ZLReadSocket"));
	m_ReadSocket->SetNonBlocking(true);

	return true;
}


void MessageReader::Quit()
{
	ShutdownSocket(m_ReadSocket);
	ShutdownSocket(m_listeningSocket);

	m_ReadSocket = nullptr;
	m_listeningSocket = nullptr;
}

void MessageReader::Finish()
{
	ShutdownSocket(m_ReadSocket);
	m_ReadSocket = nullptr;
	m_socketError = false;
}

void MessageReader::ShutdownSocket(FSocket* socket)
{
	if (socket != nullptr)
	{
		UE_LOG(LogZLCloudPlugin, Display, TEXT("Closing connection: %s"), *socket->GetDescription());
		socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(socket);
	}
}

bool MessageReader::Error()
{
	return m_socketError;
}

MessageWithData* MessageReader::GetMessage()
{
	int nextMessageSpace = m_messageBufferWritePos - m_messageBufferReadPos;
	if (nextMessageSpace > 3)
	{
		//at least have a message length...
		int msgLength = (int)m_messageBuffer[m_messageBufferReadPos + 0] * 65536
			+ (int)m_messageBuffer[m_messageBufferReadPos + 1] * 256
			+ (int)m_messageBuffer[m_messageBufferReadPos + 2];
		msgLength += 3; //including the 3 length bytes too

		if (nextMessageSpace >= msgLength)
		{
			//have all of this message
			MessageWithData* messageWithData = new MessageWithData(m_messageBuffer, m_messageBufferReadPos + 3, msgLength + m_messageBufferReadPos);
			messageWithData->SetData(m_messageBuffer);

			m_messageBufferReadPos += msgLength;
			return messageWithData;
		}
	}

	//have processed all available messages, remove them from buffer...
	if (m_messageBufferReadPos > 0)
	{
		int remainingMessageDataLength = m_messageBufferWritePos - m_messageBufferReadPos;
		for (int i = 0; i < remainingMessageDataLength; i++)
		{
			m_messageBuffer[i] = m_messageBuffer[i + m_messageBufferReadPos];
		}
		m_messageBufferReadPos = 0;                             //i.e. moved down by m_messageBufferReadPos
		m_messageBufferWritePos = remainingMessageDataLength;   //i.e. moved down by m_messageBufferReadPos
	}

	//...and fetch more from socket
	FillBuffer();

	return nullptr;
}

int MessageReader::FillBuffer()
{
	int numRec = 0;

	int bufferSpace = m_messageBufferSize - m_messageBufferWritePos;
	if ((bufferSpace >= m_recvBufferSize) && (m_ReadSocket != nullptr))
	{
		//Receive any data from the socket and add it to the buffer
		if (m_ReadSocket->Recv((uint8*)m_recvBuffer, m_recvBufferSize, numRec))
		{			
			for (int i = 0; i < numRec; ++i)
			{
				m_messageBuffer[m_messageBufferWritePos + i] = m_recvBuffer[i];
			}
			m_messageBufferWritePos += numRec;
		}
		
	}

	return numRec;
}



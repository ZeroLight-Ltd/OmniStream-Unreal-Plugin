// Copyright ZeroLight ltd. All Rights Reserved.
#include "MessageHandler.h"


MessageWithData::MessageWithData(unsigned char* messageBuffer, int messageStart, int nextMessageStart)
{
	int maxSearchPos = messageStart + m_maxMessageLength;
	if (maxSearchPos > nextMessageStart)
	{
		maxSearchPos = nextMessageStart;
	}

	int cutPos = -1;
	m_messageDataStart = -1;
	for (int i = messageStart; i < maxSearchPos; i++)
	{
		char c = messageBuffer[i];
		if ((c == ':') || (c <= ' '))	//separator is first colon or whitespace
		{
			cutPos = i;					//message name is before this...
			m_messageDataStart = i + 1;	//...and message data is after
			break;
		}
	}

	m_hasReply = false;
	m_jsonData = false;

	m_messageName = FString(((cutPos > 0) ? cutPos : nextMessageStart) - messageStart, (char*)&messageBuffer[messageStart]);
	m_messageDataEnd = (nextMessageStart >= 0) ? nextMessageStart : m_messageName.Len();
}


void MessageWithData::SetData(unsigned char* messageBuffer)
{
	if (m_messageDataStart >= 0)	//will be negative if there is no data following
	{
		//skip any whitespace at start
		while ((m_messageDataStart < m_messageDataEnd) && (messageBuffer[m_messageDataStart] <= ' '))
		{
			m_messageDataStart++;
		}
		
		int stringSize = m_messageDataEnd - m_messageDataStart;
		unsigned char* messageDataString = new unsigned char[stringSize+1];
		memcpy(messageDataString, &messageBuffer[m_messageDataStart], stringSize);
		messageDataString[stringSize] = '\0';

		m_messageData = FString(UTF8_TO_TCHAR((char*)messageDataString));

		m_messageDataStart = -1;    //don't do it again
		delete[] messageDataString;

		TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(m_messageData);
		if (FJsonSerializer::Deserialize(JsonReader, m_messageJSON))
		{
			m_jsonData = true;
		}
		
	}
}


void MessageWithData::SetReply(FString replyMessageName, FString replyMessageData /*= ""*/)
{
	m_hasReply = true;
	m_replyMessageName = replyMessageName;
	m_replyMessageData = replyMessageData;
}



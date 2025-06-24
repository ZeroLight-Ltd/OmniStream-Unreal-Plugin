// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#include "ZLCloudPluginPrivate.h"
#include "CoreMinimal.h"
#include "CoreFwd.h"
#include "Json.h"

class MessageWithData
{
public:
	MessageWithData(unsigned char* messageBuffer, int messageStart, int nextMessageStart);
	void SetData(unsigned char* messageBuffer);
	void SetReply(FString replyMessageName, FString replyMessageData = "");

	const int m_maxMessageLength = 64;

	FString m_messageName;
	FString m_messageData;
	int m_messageDataStart;
	int m_messageDataEnd;

	bool m_hasReply;
	FString m_replyMessageName;
	FString m_replyMessageData;

	bool m_jsonData;
	TSharedPtr<FJsonObject> m_messageJSON;
};


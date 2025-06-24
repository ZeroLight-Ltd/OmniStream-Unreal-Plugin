// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace ZLProtocol
{
	enum class ZLCLOUDPLUGIN_API EZLCloudPluginMessageTypes
	{
		Uint8,
		Uint16,
		Int16,
		Float,
		Double,
		Variable,
		Undefined
	};

	enum class ZLCLOUDPLUGIN_API EZLCloudPluginMessageDirection : uint8
	{
		ToStreamer = 0,
		FromStreamer = 1
	};

	class ZLCLOUDPLUGIN_API FZLCloudPluginInputMessage
	{
	private:
		static inline uint8 CurrentId = 0;
	public:
		FZLCloudPluginInputMessage()
		: Id(++CurrentId)
		, Structure({ EZLCloudPluginMessageTypes::Undefined })
		{
		}

		FZLCloudPluginInputMessage(uint8 InByteLength, TArray<EZLCloudPluginMessageTypes> InStructure)
		: Id(++CurrentId)
		, ByteLength(InByteLength)
		, Structure(InStructure)
		{
		}

		FZLCloudPluginInputMessage(uint8 InId)
		: Id(InId)
		, Structure({ EZLCloudPluginMessageTypes::Undefined })
		{
			// We set id to 255 for the Protocol message type. 
			// We don't want to set the CurrentId to this value otherwise we will have no room for custom messages
			if(InId > CurrentId && InId != 255)
			{
				CurrentId = InId; // Ensure that any new message id starts after the highest specified Id
			}
		}

		FZLCloudPluginInputMessage(uint8 InId, uint8 InByteLength, TArray<EZLCloudPluginMessageTypes> InStructure)
		: Id(InId)
		, ByteLength(InByteLength)
		, Structure(InStructure)
		{
			// We set id to 255 for the Protocol message type. 
			// We don't want to set the CurrentId to this value otherwise we will have no room for custom messages
			if(InId > CurrentId && InId != 255)
			{
				CurrentId = InId; // Ensure that any new message id starts after the highest specified Id
			}
		}

		uint8 Id;
		uint8 ByteLength;
		TArray<EZLCloudPluginMessageTypes> Structure;
	};

	struct ZLCLOUDPLUGIN_API FZLCloudPluginProtocol
	{
	public:
		TMap<FString, FZLCloudPluginInputMessage> ToStreamerProtocol;
		TMap<FString, FZLCloudPluginInputMessage> FromStreamerProtocol;
	};
}; // namespace ZLProtocol

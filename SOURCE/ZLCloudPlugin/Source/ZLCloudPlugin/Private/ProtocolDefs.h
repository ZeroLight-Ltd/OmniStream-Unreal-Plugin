// Copyright ZeroLight ltd. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "api/data_channel_interface.h"

#if 0
namespace ZLCloudPlugin {
	namespace Protocol
	{
		enum class EToStreamerMsg : uint8
		{
			/**********************************************************************/

			/*
			* Control Messages. Range = 0..49.
			*/

			//none currently

			/**********************************************************************/

			/*
			* Input Messages. Range = 50..89.
			*/

			// Generic Input Messages. Range = 50..59.
			UIInteraction = 50,
			Command = 51,

			// Keyboard Input Message. Range = 60..69.
			KeyDown = 60,
			KeyUp = 61,
			KeyPress = 62,

			// Mouse Input Messages. Range = 70..79.
			MouseEnter = 70,
			MouseLeave = 71,
			MouseDown = 72,
			MouseUp = 73,
			MouseMove = 74,
			MouseWheel = 75,

			// Touch Input Messages. Range = 80..89.
			TouchStart = 80,
			TouchEnd = 81,
			TouchMove = 82,

			// Gamepad Input Messages. Range = 90..99
			GamepadButtonPressed = 90,
			GamepadButtonReleased = 91,
			GamepadAnalog = 92,

			/**********************************************************************/

			/*
			* Ensure Count is the final entry.
			*/
			Count

			/**********************************************************************/
		};

	}; // namespace ZLCloudPlugin::Protocol
}

#endif

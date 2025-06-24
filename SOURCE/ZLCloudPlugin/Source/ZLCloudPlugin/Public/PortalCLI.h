// Copyright ZeroLight ltd. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include <iostream>
#include <cstdlib>
#include <cstring>
#include "Interfaces/IPluginManager.h"
namespace portal {
	extern "C" {
#include "portal.h"
	}
}

DECLARE_LOG_CATEGORY_EXTERN(LogPortalDllCLI, Log, All);

static bool s_CIMode = false;

void fetch_error();

template<typename T> T* handle_ptr_error(T* ptr) {
	if (ptr == nullptr) {
		fetch_error();
	}
	return ptr;
}

class FFIString {
public:
	portal::FFIString m_s;
	FFIString(portal::FFIString s) : m_s(s) {
		if (m_s.c_str == nullptr) {
			fetch_error();
			throw "unreachable";
		}
	}
	~FFIString() {
		portal::ffi_string_destroy(m_s);
	}
	char* c_str() {
		return m_s.c_str;
	}
	inline void free() {
		portal::ffi_string_destroy(m_s);
	}
	inline FString ToFString() { return FString(m_s.c_str); }
};

class UTF8InputString {
public:
	portal::UTF8InputString m_inputString;
	UTF8InputString(const uint8_t* strPtr, uint32_t length) {
		m_inputString.pointer = strPtr;
		m_inputString.length_in_bytes = length;
	}
	~UTF8InputString() {
	}
};

class Config {
public:
	portal::Config* m_cfg;
	Config() : m_cfg(portal::config_new()) {}
	~Config() {
		if (m_cfg != nullptr) {
			portal::config_destroy(m_cfg);
		}
	}

	bool is_valid() {
		return m_cfg == nullptr;
	}

	void add_scope(char* scope) {
		if (!portal::config_add_scope(m_cfg, scope)) {
			fetch_error();
		}
	}
};

class OutputMessage {
public:
	portal::OutputMessage* m_output_message;
	OutputMessage(portal::OutputMessage* m_output_message) :
		m_output_message(m_output_message) {
	}
	~OutputMessage() {
		if (m_output_message != nullptr) {
			portal::output_message_destroy(m_output_message);
		}
	}
	// This MUST be checked before trying to use the message
	bool is_eof() {
		return m_output_message == nullptr;
	}
	bool print();
	FString ToFString();
};

class ProgressStream
{
public:
	portal::ProgressStream* m_progress_stream;
	ProgressStream(portal::ProgressStream* m_progress_stream) : m_progress_stream(m_progress_stream) {}
	~ProgressStream()
	{
		portal::progress_stream_destroy(m_progress_stream);
	}

	OutputMessage get_next_message()
	{
		portal::GetNextMessageResult result = portal::progress_stream_get_next_message(m_progress_stream);
		if (result.output_message == nullptr && !result.success)
		{
			fetch_error();
			throw "unreachable";
		}
		else {
			// Null pointer indicates an EOF message
			return OutputMessage(result.output_message);
		}
	}
};

class EnsureTokenResult {
public:
	portal::EnsureTokenResult* m_token_result;
	EnsureTokenResult(portal::EnsureTokenResult* m_token_result) : m_token_result(m_token_result) {
	}
	~EnsureTokenResult() {
		if (m_token_result != nullptr) {
			portal::ensure_token_task_result_destory(m_token_result);
		}
	}
	// This MUST be checked before trying to use the message
	bool is_eof() {
		return m_token_result == nullptr;
	}

};

class PortalClient {
public:
	static void* m_portalDLLHandle;
	static bool m_pluginLibLoaded;

	static bool InitPlugin();

	static void FreePlugin();

	portal::Client* m_client;

	PortalClient() {};
	PortalClient(Config* cfg);
	PortalClient(Config* cfg, portal::BuildContext* bc);
	~PortalClient();



	OutputMessage create_build(char* json);

	OutputMessage create_stage(char* json) {
		size_t len = strlen(json);
		portal::OutputMessage* msg = handle_ptr_error(portal::client_create_stage(
			m_client,
			/* Danger, super platform specific! */
			reinterpret_cast<const uint8_t*>(&json[0]),
			len
		));
		return OutputMessage(msg);
	}
	OutputMessage upload(char* json) {
		size_t len = strlen(json);
		portal::OutputMessage* msg = handle_ptr_error(portal::client_upload(
			m_client,
			/* Danger, super platform specific! */
			reinterpret_cast<const uint8_t*>(&json[0]),
			len
		));
		return OutputMessage(msg);
	}

	OutputMessage finalize_stage(char* json) {
		size_t len = strlen(json);
		portal::OutputMessage* msg = handle_ptr_error(portal::client_finalize_stage(
			m_client,
			/* Danger, super platform specific! */
			reinterpret_cast<const uint8_t*>(&json[0]),
			len
		));
		return OutputMessage(msg);
	}
	OutputMessage finalize_build(char* json) {
		size_t len = strlen(json);
		portal::OutputMessage* msg = handle_ptr_error(portal::client_finalize_build(
			m_client,
			/* Danger, super platform specific! */
			reinterpret_cast<const uint8_t*>(&json[0]),
			len
		));
		return OutputMessage(msg);
	}

	ProgressStream* take_progress_stream() {
		portal::ProgressStream* ps = portal::client_take_progress_stream(m_client);
		return new ProgressStream(ps);
	}

	OutputMessage client_get_asset_line(const char* asset_line_id)
	{
		size_t asset_line_id_length_in_bytes = strlen(asset_line_id);
		UTF8InputString assetLineInputStr = UTF8InputString(reinterpret_cast<const uint8_t*>(&asset_line_id[0]), asset_line_id_length_in_bytes);
		portal::OutputMessage* msg = handle_ptr_error(portal::client_get_asset_line(
			m_client,
			assetLineInputStr.m_inputString
		));

		return OutputMessage(msg);
	}
};

inline void* read_output(void* ptr) {
	// Note: this thread owns this pointer
	ProgressStream* ps = (ProgressStream*)ptr;
	while (true) {
		OutputMessage msg = ps->get_next_message();
		if (msg.is_eof()) {
			break;
		}
		msg.print();
	}

	// TODO: Exception safety, maybe c++ smart pointer
	delete ps;
	return nullptr;
}
#endif
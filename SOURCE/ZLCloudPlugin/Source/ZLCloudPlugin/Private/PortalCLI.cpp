// Copyright ZeroLight ltd. All Rights Reserved.
#if WITH_EDITOR

#include "PortalCLI.h"

DEFINE_LOG_CATEGORY(LogPortalDllCLI);

void* PortalClient::m_portalDLLHandle = nullptr;
bool PortalClient::m_pluginLibLoaded = false;

void fetch_error() {
    UE_LOG(LogPortalDllCLI, Error, TEXT("*** Portal Error ***"));
    portal::PortalError* e = portal::take_last_error();
    if (e != nullptr) {
        portal::FFIString e_msg = portal::error_get_message(e);
        FString errStr = e_msg.c_str;
        if (!errStr.IsEmpty())
        {
            FString ueLogFriendlyMsg = errStr.Replace(TEXT("\n"), TEXT(" "));
            UE_LOG(LogPortalDllCLI, Error, TEXT(" %s"), *ueLogFriendlyMsg);
        }
        portal::error_destory(e);
        //throw "unexpected error";

        if (s_CIMode)
        {
            std::exit(-1);
        }
    }
    else {
        throw "unneeded fetch_error() call";
    }
}

namespace
{
portal::UTF8InputString MakeUtf8Input(const char* value)
{
    portal::UTF8InputString input = {};
    if (value != nullptr)
    {
        input.pointer = reinterpret_cast<const uint8_t*>(value);
        input.length_in_bytes = static_cast<uint32_t>(strlen(value));
    }
    return input;
}
}

PortalClient::PortalClient(Config* cfg)
{
    m_client = handle_ptr_error(portal::client_new_from_config(cfg->m_cfg));
    // The config is consumed to create the client
    cfg->m_cfg = nullptr;
}

PortalClient::~PortalClient() 
{
    portal::client_destroy(m_client);
}

bool PortalClient::InitPlugin()
{
    if (m_portalDLLHandle != nullptr)
        return true;

    // Get the base directory of this plugin
    FString BaseDir = IPluginManager::Get().FindPlugin("ZLCloudPlugin")->GetBaseDir();

    FString LibraryPath;
#if PLATFORM_WINDOWS
    LibraryPath = FPaths::Combine(*BaseDir, TEXT("Binaries/ThirdParty/Portal/Win64/portal_client_c.dll"));
#elif PLATFORM_MAC
    LibraryPath = FPaths::Combine(*BaseDir, TEXT("Source/ThirdParty/Portal/Mac/Release/portal_client_c.dylib"));
#elif PLATFORM_LINUX
    LibraryPath = FPaths::Combine(*BaseDir, TEXT("Binaries/ThirdParty/Portal/Linux/x86_64-unknown-linux-gnu/portal_client_c.so"));
#endif // PLATFORM_WINDOWS

    m_portalDLLHandle = !LibraryPath.IsEmpty() ? FPlatformProcess::GetDllHandle(*LibraryPath) : nullptr;

    if (m_portalDLLHandle && !m_pluginLibLoaded)
    {
        m_pluginLibLoaded = true;

        FString LogOutputDir = FPaths::Combine(FPaths::GetPath(FPaths::ProjectSavedDir()), TEXT("Logs"));
        FString LogOutputPath = FPaths::Combine(LogOutputDir, TEXT("portalcli.log"));

        if (!FPaths::DirectoryExists(LogOutputDir))
        {
            IPlatformFile& platformFile = FPlatformFileManager::Get().GetPlatformFile();

            platformFile.CreateDirectory(*LogOutputDir);
        }

        std::string utf8String(TCHAR_TO_UTF8(*LogOutputPath));

        int stringSize = utf8String.length() + 1;
        char* pSendData = new char[stringSize];
        strcpy_s(pSendData, stringSize, utf8String.c_str());

        bool success = portal::start_logging(pSendData, true, true);
        if (success)
        {
            UE_LOG(LogPortalDllCLI, Log, TEXT("Portal client logging started"));
        }
        else
        {
            UE_LOG(LogPortalDllCLI, Error, TEXT("Portal client logging failed"));
            fetch_error();
        }
    }
    else
    {
        UE_LOG(LogPortalDllCLI, Error, TEXT("Failed to load Portal Client dll"));
        return false;
    }

    return m_pluginLibLoaded;
}

OutputMessage PortalClient::create_portal_asset_version(
    const char* asset_line_id,
    const char* storage_provider,
    const char* extension_data,
    const char* upload_dir,
    uint64_t chunk_size,
    const char* source_dir,
    bool upload_git_hashes,
    const char* comment)
{
    const char* commentArg = (comment != nullptr && comment[0] != '\0') ? comment : nullptr;
    portal::OutputMessage* msg = handle_ptr_error(portal::client_create_portal_asset_version(
        m_client,
        MakeUtf8Input(asset_line_id),
        MakeUtf8Input(storage_provider),
        MakeUtf8Input(extension_data),
        MakeUtf8Input(upload_dir),
        chunk_size,
        MakeUtf8Input(source_dir),
        upload_git_hashes,
        MakeUtf8Input(commentArg)));
    return OutputMessage(msg);
}

OutputMessage PortalClient::finalize_portal_asset_version(const char* asset_version_id)
{
    portal::OutputMessage* msg = handle_ptr_error(portal::client_finalize_portal_asset_version(
        m_client,
        MakeUtf8Input(asset_version_id)));
    return OutputMessage(msg);
}

OutputMessage PortalClient::portal_upload(const char* asset_version_id, const char* directory)
{
    portal::OutputMessage* msg = handle_ptr_error(portal::client_portal_upload(
        m_client,
        MakeUtf8Input(asset_version_id),
        MakeUtf8Input(directory)));
    return OutputMessage(msg);
}

OutputMessage PortalClient::upload_portal_asset_version_one_step(portal::OneStepUploadParams params)
{
    portal::OutputMessage* msg = handle_ptr_error(portal::client_upload_portal_asset_version_one_step(
        m_client,
        params));
    return OutputMessage(msg);
}

void PortalClient::FreePlugin()
{
    if (m_pluginLibLoaded)
    {
        // Free the dll handle
        FPlatformProcess::FreeDllHandle(m_portalDLLHandle);
        m_portalDLLHandle = nullptr;
    }

    m_pluginLibLoaded = false;
}

bool OutputMessage::print()
{
    if (m_output_message != nullptr) {
        FFIString s = portal::output_message_format_text(m_output_message);
        FString msg = FString(s.c_str());
        if (!msg.IsEmpty())
        {
            FString ueLogFriendlyMsg = msg.Replace(TEXT("\n"), TEXT(" "));
            UE_LOG(LogPortalDllCLI, Log, TEXT(" %s"), *ueLogFriendlyMsg);
            return true;
        }
    }
    else {
        throw "print() called on eof message";
    }
    return false;
}

FString OutputMessage::ToFString()
{
    if (m_output_message != nullptr) {
        FFIString s = portal::output_message_format_text(m_output_message);
        FString msg = FString(s.c_str());
        return msg;
    }
    else {
        throw "ToFString() called on eof message";
    }
}

#endif
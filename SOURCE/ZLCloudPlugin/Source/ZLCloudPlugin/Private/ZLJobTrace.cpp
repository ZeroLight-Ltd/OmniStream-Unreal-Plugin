#include "ZLJobTrace.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/DateTime.h"
#include "IZLCloudPluginModule.h"
#include "ZLCloudPluginModule.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogZLJobTrace, Log, All);

ZLJobTrace* ZLJobTrace::__Instance = nullptr;
int32 ZLJobTrace::S_ActiveTracingId = -1;

double FJobTraceTimer::UTC_Now()
{
    return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
}

FJobTraceTimer::FJobTraceTimer(const FString& InName)
    : Name(InName)
{
    StartTimeTS = UTC_Now();
    EndTimeTS = 0;
}

FJobTraceTimer::FJobTraceTimer(const FString& InName, double InBaseOffsetTimeTS)
    : Name(InName), OffsetBaseTimeTS(InBaseOffsetTimeTS)
{
    StartTimeTS = UTC_Now();
    EndTimeTS = 0;
}

void FJobTraceTimer::EndTimer()
{
    if (EndTimeTS == 0)
    {
        EndTimeTS = UTC_Now();
    }
}

double FJobTraceTimer::ToMicroseconds(double Seconds)
{
    return FMath::RoundToDouble(Seconds * 1000000.0);
}

TSharedPtr<FJsonObject> FJobTraceTimer::ToChromeTraceEventsJSON() const
{
    double StartTime = StartTimeTS - OffsetBaseTimeTS;
    double TotalTime = (EndTimeTS - OffsetBaseTimeTS) - StartTime;

    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField("name", Name);
    JsonObject->SetStringField("cat", "PERF");
    JsonObject->SetStringField("ph", "X");
    JsonObject->SetStringField("pid", FPaths::GetCleanFilename(FPlatformProcess::GetApplicationName(FPlatformProcess::GetCurrentProcessId())));
    JsonObject->SetNumberField("tid", 1);
    JsonObject->SetNumberField("ts", ToMicroseconds(StartTime));
    JsonObject->SetNumberField("dur", FMath::Max(ToMicroseconds(TotalTime), 0.0));

    return JsonObject;
}

// FZLJobTraceData implementation
double FZLJobTraceData::UTC_Now()
{
    return (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
}

FZLJobTraceData::FZLJobTraceData(int32 InID, double InBaseTimeOffsetTS)
    : ID(InID), BaseTimeOffsetTS(InBaseTimeOffsetTS)
{
    StartTraceTS = UTC_Now();
    Timers.Add("TotalJobTime", FJobTraceTimer("TotalJobTime", BaseTimeOffsetTS));
}

TArray<TSharedPtr<FJsonValue>> FZLJobTraceData::TimersToChromeTraceJSON()
{
    TArray<TSharedPtr<FJsonValue>> TraceEvents;
    for (const auto& TimerPair : Timers)
    {
        TSharedPtr<FJsonObject> TimerJson = TimerPair.Value.ToChromeTraceEventsJSON();
        TraceEvents.Add(MakeShareable(new FJsonValueObject(TimerJson)));
    }
    return TraceEvents;
}

TSharedPtr<FJsonObject> FZLJobTraceData::ToDict()
{
    TSharedPtr<FJsonObject> JobTraceData = MakeShareable(new FJsonObject());
    TArray<TSharedPtr<FJsonValue>> TimingData = TimersToChromeTraceJSON();

    JobTraceData->SetNumberField("trace_id", ID);
    JobTraceData->SetArrayField("ve_timings", TimingData);
    
    TSharedPtr<FJsonObject> ConfigureDataObject = MakeShareable(new FJsonObject());
    for(const auto& Pair : ConfigureData)
    {
        ConfigureDataObject->SetField(Pair.Key, Pair.Value);
    }
    JobTraceData->SetObjectField("ve_configure_data", ConfigureDataObject);

    JobTraceData->SetNumberField("ve_trace_start_ts", StartTraceTS);

    return JobTraceData;
}

void FZLJobTraceData::StartTimer(const FString& Name)
{
    if (Timers.Contains(Name))
    {
        UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Duplicate StartTimer call for %s detected!"), *Name);
        return;
    }
    Timers.Add(Name, FJobTraceTimer(Name, BaseTimeOffsetTS));
}

void FZLJobTraceData::EndTimer(const FString& Name)
{
    if (FJobTraceTimer* Timer = Timers.Find(Name))
    {
        Timer->EndTimer();
    }
    else
    {
        UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Trace data timer %s requested end but no entry in active job"), *Name);
    }
}

bool FZLJobTraceData::HasTimer(const FString& Name)
{
    return Timers.Contains(Name);
}

// ZLJobTrace implementation
ZLJobTrace::ZLJobTrace() {}

ZLJobTrace* ZLJobTrace::Get()
{
    if (__Instance == nullptr)
    {
        __Instance = new ZLJobTrace();
    }
    return __Instance;
}

void ZLJobTrace::JOBTRACE_TIMER_START(const FString& Name)
{
    if (!ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsContentGenerationMode())
        return;

    if (Get()->JobTraceDataDict.Contains(S_ActiveTracingId))
    {
        Get()->JobTraceDataDict[S_ActiveTracingId].StartTimer(Name);
    }
}

bool ZLJobTrace::JOBTRACE_IS_CURRENTLY_TIMING(const FString& Name)
{
    if (!ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsContentGenerationMode())
        return false;

    if (Get()->JobTraceDataDict.Contains(S_ActiveTracingId))
    {
        return Get()->JobTraceDataDict[S_ActiveTracingId].HasTimer(Name);
    }

    return false;
}

void ZLJobTrace::JOBTRACE_TIMER_END(const FString& Name)
{
    if (!ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsContentGenerationMode())
        return;

    if (Get()->JobTraceDataDict.Contains(S_ActiveTracingId))
    {
        Get()->JobTraceDataDict[S_ActiveTracingId].EndTimer(Name);
    }
}

void ZLJobTrace::JOBTRACE_ADD_DATA(const FString& Key, TSharedPtr<FJsonValue> Value)
{
    if (!ZLCloudPlugin::FZLCloudPluginModule::GetModule()->IsContentGenerationMode())
        return;

    if (Get()->JobTraceDataDict.Contains(S_ActiveTracingId))
    {
        Get()->JobTraceDataDict[S_ActiveTracingId].ConfigureData.Add(Key, Value);
    }
}

void ZLJobTrace::ON_START_JOBTRACE(const FString& MessageData)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageData);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        int32 JobID = -1;
        if (JsonObject->TryGetNumberField("jobTraceId", JobID))
        {
            double BaseTimeOffsetTS = 0;
            JsonObject->TryGetNumberField("baseTimeOffset", BaseTimeOffsetTS);

            if (Get()->JobTraceDataDict.Contains(JobID))
            {
                UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] Duplicate jobID start requested!"));
            }

            Get()->JobTraceDataDict.Add(JobID, FZLJobTraceData(JobID, BaseTimeOffsetTS));
            S_ActiveTracingId = JobID;

        }
    }
}

FString ZLJobTrace::ON_END_JOBTRACE(const FString& MessageData)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageData);

    FString JsonString;

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        int32 JobID = -1;
        if (JsonObject->TryGetNumberField("jobTraceId", JobID))
        {
            if (FZLJobTraceData* JobData = Get()->JobTraceDataDict.Find(JobID))
            {
                JobData->EndTimer("TotalJobTime");
                TSharedPtr<FJsonObject> JobTraceData = JobData->ToDict();

                TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
                FJsonSerializer::Serialize(JobTraceData.ToSharedRef(), Writer);

                S_ActiveTracingId = -1;
                Get()->JobTraceDataDict.Remove(JobID); // Clean up finished job
            }
            else
            {
                UE_LOG(LogZLJobTrace, Warning, TEXT("[ZLJobTraceData] JobTrace with ID %d not found for ON_END_JOBTRACE"), JobID);
            }
        }
    }

    return JsonString;
}

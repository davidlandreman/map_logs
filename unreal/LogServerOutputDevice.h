// LogServerOutputDevice.h
// Custom FOutputDevice that sends UE_LOG output to a remote log server via UDP
//
// Usage:
//   1. Add this header to your UE project
//   2. In your game module's StartupModule() or GameInstance::Init():
//      GLog->AddOutputDevice(new FLogServerOutputDevice(TEXT("127.0.0.1"), 52099, TEXT("client")));
//   3. For dedicated server, use TEXT("server") as the source name
//
// The log server must be running to receive logs. Logs are sent as JSON over UDP.

#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

class FLogServerOutputDevice : public FOutputDevice
{
public:
    /**
     * Constructor
     * @param Host - The log server hostname or IP (e.g., "127.0.0.1")
     * @param Port - The log server UDP port (default: 52099)
     * @param SourceName - Identifier for this log source (e.g., "client" or "server")
     */
    FLogServerOutputDevice(const FString& Host = TEXT("127.0.0.1"), int32 Port = 52099, const FString& SourceName = TEXT("client"))
        : SourceName(SourceName)
        , bIsInitialized(false)
    {
        // Generate unique instance ID: {source}_{timestamp_ms}_{random_hex}
        int64 TimestampMs = FDateTime::UtcNow().ToUnixTimestamp() * 1000 + FDateTime::UtcNow().GetMillisecond();
        uint32 RandomPart = FMath::Rand();
        InstanceId = FString::Printf(TEXT("%s_%lld_%04x"), *SourceName, TimestampMs, RandomPart & 0xFFFF);

        // Create UDP socket
        ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
        if (!SocketSubsystem)
        {
            UE_LOG(LogTemp, Error, TEXT("LogServerOutputDevice: Failed to get socket subsystem"));
            return;
        }

        Socket = SocketSubsystem->CreateSocket(NAME_DGram, TEXT("LogServerSocket"), false);
        if (!Socket)
        {
            UE_LOG(LogTemp, Error, TEXT("LogServerOutputDevice: Failed to create UDP socket"));
            return;
        }

        // Resolve server address
        ServerAddr = SocketSubsystem->CreateInternetAddr();
        bool bIsValid = false;
        ServerAddr->SetIp(*Host, bIsValid);

        if (!bIsValid)
        {
            // Try resolving as hostname
            ESocketErrors ResolveError = SocketSubsystem->GetHostByName(TCHAR_TO_ANSI(*Host), *ServerAddr);
            if (ResolveError != SE_NO_ERROR)
            {
                UE_LOG(LogTemp, Error, TEXT("LogServerOutputDevice: Failed to resolve host %s"), *Host);
                return;
            }
        }

        ServerAddr->SetPort(Port);
        bIsInitialized = true;

        UE_LOG(LogTemp, Log, TEXT("LogServerOutputDevice: Initialized with instance '%s', sending to %s:%d"), *InstanceId, *Host, Port);
    }

    /**
     * Set the session ID for this log source.
     * Call this when joining/hosting a game session with a shared identifier (e.g., match ID, world seed).
     * All instances in the same game session should use the same session ID.
     * @param InSessionId - The shared session identifier
     */
    void SetSessionId(const FString& InSessionId)
    {
        SessionId = InSessionId;
        UE_LOG(LogTemp, Log, TEXT("LogServerOutputDevice: Session ID set to '%s' (instance: %s)"), *SessionId, *InstanceId);
    }

    /**
     * Get the current session ID
     */
    FString GetSessionId() const { return SessionId; }

    /**
     * Get the instance ID (unique per app instance)
     */
    FString GetInstanceId() const { return InstanceId; }

    virtual ~FLogServerOutputDevice()
    {
        if (Socket)
        {
            Socket->Close();
            ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
            Socket = nullptr;
        }
    }

    // FOutputDevice interface
    virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category) override
    {
        Serialize(Message, Verbosity, Category, -1.0);
    }

    virtual void Serialize(const TCHAR* Message, ELogVerbosity::Type Verbosity, const FName& Category, const double Time) override
    {
        if (!bIsInitialized || !Socket)
        {
            return;
        }

        // Build JSON payload
        TSharedRef<FJsonObject> JsonObject = MakeShared<FJsonObject>();

        JsonObject->SetStringField(TEXT("source"), SourceName);
        JsonObject->SetStringField(TEXT("category"), Category.ToString());
        JsonObject->SetStringField(TEXT("verbosity"), VerbosityToString(Verbosity));
        JsonObject->SetStringField(TEXT("message"), FString(Message));

        // Timestamp
        double Timestamp = (Time >= 0) ? Time : FPlatformTime::Seconds();
        JsonObject->SetNumberField(TEXT("timestamp"), Timestamp);

        // Frame number
        JsonObject->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));

        // Session and instance identifiers
        JsonObject->SetStringField(TEXT("session_id"), SessionId);
        JsonObject->SetStringField(TEXT("instance_id"), InstanceId);

        // Serialize to string
        FString JsonString;
        TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
        FJsonSerializer::Serialize(JsonObject, Writer);

        // Send via UDP
        int32 BytesSent = 0;
        TArray<uint8> Data;
        Data.Append(reinterpret_cast<const uint8*>(TCHAR_TO_UTF8(*JsonString)), JsonString.Len());

        Socket->SendTo(Data.GetData(), Data.Num(), BytesSent, *ServerAddr);
    }

    virtual bool CanBeUsedOnMultipleThreads() const override
    {
        return true;
    }

    virtual bool CanBeUsedOnAnyThread() const override
    {
        return true;
    }

private:
    static FString VerbosityToString(ELogVerbosity::Type Verbosity)
    {
        switch (Verbosity)
        {
            case ELogVerbosity::Fatal: return TEXT("Fatal");
            case ELogVerbosity::Error: return TEXT("Error");
            case ELogVerbosity::Warning: return TEXT("Warning");
            case ELogVerbosity::Display: return TEXT("Display");
            case ELogVerbosity::Log: return TEXT("Log");
            case ELogVerbosity::Verbose: return TEXT("Verbose");
            case ELogVerbosity::VeryVerbose: return TEXT("VeryVerbose");
            default: return TEXT("Log");
        }
    }

    FString SourceName;
    FString SessionId;      // Shared game session identifier (set via SetSessionId)
    FString InstanceId;     // Unique per app instance (generated in constructor)
    FSocket* Socket = nullptr;
    TSharedPtr<FInternetAddr> ServerAddr;
    bool bIsInitialized;
};


// Convenience macros for initialization
// Add to your game module or game instance

/*
// Example usage in YourGame.cpp (game module):

#include "LogServerOutputDevice.h"

// Store a pointer to access SetSessionId later
FLogServerOutputDevice* LogServerDevice = nullptr;

void FYourGameModule::StartupModule()
{
    // Determine if this is client or server
    FString SourceName = IsRunningDedicatedServer() ? TEXT("server") : TEXT("client");

    // Create and register the output device
    LogServerDevice = new FLogServerOutputDevice(TEXT("127.0.0.1"), 52099, SourceName);
    GLog->AddOutputDevice(LogServerDevice);
}

void FYourGameModule::ShutdownModule()
{
    if (LogServerDevice)
    {
        GLog->RemoveOutputDevice(LogServerDevice);
        delete LogServerDevice;
        LogServerDevice = nullptr;
    }
}

// When joining/hosting a game session, set the session ID:
// This should be a shared identifier known to all participants (e.g., match ID, world seed)
void OnSessionStarted(const FString& MatchId)
{
    if (LogServerDevice)
    {
        LogServerDevice->SetSessionId(MatchId);
    }
}

// Example with world seed:
void OnWorldGenerated(int32 WorldSeed)
{
    if (LogServerDevice)
    {
        LogServerDevice->SetSessionId(FString::Printf(TEXT("%d"), WorldSeed));
    }
}
*/

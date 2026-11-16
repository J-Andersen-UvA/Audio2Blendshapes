#include "AudioFileDecoder.h"

#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Windows/WindowsHWrapper.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include "Windows/HideWindowsPlatformTypes.h"

bool DecodeAudioFile(const FString& Path, TArray<float>& Samples, int32& Rate, int32& Channels, FString& Error)
{
    const FString Extension = FPaths::GetExtension(Path).ToLower();
    if (Extension != TEXT("wav") && Extension != TEXT("mp3"))
    {
        Error = TEXT("Select a WAV or MP3 file.");
        return false;
    }

    using Microsoft::WRL::ComPtr;
    const auto Check = [&Error](HRESULT Status, const TCHAR* Operation)
    {
        if (SUCCEEDED(Status))
        {
            return true;
        }
        Error = FString::Printf(TEXT("%s failed (0x%08x)."), Operation, static_cast<uint32>(Status));
        return false;
    };

    if (!Check(CoInitializeEx(nullptr, COINIT_MULTITHREADED), TEXT("COM initialization")))
    {
        return false;
    }
    ON_SCOPE_EXIT { CoUninitialize(); };
    if (!Check(MFStartup(MF_VERSION), TEXT("Media Foundation startup")))
    {
        return false;
    }
    ON_SCOPE_EXIT { MFShutdown(); };

    constexpr DWORD AudioStream = MF_SOURCE_READER_FIRST_AUDIO_STREAM;
    ComPtr<IMFSourceReader> Reader;
    ComPtr<IMFMediaType> Requested;
    ComPtr<IMFMediaType> Format;
    const FString FullPath = FPaths::ConvertRelativePathToFull(Path);
    if (!Check(MFCreateSourceReaderFromURL(*FullPath, nullptr, Reader.GetAddressOf()), TEXT("Opening audio file")) ||
        !Check(Reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, false), TEXT("Clearing streams")) ||
        !Check(Reader->SetStreamSelection(AudioStream, true), TEXT("Selecting audio stream")) ||
        !Check(MFCreateMediaType(Requested.GetAddressOf()), TEXT("Creating PCM format")) ||
        !Check(Requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), TEXT("Selecting audio format")) ||
        !Check(Requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM), TEXT("Selecting PCM")) ||
        !Check(Requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), TEXT("Selecting PCM16")) ||
        !Check(Reader->SetCurrentMediaType(AudioStream, nullptr, Requested.Get()), TEXT("Configuring decoder")) ||
        !Check(Reader->GetCurrentMediaType(AudioStream, Format.GetAddressOf()), TEXT("Reading PCM format")))
    {
        return false;
    }
    UINT32 SampleRate = 0, NumChannels = 0, Bits = 0;
    if (FAILED(Format->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &SampleRate)) ||
        FAILED(Format->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &NumChannels)) ||
        FAILED(Format->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &Bits)) ||
        Bits != 16 || SampleRate == 0 || NumChannels == 0)
    {
        Error = TEXT("The decoder did not provide a valid PCM16 format.");
        return false;
    }
    Rate = SampleRate;
    Channels = NumChannels;

    for (;;)
    {
        DWORD Flags = 0;
        ComPtr<IMFSample> Sample;
        if (!Check(Reader->ReadSample(AudioStream, 0, nullptr, &Flags, nullptr, Sample.GetAddressOf()), TEXT("Decoding audio")))
        {
            return false;
        }
        if (Flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
        {
            Error = TEXT("The audio stream failed or changed PCM format during decoding.");
            return false;
        }
        if (Sample)
        {
            ComPtr<IMFMediaBuffer> Buffer;
            BYTE* Bytes = nullptr;
            DWORD Length = 0;
            if (!Check(Sample->ConvertToContiguousBuffer(Buffer.GetAddressOf()), TEXT("Reading PCM buffer")) ||
                !Check(Buffer->Lock(&Bytes, nullptr, &Length), TEXT("Locking PCM buffer")))
            {
                return false;
            }
            ON_SCOPE_EXIT { Buffer->Unlock(); };
            const uint32 Count = Length / sizeof(int16);
            if (Length % sizeof(int16) != 0 || Count > static_cast<uint32>(MAX_int32 - Samples.Num()))
            {
                Error = TEXT("Invalid or oversized PCM buffer.");
                return false;
            }
            const int32 Start = Samples.AddUninitialized(Count);
            for (uint32 Index = 0; Index < Count; ++Index)
            {
                int16 Value;
                FMemory::Memcpy(&Value, Bytes + Index * sizeof(Value), sizeof(Value));
                Samples[Start + Index] = Value / 32768.0f;
            }
        }
        if (Flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            return true;
        }
    }
}

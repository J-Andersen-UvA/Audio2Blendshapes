#include "Audio2BlendshapesLibrary.h"

#include "Async/Async.h"
#include "Audio2FaceRuntime.h"
#include "AudioFileDecoder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

UGenerateAudioBlendshapes* UGenerateAudioBlendshapes::GenerateBlendshapesFromPCM(
    const TArray<float>& Samples, int32 SampleRate, int32 Channels, FString ModelPath)
{
    auto* Action = NewObject<UGenerateAudioBlendshapes>();
    Action->InputSamples = Samples;
    Action->InputRate = SampleRate;
    Action->InputChannels = Channels;
    Action->InputModel = MoveTemp(ModelPath);
    return Action;
}

UGenerateAudioBlendshapes* UGenerateAudioBlendshapes::GenerateBlendshapesFromFile(
    const FString& AudioPath, FString ModelPath)
{
    auto* Action = NewObject<UGenerateAudioBlendshapes>();
    Action->bFromFile = true;
    Action->InputFile = AudioPath;
    Action->InputModel = MoveTemp(ModelPath);
    return Action;
}

void UGenerateAudioBlendshapes::Activate()
{
    if (IsRooted())
    {
        return;
    }
    AddToRoot();
    Async(EAsyncExecution::ThreadPool, [this, Samples = MoveTemp(InputSamples), File = MoveTemp(InputFile),
        Model = MoveTemp(InputModel), Rate = InputRate, Channels = InputChannels, bFile = bFromFile]() mutable
    {
        FAudio2BlendshapeClip Clip;
        FString Error;
        const bool bSuccess = (!bFile || DecodeAudioFile(File, Samples, Rate, Channels, Error)) &&
            SolveBlendshapes(MoveTemp(Samples), Rate, Channels, Model, Clip, Error);
        AsyncTask(ENamedThreads::GameThread, [this, bSuccess, Clip = MoveTemp(Clip), Error = MoveTemp(Error)]()
        {
            if (bSuccess)
            {
                Completed.Broadcast(Clip, Error);
            }
            else
            {
                Failed.Broadcast(FAudio2BlendshapeClip{}, Error);
            }
            RemoveFromRoot();
            SetReadyToDestroy();
        });
    });
}

bool UAudio2BlendshapesLibrary::ExportLiveLinkFaceStyleCSV(const FAudio2BlendshapeClip& Clip,
    const FString& Filename, FString& Error)
{
    Error.Reset();
    if (Clip.CurveNames.IsEmpty() || Clip.Frames.IsEmpty() || Clip.FrameRate <= 0 ||
        !FPaths::GetExtension(Filename).Equals(TEXT("csv"), ESearchCase::IgnoreCase))
    {
        Error = TEXT("Provide a blendshape clip and a .csv output path.");
        return false;
    }

    FString CSV = TEXT("Timecode,BlendshapeCount");
    for (FName Name : Clip.CurveNames)
    {
        const FString Column = Name.ToString();
        CSV += TEXT(",");
        if (Column.Contains(TEXT(",")) || Column.Contains(TEXT("\"")) ||
            Column.Contains(TEXT("\r")) || Column.Contains(TEXT("\n")))
        {
            CSV += TEXT("\"") + Column.Replace(TEXT("\""), TEXT("\"\"")) + TEXT("\"");
        }
        else
        {
            CSV += Column;
        }
    }
    CSV += TEXT("\r\n");
    for (const auto& Frame : Clip.Frames)
    {
        if (Frame.Weights.Num() != Clip.CurveNames.Num() ||
            !FMath::IsFinite(Frame.TimeSeconds) || Frame.TimeSeconds < 0 ||
            Frame.TimeSeconds > static_cast<double>(MAX_int32) / Clip.FrameRate)
        {
            Error = TEXT("The clip contains an invalid frame.");
            return false;
        }

        const int32 FrameNumber = FMath::RoundToInt(Frame.TimeSeconds * Clip.FrameRate);
        const int32 Seconds = FrameNumber / Clip.FrameRate;
        CSV += FString::Printf(TEXT("%02d:%02d:%02d:%02d.000,%d"), Seconds / 3600,
            Seconds / 60 % 60, Seconds % 60, FrameNumber % Clip.FrameRate, Frame.Weights.Num());
        for (float Weight : Frame.Weights)
        {
            CSV += FString::Printf(TEXT(",%.7g"), static_cast<double>(Weight));
        }
        CSV += TEXT("\r\n");
    }
    if (!FFileHelper::SaveStringToFile(CSV, *Filename, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        Error = FString::Printf(TEXT("Could not write %s."), *Filename);
        return false;
    }
    return true;
}

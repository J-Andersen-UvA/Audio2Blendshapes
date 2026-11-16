#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Audio2BlendshapesLibrary.generated.h"

USTRUCT(BlueprintType)
struct AUDIO2BLENDSHAPES_API FAudio2BlendshapeFrame
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    double TimeSeconds = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    TArray<float> Weights;
};

USTRUCT(BlueprintType)
struct AUDIO2BLENDSHAPES_API FAudio2BlendshapeClip
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    TArray<FName> CurveNames;

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    TArray<FAudio2BlendshapeFrame> Frames;

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    int32 FrameRate = 60;

    UPROPERTY(BlueprintReadOnly, Category = "Audio2Blendshapes")
    double AudioDurationSeconds = 0;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAudio2BlendshapesCompleted,
    const FAudio2BlendshapeClip&, Clip, const FString&, Error);

UCLASS()
class AUDIO2BLENDSHAPES_API UGenerateAudioBlendshapes : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()

public:
    UPROPERTY(BlueprintAssignable)
    FAudio2BlendshapesCompleted Completed;

    UPROPERTY(BlueprintAssignable)
    FAudio2BlendshapesCompleted Failed;

    UFUNCTION(BlueprintCallable, Category = "Audio2Blendshapes",
        meta = (BlueprintInternalUseOnly = "true", AdvancedDisplay = "ModelPath"))
    static UGenerateAudioBlendshapes* GenerateBlendshapesFromPCM(
        const TArray<float>& Samples, int32 SampleRate, int32 Channels = 1, FString ModelPath = "");

    UFUNCTION(BlueprintCallable, Category = "Audio2Blendshapes",
        meta = (BlueprintInternalUseOnly = "true", AdvancedDisplay = "ModelPath"))
    static UGenerateAudioBlendshapes* GenerateBlendshapesFromFile(
        const FString& AudioPath, FString ModelPath = "");

    virtual void Activate() override;

private:
    TArray<float> InputSamples;
    FString InputFile;
    FString InputModel;
    int32 InputRate = 0;
    int32 InputChannels = 0;
    bool bFromFile = false;
};

UCLASS()
class AUDIO2BLENDSHAPES_API UAudio2BlendshapesLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "Audio2Blendshapes")
    static bool ExportLiveLinkFaceStyleCSV(const FAudio2BlendshapeClip& Clip,
        const FString& Filename, FString& Error);
};

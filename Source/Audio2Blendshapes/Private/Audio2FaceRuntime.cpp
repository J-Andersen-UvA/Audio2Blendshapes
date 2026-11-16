#include "Audio2FaceRuntime.h"

#include "DSP/SampleRateConverter.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Modules/ModuleManager.h"
#include "Windows/WindowsHWrapper.h"

THIRD_PARTY_INCLUDES_START
#include "audio2face/audio2face.h"
#include "audio2x/cuda_utils.h"
THIRD_PARTY_INCLUDES_END

namespace
{
constexpr int32 SolverRate = 16000;
constexpr int32 SolverFrameRate = 60;
FCriticalSection SolverLock;

FString EnvironmentOrDefault(const TCHAR* Key, const FString& Default)
{
    FString Value = FPlatformMisc::GetEnvironmentVariable(Key);
    return Value.IsEmpty() ? Default : Value;
}

class FAudio2BlendshapesModule : public IModuleInterface
{
public:
    bool SupportsDynamicReloading() override { return false; }

    bool LoadRuntime(const FString& SdkRoot, FString& Error)
    {
        if (!Libraries.IsEmpty())
        {
            return true;
        }

        const FString CudaBin = EnvironmentOrDefault(TEXT("CUDA_PATH"),
            TEXT("C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.9")) / TEXT("bin");
        const FString TensorRtLib = EnvironmentOrDefault(TEXT("TENSORRT_ROOT_DIR"),
            SdkRoot / TEXT("_toolkits/TensorRT-10.13.2.6")) / TEXT("lib");
        const FString Paths[] = {
            CudaBin / TEXT("cudart64_12.dll"),
            CudaBin / TEXT("cublasLt64_12.dll"),
            CudaBin / TEXT("cublas64_12.dll"),
            CudaBin / TEXT("curand64_10.dll"),
            TensorRtLib / TEXT("nvinfer_10.dll"),
            TensorRtLib / TEXT("nvinfer_plugin_10.dll"),
            SdkRoot / TEXT("_build/release/audio2x-sdk/bin/audio2x.dll")
        };
        for (const FString& Path : Paths)
        {
            FString NativePath = FPaths::ConvertRelativePathToFull(Path);
            FPaths::MakePlatformFilename(NativePath);
            HMODULE Handle = ::LoadLibraryExW(*NativePath, nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!Handle)
            {
                const uint32 Code = ::GetLastError();
                UnloadRuntime();
                Error = FString::Printf(TEXT("Could not load %s (Win32 error %u)."), *NativePath, Code);
                return false;
            }
            Libraries.Add(Handle);
        }
        return true;
    }

    void ShutdownModule() override
    {
        FScopeLock Lock(&SolverLock);
        UnloadRuntime();
    }

private:
    void UnloadRuntime()
    {
        for (int32 Index = Libraries.Num() - 1; Index >= 0; --Index)
        {
            ::FreeLibrary(Libraries[Index]);
        }
        Libraries.Reset();
    }

    TArray<HMODULE> Libraries;
};

struct FCollectedFrames
{
    FCriticalSection Lock;
    TArray<FAudio2BlendshapeFrame> Frames;
    std::error_code Error;
    int32 WeightCount = 0;
    bool bInvalidWeights = false;
};

void ReceiveFrame(void* User, const nva2f::IBlendshapeExecutor::HostResults& Result, std::error_code Error)
{
    auto& Collected = *static_cast<FCollectedFrames*>(User);
    FScopeLock Lock(&Collected.Lock);
    if (Error)
    {
        Collected.Error = Error;
        return;
    }
    if (Result.weights.Size() != static_cast<size_t>(Collected.WeightCount))
    {
        Collected.bInvalidWeights = true;
        return;
    }
    FAudio2BlendshapeFrame& Frame = Collected.Frames.AddDefaulted_GetRef();
    Frame.TimeSeconds = static_cast<double>(Result.timeStampCurrentFrame) / SolverRate;
    Frame.Weights.Append(Result.weights.Data(), Collected.WeightCount);
}
}

IMPLEMENT_MODULE(FAudio2BlendshapesModule, Audio2Blendshapes)

bool SolveBlendshapes(TArray<float> Samples, int32 Rate, int32 Channels, const FString& ModelPath,
    FAudio2BlendshapeClip& Clip, FString& Error)
{
    if (Rate < 1000 || Rate > 384000 || Channels < 1 || Channels > 32 ||
        Samples.IsEmpty() || Samples.Num() % Channels != 0)
    {
        Error = TEXT("Provide non-empty interleaved PCM, a valid sample rate, and a channel count.");
        return false;
    }

    const int32 FrameCount = Samples.Num() / Channels;
    const double Duration = static_cast<double>(FrameCount) / Rate;
    if (Duration * SolverRate >= MAX_int32)
    {
        Error = TEXT("The resampled audio would exceed Unreal's array size limit.");
        return false;
    }
    for (int32 Frame = 0; Frame < FrameCount; ++Frame)
    {
        double Sum = 0;
        for (int32 Channel = 0; Channel < Channels; ++Channel)
        {
            const float Sample = Samples[Frame * Channels + Channel];
            if (!FMath::IsFinite(Sample))
            {
                Error = TEXT("PCM samples must be finite.");
                return false;
            }
            Sum += FMath::Clamp(Sample, -1.0f, 1.0f);
        }
        Samples[Frame] = static_cast<float>(Sum / Channels);
    }
    Samples.SetNum(FrameCount, EAllowShrinking::No);

    if (Rate != SolverRate)
    {
        TUniquePtr<Audio::ISampleRateConverter> Converter(Audio::ISampleRateConverter::CreateSampleRateConverter());
        Converter->Init(static_cast<float>(Rate) / SolverRate, 1);
        TArray<float> Resampled;
        Converter->ProcessFullbuffer(Samples.GetData(), Samples.Num(), Resampled);
        Samples = MoveTemp(Resampled);
    }
    if (Samples.IsEmpty())
    {
        Error = TEXT("The audio is too short to resample.");
        return false;
    }

    const FString SdkRoot = EnvironmentOrDefault(TEXT("AUDIO2FACE_SDK_ROOT"), TEXT(A2B_SDK_ROOT));
    const FString Model = FPaths::ConvertRelativePathToFull(ModelPath.IsEmpty()
        ? SdkRoot / TEXT("_data/audio2face-models/audio2face-3d-v2.3.1-claire/model.json") : ModelPath);
    if (!FPaths::FileExists(Model))
    {
        Error = FString::Printf(TEXT("Audio2Face model file does not exist: %s"), *Model);
        return false;
    }

    FScopeLock SolverGuard(&SolverLock);
    auto* Module = FModuleManager::GetModulePtr<FAudio2BlendshapesModule>(TEXT("Audio2Blendshapes"));
    if (!Module)
    {
        Error = TEXT("Audio2Blendshapes module is unavailable.");
        return false;
    }
    if (!Module->LoadRuntime(SdkRoot, Error))
    {
        return false;
    }

    auto Check = [&Error](std::error_code Code, const TCHAR* Operation)
    {
        if (!Code)
        {
            return true;
        }
        Error = FString::Printf(TEXT("%s: %s"), Operation, UTF8_TO_TCHAR(Code.message().c_str()));
        return false;
    };

    FCollectedFrames Collected;
    auto Destroy = [](nva2f::IBlendshapeExecutorBundle* Bundle) { if (Bundle) Bundle->Destroy(); };
    TUniquePtr<nva2f::IBlendshapeExecutorBundle, decltype(Destroy)> Bundle(
        nva2f::ReadRegressionBlendshapeSolveExecutorBundle(1, TCHAR_TO_UTF8(*Model),
            nva2f::IGeometryExecutor::ExecutionOption::SkinTongue, false, SolverFrameRate, 1, nullptr, nullptr),
        Destroy);
    if (!Bundle)
    {
        Error = FString::Printf(TEXT("Could not initialize the regression model %s. Check network.trt and the editor log."), *Model);
        return false;
    }

    auto& Executor = Bundle->GetExecutor();
    nva2f::IBlendshapeSolver* Skin = nullptr;
    nva2f::IBlendshapeSolver* Tongue = nullptr;
    if (!Check(nva2f::GetExecutorSkinSolver(Executor, 0, &Skin), TEXT("Reading skin curves")) ||
        !Check(nva2f::GetExecutorTongueSolver(Executor, 0, &Tongue), TEXT("Reading tongue curves")))
    {
        return false;
    }
    for (auto* Solver : {Skin, Tongue})
    {
        if (Solver)
        {
            for (int32 Index = 0; Index < Solver->NumBlendshapePoses(); ++Index)
            {
                Clip.CurveNames.Emplace(UTF8_TO_TCHAR(Solver->GetPoseName(Index)));
            }
        }
    }
    Collected.WeightCount = Clip.CurveNames.Num();
    if (Collected.WeightCount == 0 || Executor.GetWeightCount() != static_cast<size_t>(Collected.WeightCount))
    {
        Error = TEXT("The model's curve names do not match the solver's output weights.");
        return false;
    }

    auto& Audio = Bundle->GetAudioAccumulator(0);
    auto& Emotion = Bundle->GetEmotionAccumulator(0);
    TArray<float> Neutral;
    Neutral.SetNumZeroed(static_cast<int32>(Emotion.GetEmotionSize()));
    auto Stream = Bundle->GetCudaStream().Data();
    if (!Check(Emotion.Accumulate(0, nva2x::HostTensorFloatConstView{Neutral.GetData(),
            static_cast<size_t>(Neutral.Num())}, Stream), TEXT("Submitting neutral emotion")) ||
        !Check(Emotion.Close(), TEXT("Closing emotion input")) ||
        !Check(Audio.Accumulate(nva2x::HostTensorFloatConstView{Samples.GetData(),
            static_cast<size_t>(Samples.Num())}, Stream), TEXT("Submitting PCM")) ||
        !Check(Audio.Close(), TEXT("Closing PCM input")) ||
        !Check(Executor.SetResultsCallback(&ReceiveFrame, &Collected), TEXT("Registering frame callback")))
    {
        return false;
    }
    while (nva2x::GetNbReadyTracks(Executor) > 0)
    {
        if (!Check(Executor.Execute(nullptr), TEXT("Running Audio2Face")))
        {
            Executor.Wait(0);
            return false;
        }
    }
    if (!Check(Executor.Wait(0), TEXT("Waiting for Audio2Face")) ||
        !Check(Collected.Error, TEXT("Collecting Audio2Face frames")))
    {
        return false;
    }
    if (Collected.bInvalidWeights || Collected.Frames.IsEmpty())
    {
        Error = TEXT("Audio2Face returned no usable blendshape frames.");
        return false;
    }

    Collected.Frames.Sort([](const auto& A, const auto& B) { return A.TimeSeconds < B.TimeSeconds; });
    Clip.Frames = MoveTemp(Collected.Frames);
    Clip.AudioDurationSeconds = Duration;
    Clip.FrameRate = SolverFrameRate;
    return true;
}

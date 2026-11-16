#pragma once

#include "Audio2BlendshapesLibrary.h"

bool SolveBlendshapes(TArray<float> Samples, int32 Rate, int32 Channels, const FString& ModelPath,
    FAudio2BlendshapeClip& Clip, FString& Error);

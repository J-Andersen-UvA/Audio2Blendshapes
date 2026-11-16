#pragma once

#include "CoreMinimal.h"

bool DecodeAudioFile(const FString& Path, TArray<float>& Samples, int32& Rate, int32& Channels, FString& Error);

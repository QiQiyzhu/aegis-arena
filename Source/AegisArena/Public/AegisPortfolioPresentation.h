#pragma once
#include "CoreMinimal.h"
class AActor;
class UObject;
namespace AegisPortfolioPresentation
{
bool Enabled();
bool V2Enabled();
bool V23Enabled();
void BuildArena(AActor* Owner);
void Sound(UObject* Context, const TCHAR* Name, const FVector& Location, float Volume = 1.f);
} // namespace AegisPortfolioPresentation

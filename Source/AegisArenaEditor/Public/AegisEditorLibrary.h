#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AegisEditorLibrary.generated.h"

/** Editor-only reproducible asset authoring. No authored package is overwritten. */
UCLASS()
class AEGISARENAEDITOR_API UAegisEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
  public:
    UFUNCTION(BlueprintCallable, Category = "Aegis|Authoring")
    static bool BuildAIAssets();
    UFUNCTION(BlueprintCallable, Category = "Aegis|Authoring")
    static bool AddNavigationBounds(UWorld* World);
    /** Uses public native APIs; Python cannot read EQS internal Options/Generator properties. */
    UFUNCTION(BlueprintPure, Category = "Aegis|Inspection")
    static FString DescribeQuery(class UEnvQuery* Query);
};

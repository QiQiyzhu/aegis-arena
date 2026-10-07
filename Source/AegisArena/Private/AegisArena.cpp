#include "Modules/ModuleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

class FAegisGameModule final : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();
        // The distributable always opens the current game. Development tools
        // keep their explicit historical profiles for reproducible evaluation.
#if UE_BUILD_SHIPPING
        FCommandLine::Append(TEXT(" -AegisPortfolio -AegisV2 -AegisV23"));
#else
        if (FParse::Param(FCommandLine::Get(), TEXT("AegisRelease")))
            FCommandLine::Append(TEXT(" -AegisPortfolio -AegisV2 -AegisV23"));
#endif
    }
};
IMPLEMENT_PRIMARY_GAME_MODULE(FAegisGameModule, AegisArena, "AegisArena");

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Module for the BikeConnect plugin. Owns process-wide WinRT/COM apartment
 * initialization on the game thread so the subsystem doesn't need to worry about it.
 */
class FBikeConnectModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool bComInitialized = false;
};

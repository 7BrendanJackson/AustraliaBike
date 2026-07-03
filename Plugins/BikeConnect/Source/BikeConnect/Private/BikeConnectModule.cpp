#include "BikeConnectModule.h"
#include "BikeConnectLog.h"

#if WITH_BIKECONNECT_WINRT
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <roapi.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

DEFINE_LOG_CATEGORY(LogBikeConnect);

void FBikeConnectModule::StartupModule()
{
#if WITH_BIKECONNECT_WINRT
	// Initialize the WinRT/COM apartment for the game thread. Individual background
	// threadpool threads used to deliver WinRT events are initialized by the OS itself.
	const HRESULT Hr = RoInitialize(RO_INIT_MULTITHREADED);
	// RPC_E_CHANGED_MODE means something else already initialized COM on this thread with
	// a different concurrency model - not fatal, WinRT activation will still work.
	bComInitialized = SUCCEEDED(Hr) || Hr == RPC_E_CHANGED_MODE;
	if (!bComInitialized)
	{
		UE_LOG(LogBikeConnect, Warning, TEXT("BikeConnect: RoInitialize failed (0x%08x). Bike connectivity will be unavailable."), Hr);
	}
#endif
}

void FBikeConnectModule::ShutdownModule()
{
#if WITH_BIKECONNECT_WINRT
	if (bComInitialized)
	{
		RoUninitialize();
		bComInitialized = false;
	}
#endif
}

IMPLEMENT_MODULE(FBikeConnectModule, BikeConnect)

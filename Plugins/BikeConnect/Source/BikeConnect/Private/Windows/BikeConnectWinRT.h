#pragma once

#include "CoreMinimal.h"
#include "BikeCSCTypes.h"

#if WITH_BIKECONNECT_WINRT

class UBikeConnectSubsystem;

/**
 * Platform backend that talks to Windows Bluetooth LE via the native WinRT COM ABI
 * (RoGetActivationFactory / ComPtr), so no extra packages need to be downloaded -
 * everything used here ships with the Windows SDK.
 *
 * Deliberately kept free of any WinRT/COM types in this header so that including it
 * from UObject headers never drags Windows.h / COM interfaces into the rest of the module.
 * The actual COM interface pointers live behind an opaque Impl in the .cpp file.
 */
class FBikeConnectWinRTImpl
{
public:
	FBikeConnectWinRTImpl(UBikeConnectSubsystem& InOwner);
	~FBikeConnectWinRTImpl();

	/** Begins continuous advertisement scanning for the CSC service UUID. Idempotent. */
	void StartScanning();

	/** Stops scanning and tears down any active connection. Idempotent. */
	void StopScanning();

	// Public so the free helper functions in BikeConnectWinRT.cpp (which do the actual COM
	// work outside of member-function scope, so lambdas can capture references cleanly) can
	// name this type. It is still only ever defined in that .cpp - nothing COM-related leaks
	// into this header.
	struct FComImpl;

private:
	TSharedPtr<FComImpl, ESPMode::ThreadSafe> Com;

	UBikeConnectSubsystem& Owner;
};

#endif // WITH_BIKECONNECT_WINRT

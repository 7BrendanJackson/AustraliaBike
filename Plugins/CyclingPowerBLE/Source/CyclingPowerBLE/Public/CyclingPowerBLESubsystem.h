#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CyclingPowerBLETypes.h"
#include "CyclingPowerBLESubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCyclingPowerDataReceived, const FCyclingPowerData&, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCyclingPowerConnectionStateChanged, ECyclingPowerConnectionState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCyclingPowerDeviceFound, const FString&, DeviceAddress);

class FCyclingPowerBLEImpl;

/**
 * Game Instance Subsystem that scans for, connects to, and streams data from
 * a Bluetooth LE Cycling Power Service device (e.g. a smart trainer or power
 * meter pedal/crank). Available from anywhere via:
 *
 *   UCyclingPowerBLESubsystem* BLE = UCyclingPowerBLESubsystem::Get(this);
 *
 * or from any object with a GameInstance:
 *
 *   GetGameInstance()->GetSubsystem<UCyclingPowerBLESubsystem>();
 *
 * Currently implemented for Windows (WinRT / Windows.Devices.Bluetooth).
 * On other platforms, calls log a warning and are no-ops.
 */
UCLASS()
class CYCLINGPOWERBLE_API UCyclingPowerBLESubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Convenience getter usable from any Actor/Pawn/Widget etc. */
	UFUNCTION(BlueprintPure, Category = "Cycling Power", meta = (WorldContext = "WorldContextObject", DisplayName = "Get Cycling Power BLE Subsystem"))
	static UCyclingPowerBLESubsystem* Get(const UObject* WorldContextObject);

	/** Begin scanning for a Cycling Power Service device. Auto-connects when one is found, and auto-resumes scanning if it later disconnects. */
	UFUNCTION(BlueprintCallable, Category = "Cycling Power")
	void StartScanning();

	/** Stop scanning (does not disconnect an already-connected device). */
	UFUNCTION(BlueprintCallable, Category = "Cycling Power")
	void StopScanning();

	/** Disconnect the current device, if any, and stop scanning. */
	UFUNCTION(BlueprintCallable, Category = "Cycling Power")
	void Disconnect();

	UFUNCTION(BlueprintPure, Category = "Cycling Power")
	ECyclingPowerConnectionState GetConnectionState() const { return ConnectionState; }

	UFUNCTION(BlueprintPure, Category = "Cycling Power")
	FCyclingPowerData GetLastData() const { return LastData; }

	/** Broadcast whenever a new Cycling Power Measurement notification arrives. */
	UPROPERTY(BlueprintAssignable, Category = "Cycling Power")
	FOnCyclingPowerDataReceived OnDataReceived;

	/** Broadcast whenever the connection state changes. */
	UPROPERTY(BlueprintAssignable, Category = "Cycling Power")
	FOnCyclingPowerConnectionStateChanged OnConnectionStateChanged;

	/** Broadcast when a matching advertisement is first seen, before connection completes. */
	UPROPERTY(BlueprintAssignable, Category = "Cycling Power")
	FOnCyclingPowerDeviceFound OnDeviceFound;

private:
	friend class FCyclingPowerBLEImpl;
	void SetConnectionState(ECyclingPowerConnectionState NewState);

	UPROPERTY()
	FCyclingPowerData LastData;

	ECyclingPowerConnectionState ConnectionState = ECyclingPowerConnectionState::Disconnected;

	// Pimpl: keeps WinRT/Windows headers entirely out of this public header.
	FCyclingPowerBLEImpl* Impl = nullptr;
};

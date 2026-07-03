#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BikeCSCTypes.h"
#include "BikeConnectSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBikeConnectedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBikeDisconnectedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBikeDataUpdatedSignature, const FBikeCSCData&, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBikeBluetoothErrorSignature, EBikeBluetoothError, ErrorType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBikeConnectionStateChangedSignature, EBikeConnectionState, NewState);

#if WITH_BIKECONNECT_WINRT
class FBikeConnectWinRTImpl;
#endif

/**
 * Central place that owns the single Bluetooth LE connection to the exercise bike.
 *
 * There is only ever one physical bike connection, managed here. BikeSensorComponents
 * on any number of Actors simply subscribe to this subsystem's events, so multiple
 * components never fight over the radio.
 *
 * The subsystem continuously scans for a CSC advertisement whenever it is not connected
 * (since the bike only powers on while being pedalled), auto-connects when found, and
 * automatically resumes scanning if the bike disconnects/powers off.
 */
UCLASS()
class BIKECONNECT_API UBikeConnectSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual ~UBikeConnectSubsystem() override;

	/** Fired the moment a GATT connection + CSC notification subscription succeeds. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FOnBikeConnectedSignature OnBikeConnected;

	/** Fired when the bike disconnects (e.g. powered off) or the link is lost. Scanning resumes automatically. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FOnBikeDisconnectedSignature OnBikeDisconnected;

	/** Fired every time a new CSC measurement notification is decoded. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FOnBikeDataUpdatedSignature OnBikeDataUpdated;

	/** Fired on Bluetooth related errors, in particular when the adapter is turned off. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FOnBikeBluetoothErrorSignature OnBikeBluetoothError;

	/** Fired whenever the coarse connection state changes (useful for UI). */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FOnBikeConnectionStateChangedSignature OnBikeConnectionStateChanged;

	/** Wheel circumference used to convert wheel-revolution deltas into speed. Default is a common 700x25c road wheel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bike Connect", meta = (ClampMin = "100.0", UIMin = "100.0", UIMax = "3000.0"))
	float WheelCircumferenceMm = 2105.f;

	/** If true (default) scanning starts automatically when the subsystem initializes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bike Connect")
	bool bAutoStart = true;

	/** Starts (or resumes) continuous scanning / auto-connect. Safe to call if already scanning or connected. */
	UFUNCTION(BlueprintCallable, Category = "Bike Connect")
	void StartScanning();

	/** Stops scanning and disconnects if currently connected. */
	UFUNCTION(BlueprintCallable, Category = "Bike Connect")
	void StopScanning();

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	EBikeConnectionState GetConnectionState() const { return ConnectionState; }

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	bool IsBikeConnected() const { return ConnectionState == EBikeConnectionState::Connected; }

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	FBikeCSCData GetLatestData() const { return LatestData; }

	/** True once we've confirmed the adapter is off - useful to show a persistent "turn Bluetooth on" prompt. */
	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	bool IsBluetoothRadioOff() const { return bRadioReportedOff; }

	// --- Called by the platform backend (declared public so free helper functions in the
	// platform .cpp can call them; they carry no UFUNCTION so are invisible to Blueprint).
	// May arrive on a background COM/WinRT thread - each of these hops to the game thread
	// before touching UObject state or broadcasting a delegate. Not intended to be called
	// from game code; use StartScanning/StopScanning instead. ---
	void HandlePlatformConnecting();
	void HandlePlatformConnected();
	void HandlePlatformDisconnected();
	void HandlePlatformDataReceived(const FBikeCSCData& RawSample);
	void HandlePlatformError(EBikeBluetoothError ErrorType);

private:
	void SetConnectionState(EBikeConnectionState NewState);
	void CheckForStaleData();

	FTimerHandle StaleDataTimerHandle;
	static constexpr float StaleDataTimeoutSeconds = 1.0f;

	UPROPERTY(Transient)
	FBikeCSCData LatestData;

	EBikeConnectionState ConnectionState = EBikeConnectionState::Disconnected;
	bool bRadioReportedOff = false;
	bool bWantsScanning = false;

	// Previous raw samples, used to compute speed/cadence deltas (rollover-safe). Game thread only.
	bool bHasPreviousWheelSample = false;
	int64 PrevWheelRevolutions = 0;
	uint16 PrevWheelEventTime = 0;

	bool bHasPreviousCrankSample = false;
	uint16 PrevCrankRevolutions = 0;
	uint16 PrevCrankEventTime = 0;

	FBikeCSCData ComputeDerivedSample(const FBikeCSCData& RawSample);

#if WITH_BIKECONNECT_WINRT
	TUniquePtr<FBikeConnectWinRTImpl> PlatformImpl;

	friend class FBikeConnectWinRTImpl;
#endif
};

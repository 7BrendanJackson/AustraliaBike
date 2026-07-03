#pragma once

#include "CoreMinimal.h"
#include "BikeCSCTypes.generated.h"

/** High level connection state of the bike link. */
UENUM(BlueprintType)
enum class EBikeConnectionState : uint8
{
	Disconnected	UMETA(DisplayName = "Disconnected"),
	Scanning		UMETA(DisplayName = "Scanning"),
	Connecting		UMETA(DisplayName = "Connecting"),
	Connected		UMETA(DisplayName = "Connected")
};

/** Reasons a bike connection can fail, surfaced to Blueprint so the user can be notified. */
UENUM(BlueprintType)
enum class EBikeBluetoothError : uint8
{
	None				UMETA(DisplayName = "None"),
	BluetoothRadioOff	UMETA(DisplayName = "Bluetooth Is Turned Off"),
	RadioUnavailable	UMETA(DisplayName = "No Bluetooth Radio Found"),
	ConnectionFailed	UMETA(DisplayName = "Connection Failed"),
	ServiceNotFound		UMETA(DisplayName = "CSC Service Not Found On Device"),
	ConnectionLost		UMETA(DisplayName = "Connection Lost")
};

/**
 * A single decoded sample from the Cycling Speed and Cadence (CSC) GATT characteristic,
 * plus the derived speed / cadence values computed from consecutive samples.
 */
USTRUCT(BlueprintType)
struct BIKECONNECT_API FBikeCSCData
{
	GENERATED_BODY()

	/** Whether this sample contained wheel revolution data. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	bool bHasWheelData = false;

	/** Whether this sample contained crank revolution data. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	bool bHasCrankData = false;

	/** Raw cumulative wheel revolution count reported by the sensor (wraps at 2^32). */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	int64 CumulativeWheelRevolutions = 0;

	/** Raw cumulative crank revolution count reported by the sensor (wraps at 2^16). */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	int32 CumulativeCrankRevolutions = 0;

	/** Device-clock timestamp of the last wheel event, in units of 1/1024 second (wraps at 2^16). Used internally to derive speed. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	int32 LastWheelEventTime = 0;

	/** Device-clock timestamp of the last crank event, in units of 1/1024 second (wraps at 2^16). Used internally to derive cadence. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	int32 LastCrankEventTime = 0;

	/** Instantaneous speed derived from the last two wheel samples, in km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	float SpeedKph = 0.f;

	/** Instantaneous cadence derived from the last two crank samples, in RPM. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	float CadenceRpm = 0.f;

	/** Server-side timestamp (seconds since app start) this sample was processed on the game thread. */
	UPROPERTY(BlueprintReadOnly, Category = "Bike Connect")
	float TimestampSeconds = 0.f;
};

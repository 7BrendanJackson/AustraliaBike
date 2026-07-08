#pragma once

#include "CoreMinimal.h"
#include "CyclingPowerBLETypes.generated.h"

UENUM(BlueprintType)
enum class ECyclingPowerConnectionState : uint8
{
	Disconnected,
	Scanning,
	Connecting,
	Connected
};

USTRUCT(BlueprintType)
struct FCyclingPowerData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	int32 InstantaneousPower = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	bool bHasCadence = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	float CadenceRPM = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	bool bHasBalance = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	float PedalPowerBalancePercent = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	bool bHasTorque = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	float AccumulatedTorqueNm = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	bool bHasWheelRPM = false;

	UPROPERTY(BlueprintReadOnly, Category = "Cycling Power")
	float WheelRPM = 0.f;
};

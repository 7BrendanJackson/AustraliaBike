#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BikeCSCTypes.h"
#include "BikeSensorComponent.generated.h"

class UBikeConnectSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBikeSensor_OnConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBikeSensor_OnDisconnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBikeSensor_OnDataUpdated, const FBikeCSCData&, Data);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBikeSensor_OnBluetoothError, EBikeBluetoothError, ErrorType);

/**
 * Drop this component on any Actor or Pawn to get access to the live exercise bike data.
 *
 * All instances share the single underlying bike connection owned by UBikeConnectSubsystem,
 * so it's safe to add this to as many actors as you like (e.g. the player pawn and a HUD actor).
 */
UCLASS(ClassGroup = (BikeConnect), meta = (BlueprintSpawnableComponent))
class BIKECONNECT_API UBikeSensorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBikeSensorComponent();

	/** Fired when the bike becomes connected and ready to report data. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FBikeSensor_OnConnected OnBikeConnected;

	/** Fired when the bike disconnects or powers off. The plugin keeps scanning automatically. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FBikeSensor_OnDisconnected OnBikeDisconnected;

	/** Fired every time a new sample arrives from the bike, with speed/cadence already computed. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FBikeSensor_OnDataUpdated OnBikeDataUpdated;

	/** Fired on Bluetooth errors - in particular when the adapter is switched off - so you can notify the user. */
	UPROPERTY(BlueprintAssignable, Category = "Bike Connect")
	FBikeSensor_OnBluetoothError OnBluetoothError;

	/** If true, this component asks the subsystem to start scanning as soon as it begins play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bike Connect")
	bool bAutoStartScanningOnBeginPlay = true;

	UFUNCTION(BlueprintCallable, Category = "Bike Connect")
	void StartScanning();

	UFUNCTION(BlueprintCallable, Category = "Bike Connect")
	void StopScanning();

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	bool IsBikeConnected() const;

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	EBikeConnectionState GetConnectionState() const;

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	FBikeCSCData GetLatestData() const;

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	float GetSpeedKph() const;

	UFUNCTION(BlueprintPure, Category = "Bike Connect")
	float GetCadenceRpm() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UBikeConnectSubsystem> Subsystem = nullptr;

	UFUNCTION()
	void HandleSubsystemConnected();

	UFUNCTION()
	void HandleSubsystemDisconnected();

	UFUNCTION()
	void HandleSubsystemDataUpdated(const FBikeCSCData& Data);

	UFUNCTION()
	void HandleSubsystemError(EBikeBluetoothError ErrorType);
};

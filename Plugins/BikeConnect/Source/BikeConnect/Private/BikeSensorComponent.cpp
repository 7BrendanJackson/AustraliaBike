#include "BikeSensorComponent.h"
#include "BikeConnectSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UBikeSensorComponent::UBikeSensorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UBikeSensorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			Subsystem = GameInstance->GetSubsystem<UBikeConnectSubsystem>();
		}
	}

	if (Subsystem)
	{
		Subsystem->OnBikeConnected.AddDynamic(this, &UBikeSensorComponent::HandleSubsystemConnected);
		Subsystem->OnBikeDisconnected.AddDynamic(this, &UBikeSensorComponent::HandleSubsystemDisconnected);
		Subsystem->OnBikeDataUpdated.AddDynamic(this, &UBikeSensorComponent::HandleSubsystemDataUpdated);
		Subsystem->OnBikeBluetoothError.AddDynamic(this, &UBikeSensorComponent::HandleSubsystemError);

		if (bAutoStartScanningOnBeginPlay)
		{
			Subsystem->StartScanning();
		}

		if (Subsystem->IsBikeConnected())
		{
			HandleSubsystemConnected();
		}
	}
}

void UBikeSensorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Subsystem)
	{
		Subsystem->OnBikeConnected.RemoveDynamic(this, &UBikeSensorComponent::HandleSubsystemConnected);
		Subsystem->OnBikeDisconnected.RemoveDynamic(this, &UBikeSensorComponent::HandleSubsystemDisconnected);
		Subsystem->OnBikeDataUpdated.RemoveDynamic(this, &UBikeSensorComponent::HandleSubsystemDataUpdated);
		Subsystem->OnBikeBluetoothError.RemoveDynamic(this, &UBikeSensorComponent::HandleSubsystemError);
	}
	Subsystem = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UBikeSensorComponent::StartScanning()
{
	if (Subsystem)
	{
		Subsystem->StartScanning();
	}
}

void UBikeSensorComponent::StopScanning()
{
	if (Subsystem)
	{
		Subsystem->StopScanning();
	}
}

bool UBikeSensorComponent::IsBikeConnected() const
{
	return Subsystem && Subsystem->IsBikeConnected();
}

EBikeConnectionState UBikeSensorComponent::GetConnectionState() const
{
	return Subsystem ? Subsystem->GetConnectionState() : EBikeConnectionState::Disconnected;
}

FBikeCSCData UBikeSensorComponent::GetLatestData() const
{
	return Subsystem ? Subsystem->GetLatestData() : FBikeCSCData();
}

float UBikeSensorComponent::GetSpeedKph() const
{
	return Subsystem ? Subsystem->GetLatestData().SpeedKph : 0.f;
}

float UBikeSensorComponent::GetCadenceRpm() const
{
	return Subsystem ? Subsystem->GetLatestData().CadenceRpm : 0.f;
}

void UBikeSensorComponent::HandleSubsystemConnected()
{
	OnBikeConnected.Broadcast();
}

void UBikeSensorComponent::HandleSubsystemDisconnected()
{
	OnBikeDisconnected.Broadcast();
}

void UBikeSensorComponent::HandleSubsystemDataUpdated(const FBikeCSCData& Data)
{
	OnBikeDataUpdated.Broadcast(Data);
}

void UBikeSensorComponent::HandleSubsystemError(EBikeBluetoothError ErrorType)
{
	OnBluetoothError.Broadcast(ErrorType);
}

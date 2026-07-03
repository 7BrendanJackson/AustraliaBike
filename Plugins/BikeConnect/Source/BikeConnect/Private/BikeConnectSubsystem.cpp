#include "BikeConnectSubsystem.h"
#include "BikeConnectLog.h"
#include "Async/Async.h"
#include "TimerManager.h"
#include "Engine/World.h"

#if WITH_BIKECONNECT_WINRT
#include "Windows/BikeConnectWinRT.h"
#endif

void UBikeConnectSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

#if WITH_BIKECONNECT_WINRT
	PlatformImpl = MakeUnique<FBikeConnectWinRTImpl>(*this);
#else
	UE_LOG(LogBikeConnect, Warning, TEXT("BikeConnect: no platform backend available on this platform - bike connectivity is disabled."));
#endif

	if (bAutoStart)
	{
		StartScanning();
	}
}

void UBikeConnectSubsystem::Deinitialize()
{
	StopScanning();

#if WITH_BIKECONNECT_WINRT
	PlatformImpl.Reset();
#endif

	Super::Deinitialize();
}

UBikeConnectSubsystem::~UBikeConnectSubsystem()
{
	// Defined here (rather than defaulted in the header) so that on Windows, TUniquePtr's
	// destructor for PlatformImpl runs where FBikeConnectWinRTImpl is a complete type.
}

void UBikeConnectSubsystem::StartScanning()
{
	bWantsScanning = true;

#if WITH_BIKECONNECT_WINRT
	if (PlatformImpl.IsValid())
	{
		SetConnectionState(EBikeConnectionState::Scanning);
		PlatformImpl->StartScanning();
	}
#else
	HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
#endif
}

void UBikeConnectSubsystem::StopScanning()
{
	bWantsScanning = false;

#if WITH_BIKECONNECT_WINRT
	if (PlatformImpl.IsValid())
	{
		PlatformImpl->StopScanning();
	}
#endif

	SetConnectionState(EBikeConnectionState::Disconnected);
}

void UBikeConnectSubsystem::SetConnectionState(EBikeConnectionState NewState)
{
	if (ConnectionState == NewState)
	{
		return;
	}
	ConnectionState = NewState;
	OnBikeConnectionStateChanged.Broadcast(ConnectionState);
}

void UBikeConnectSubsystem::HandlePlatformConnecting()
{
	TWeakObjectPtr<UBikeConnectSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (UBikeConnectSubsystem* Subsystem = WeakThis.Get())
		{
			Subsystem->SetConnectionState(EBikeConnectionState::Connecting);
		}
	});
}

void UBikeConnectSubsystem::HandlePlatformConnected()
{
	TWeakObjectPtr<UBikeConnectSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (UBikeConnectSubsystem* Subsystem = WeakThis.Get())
		{
			// Fresh connection: forget any stale delta-tracking state from a previous session.
			Subsystem->bHasPreviousWheelSample = false;
			Subsystem->bHasPreviousCrankSample = false;

			Subsystem->SetConnectionState(EBikeConnectionState::Connected);
			if (UWorld* World = Subsystem->GetWorld())
			{
				World->GetTimerManager().SetTimer(
					Subsystem->StaleDataTimerHandle,
					Subsystem, &UBikeConnectSubsystem::CheckForStaleData,
					StaleDataTimeoutSeconds, false);
			}
			Subsystem->OnBikeConnected.Broadcast();
		}
	});
}

void UBikeConnectSubsystem::HandlePlatformDisconnected()
{
	TWeakObjectPtr<UBikeConnectSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis]()
	{
		if (UBikeConnectSubsystem* Subsystem = WeakThis.Get())
		{
			Subsystem->bHasPreviousWheelSample = false;
			Subsystem->bHasPreviousCrankSample = false;
			if (UWorld* World = Subsystem->GetWorld())
			{
				World->GetTimerManager().ClearTimer(Subsystem->StaleDataTimerHandle);
			}
			Subsystem->OnBikeDisconnected.Broadcast();

			// The bike only powers on while pedalling, so losing the connection is the
			// expected steady state between sessions - go back to (or stay in) Scanning
			// rather than Disconnected, as long as the caller still wants us scanning.
			Subsystem->SetConnectionState(Subsystem->bWantsScanning ? EBikeConnectionState::Scanning : EBikeConnectionState::Disconnected);
		}
	});
}

void UBikeConnectSubsystem::HandlePlatformDataReceived(const FBikeCSCData& RawSample)
{
	TWeakObjectPtr<UBikeConnectSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, RawSample]()
	{
		if (UBikeConnectSubsystem* Subsystem = WeakThis.Get())
		{
			const FBikeCSCData Derived = Subsystem->ComputeDerivedSample(RawSample);
			Subsystem->LatestData = Derived;
			if (UWorld* World = Subsystem->GetWorld())
			{
				World->GetTimerManager().SetTimer(
					Subsystem->StaleDataTimerHandle,
					Subsystem, &UBikeConnectSubsystem::CheckForStaleData,
					StaleDataTimeoutSeconds, false);
			}
			Subsystem->OnBikeDataUpdated.Broadcast(Derived);
		}
	});
}

void UBikeConnectSubsystem::HandlePlatformError(EBikeBluetoothError ErrorType)
{
	TWeakObjectPtr<UBikeConnectSubsystem> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, ErrorType]()
	{
		if (UBikeConnectSubsystem* Subsystem = WeakThis.Get())
		{
			Subsystem->bRadioReportedOff = (ErrorType == EBikeBluetoothError::BluetoothRadioOff);
			Subsystem->OnBikeBluetoothError.Broadcast(ErrorType);
		}
	});
}

void UBikeConnectSubsystem::CheckForStaleData()
{
	if (LatestData.SpeedKph != 0.f || LatestData.CadenceRpm != 0.f)
	{
		LatestData.SpeedKph = 0.f;
		LatestData.CadenceRpm = 0.f;
		bHasPreviousWheelSample = false;
		bHasPreviousCrankSample = false;
		OnBikeDataUpdated.Broadcast(LatestData);
	}
}

FBikeCSCData UBikeConnectSubsystem::ComputeDerivedSample(const FBikeCSCData& RawSample)
{
	FBikeCSCData Result = RawSample;
	Result.TimestampSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	// CSC event timestamps are a 16-bit counter in units of 1/1024 second that wraps around;
	// revolution counters also wrap (wheel at 2^32, crank at 2^16). All deltas below are
	// computed with unsigned wraparound arithmetic so a rollover doesn't produce a spike.
	if (RawSample.bHasWheelData)
	{
		if (bHasPreviousWheelSample)
		{
			const uint32 CurrRevs = static_cast<uint32>(RawSample.CumulativeWheelRevolutions);
			const uint32 PrevRevs = static_cast<uint32>(PrevWheelRevolutions);
			const uint32 DeltaRevs = CurrRevs - PrevRevs; // wraps correctly for uint32

			const uint16 DeltaTimeTicks = static_cast<uint16>(RawSample.LastWheelEventTime) - PrevWheelEventTime; // wraps correctly for uint16
			const float DeltaTimeSeconds = DeltaTimeTicks / 1024.f;

			if (DeltaTimeSeconds > 0.f && DeltaRevs < 100000u) // sanity guard against garbage/duplicate packets
			{
				const float DistanceMeters = (DeltaRevs * WheelCircumferenceMm) / 1000.f;
				const float MetersPerSecond = DistanceMeters / DeltaTimeSeconds;
				Result.SpeedKph = MetersPerSecond * 3.6f;
			}
			else if (DeltaTimeSeconds <= 0.f)
			{
				// The wheel event time hasn't advanced since the last notification, which per the
				// CSC spec means no new revolution has been recorded - the bike keeps sending periodic
				// notifications even while stationary, but this field only changes when the wheel
				// actually turns. Report zero rather than holding the last speed, or the rider would
				// appear to keep coasting forever after they actually stop.
				Result.SpeedKph = 0.f;
			}
		}

		PrevWheelRevolutions = RawSample.CumulativeWheelRevolutions;
		PrevWheelEventTime = static_cast<uint16>(RawSample.LastWheelEventTime);
		bHasPreviousWheelSample = true;
	}
	else
	{
		Result.SpeedKph = LatestData.SpeedKph;
	}

	if (RawSample.bHasCrankData)
	{
		if (bHasPreviousCrankSample)
		{
			const uint16 DeltaRevs = static_cast<uint16>(RawSample.CumulativeCrankRevolutions) - PrevCrankRevolutions; // wraps correctly
			const uint16 DeltaTimeTicks = static_cast<uint16>(RawSample.LastCrankEventTime) - PrevCrankEventTime; // wraps correctly
			const float DeltaTimeSeconds = DeltaTimeTicks / 1024.f;

			if (DeltaTimeSeconds > 0.f && DeltaRevs < 2000)
			{
				Result.CadenceRpm = (DeltaRevs / DeltaTimeSeconds) * 60.f;
			}
			else if (DeltaTimeSeconds <= 0.f)
			{
				// Same reasoning as the wheel case above: an unchanged crank event time means no new
				// crank revolution, i.e. the rider has stopped pedaling - report zero, not the last cadence.
				Result.CadenceRpm = 0.f;
			}
		}

		PrevCrankRevolutions = static_cast<uint16>(RawSample.CumulativeCrankRevolutions);
		PrevCrankEventTime = static_cast<uint16>(RawSample.LastCrankEventTime);
		bHasPreviousCrankSample = true;
	}
	else
	{
		Result.CadenceRpm = LatestData.CadenceRpm;
	}

	return Result;
}

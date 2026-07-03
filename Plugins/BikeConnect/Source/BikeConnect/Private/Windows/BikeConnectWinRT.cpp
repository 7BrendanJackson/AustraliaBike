#include "Windows/BikeConnectWinRT.h"

#if WITH_BIKECONNECT_WINRT

#include "BikeConnectSubsystem.h"
#include "Async/Async.h"
#include "HAL/CriticalSection.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/AllowWindowsPlatformAtomics.h"

THIRD_PARTY_INCLUDES_START
#include <roapi.h>
#include <wrl/client.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>
#include <windows.devices.bluetooth.h>
#include <windows.devices.bluetooth.advertisement.h>
#include <windows.devices.bluetooth.genericattributeprofile.h>
#include <windows.devices.radios.h>
#include <windows.storage.streams.h>
#include <robuffer.h>
THIRD_PARTY_INCLUDES_END

#include "Windows/HideWindowsPlatformAtomics.h"
#include "Windows/HideWindowsPlatformTypes.h"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Devices::Bluetooth;
using namespace ABI::Windows::Devices::Bluetooth::Advertisement;
using namespace ABI::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace ABI::Windows::Devices::Radios;
using namespace ABI::Windows::Storage::Streams;

// Bluetooth SIG standard 16-bit UUIDs expanded to the base Bluetooth UUID.
// 0x1816 = Cycling Speed and Cadence service, 0x2A5B = CSC Measurement characteristic.
static const GUID CSC_SERVICE_UUID =
	{ 0x00001816, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };
static const GUID CSC_MEASUREMENT_CHAR_UUID =
	{ 0x00002a5b, 0x0000, 0x1000, { 0x80, 0x00, 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb } };

namespace BikeConnectWinRTInternal
{
	// Blocks the calling thread until an IAsyncOperation<TAsync*> (interface-pointer result) completes.
	// TAsync is the WinRT generic parameter the async operation was created with (often a runtime
	// class, e.g. BluetoothLEDevice), while TResult is the interface the caller actually wants
	// (e.g. IBluetoothLEDevice). These are kept as separate template parameters - rather than both
	// being deduced as a single T - because the two often differ, which previously made the call
	// ambiguous. Note that GetResults() itself is declared in terms of the interface type (WinRT's
	// aggregate-type mapping), so no separate QueryInterface is needed - RawResult must be a TResult*.
	template <typename TAsync, typename TResult>
	static HRESULT BlockingWaitInterface(const ComPtr<IAsyncOperation<TAsync*>>& Op, ComPtr<TResult>& OutResult, DWORD TimeoutMs = 15000)
	{
		if (!Op)
		{
			return E_POINTER;
		}

		HANDLE Event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		if (!Event)
		{
			return E_FAIL;
		}

		auto Handler = Callback<IAsyncOperationCompletedHandler<TAsync*>>(
			[Event](IAsyncOperation<TAsync*>*, AsyncStatus) -> HRESULT
			{
				SetEvent(Event);
				return S_OK;
			});

		HRESULT Hr = Op->put_Completed(Handler.Get());
		if (SUCCEEDED(Hr))
		{
			const DWORD WaitResult = WaitForSingleObjectEx(Event, TimeoutMs, false);
			if (WaitResult == WAIT_OBJECT_0)
			{
				TResult* RawResult = nullptr;
				Hr = Op->GetResults(&RawResult);
				if (SUCCEEDED(Hr) && RawResult)
				{
					OutResult.Attach(RawResult);
				}
			}
			else
			{
				Hr = E_FAIL;
			}
		}

		CloseHandle(Event);
		return Hr;
	}

	// Blocks the calling thread until an IAsyncOperation<T> (value-type result, e.g. an enum) completes.
	template <typename T>
	static HRESULT BlockingWaitValue(const ComPtr<IAsyncOperation<T>>& Op, T& OutResult, DWORD TimeoutMs = 15000)
	{
		if (!Op)
		{
			return E_POINTER;
		}

		HANDLE Event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
		if (!Event)
		{
			return E_FAIL;
		}

		auto Handler = Callback<IAsyncOperationCompletedHandler<T>>(
			[Event](IAsyncOperation<T>*, AsyncStatus) -> HRESULT
			{
				SetEvent(Event);
				return S_OK;
			});

		HRESULT Hr = Op->put_Completed(Handler.Get());
		if (SUCCEEDED(Hr))
		{
			const DWORD WaitResult = WaitForSingleObjectEx(Event, TimeoutMs, false);
			if (WaitResult == WAIT_OBJECT_0)
			{
				Hr = Op->GetResults(&OutResult);
			}
			else
			{
				Hr = E_FAIL;
			}
		}

		CloseHandle(Event);
		return Hr;
	}

	static FBikeCSCData ParseCscMeasurement(const BYTE* Data, UINT32 Length)
	{
		FBikeCSCData Result;
		if (!Data || Length < 1)
		{
			return Result;
		}

		const uint8 Flags = Data[0];
		const bool bWheelPresent = (Flags & 0x01) != 0;
		const bool bCrankPresent = (Flags & 0x02) != 0;
		uint32 Offset = 1;

		if (bWheelPresent && Offset + 6 <= Length)
		{
			const uint32 CumWheelRevs =
				(uint32)Data[Offset] | ((uint32)Data[Offset + 1] << 8) |
				((uint32)Data[Offset + 2] << 16) | ((uint32)Data[Offset + 3] << 24);
			const uint16 LastWheelEventTime = (uint16)Data[Offset + 4] | ((uint16)Data[Offset + 5] << 8);
			Offset += 6;

			Result.bHasWheelData = true;
			Result.CumulativeWheelRevolutions = CumWheelRevs;
			Result.LastWheelEventTime = LastWheelEventTime;
		}

		if (bCrankPresent && Offset + 4 <= Length)
		{
			const uint16 CumCrankRevs = (uint16)Data[Offset] | ((uint16)Data[Offset + 1] << 8);
			const uint16 LastCrankEventTime = (uint16)Data[Offset + 2] | ((uint16)Data[Offset + 3] << 8);
			Offset += 4;

			Result.bHasCrankData = true;
			Result.CumulativeCrankRevolutions = CumCrankRevs;
			Result.LastCrankEventTime = LastCrankEventTime;
		}

		return Result;
	}
}

struct FBikeConnectWinRTImpl::FComImpl
{
	// Continuous scanning
	ComPtr<IBluetoothLEAdvertisementWatcher> Watcher;
	EventRegistrationToken ReceivedToken{};
	bool bWatcherRegistered = false;
	bool bWatcherRunning = false;

	// Radio (adapter) monitoring
	ComPtr<IRadio> BluetoothRadio;
	EventRegistrationToken RadioStateChangedToken{};
	bool bRadioRegistered = false;
	bool bRadioMonitorInitialized = false;

	// Active device connection
	ComPtr<IBluetoothLEDevice> Device;
	EventRegistrationToken ConnectionStatusToken{};
	bool bConnectionStatusRegistered = false;

	ComPtr<IGattCharacteristic> NotifyCharacteristic;
	EventRegistrationToken ValueChangedToken{};
	bool bValueChangedRegistered = false;

	FCriticalSection Lock;
	bool bConnectingOrConnected = false;
	bool bWantsScanning = false;
};

FBikeConnectWinRTImpl::FBikeConnectWinRTImpl(UBikeConnectSubsystem& InOwner)
	: Owner(InOwner)
{
	Com = MakeShared<FComImpl, ESPMode::ThreadSafe>();
}

FBikeConnectWinRTImpl::~FBikeConnectWinRTImpl()
{
	StopScanning();
}

// ---- Forward declarations of the free functions that do the actual COM work ----
namespace BikeConnectWinRTInternal
{
	// Functions that either get dispatched to a background task, or register a WinRT event
	// closure that can fire long after the call that created it returns, take the FComImpl by
	// TSharedRef (by value) rather than by plain reference. Capturing the TSharedRef (instead of
	// a raw FComImpl&) in those closures keeps the connection state alive for as long as WinRT
	// might still call back into it, even if FBikeConnectWinRTImpl itself has since been torn down.
	using FComRef = TSharedRef<FBikeConnectWinRTImpl::FComImpl, ESPMode::ThreadSafe>;

	static void InitializeRadioMonitor(FComRef Com, UBikeConnectSubsystem& Owner);
	static void StartWatcher(FComRef Com, UBikeConnectSubsystem& Owner);
	static void StopWatcher(FBikeConnectWinRTImpl::FComImpl& Com);
	static void CleanupConnection(FBikeConnectWinRTImpl::FComImpl& Com);
	static void ConnectToDevice(FComRef Com, UBikeConnectSubsystem& Owner, UINT64 Address);
}

void FBikeConnectWinRTImpl::StartScanning()
{
	using namespace BikeConnectWinRTInternal;

	FComRef ComRef = Com.ToSharedRef();
	UBikeConnectSubsystem& OwnerRef = Owner;

	// Requesting radio access, enumerating radios, and activating/starting the advertisement
	// watcher all make blocking WinRT calls under the hood. StartScanning() is frequently called
	// directly from the game thread (Subsystem::Initialize / Component::BeginPlay), so this must
	// run on a background thread - otherwise the engine stalls until the OS responds.
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [ComRef, &OwnerRef]()
	{
		FScopeLock ScopeLock(&ComRef->Lock);
		ComRef->bWantsScanning = true;

		if (!ComRef->bRadioMonitorInitialized)
		{
			ComRef->bRadioMonitorInitialized = true;
			InitializeRadioMonitor(ComRef, OwnerRef);
		}

		if (!ComRef->bWatcherRunning)
		{
			StartWatcher(ComRef, OwnerRef);
		}
	});
}

void FBikeConnectWinRTImpl::StopScanning()
{
	using namespace BikeConnectWinRTInternal;

	FScopeLock ScopeLock(&Com->Lock);
	Com->bWantsScanning = false;

	StopWatcher(*Com);
	CleanupConnection(*Com);
}

namespace BikeConnectWinRTInternal
{
	static HRESULT OnRadioStateChanged(FBikeConnectWinRTImpl::FComImpl& Com, UBikeConnectSubsystem& Owner)
	{
		if (!Com.BluetoothRadio)
		{
			return S_OK;
		}

		RadioState State = RadioState_Unknown;
		Com.BluetoothRadio->get_State(&State);

		if (State != RadioState_On)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::BluetoothRadioOff);
		}
		return S_OK;
	}

	static void InitializeRadioMonitor(FComRef Com, UBikeConnectSubsystem& Owner)
	{
		ComPtr<IRadioStatics> RadioStatics;
		HRESULT Hr = RoGetActivationFactory(
			HStringReference(RuntimeClass_Windows_Devices_Radios_Radio).Get(),
			IID_PPV_ARGS(&RadioStatics));
		if (FAILED(Hr) || !RadioStatics)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		ComPtr<IAsyncOperation<RadioAccessStatus>> AccessOp;
		RadioStatics->RequestAccessAsync(&AccessOp);
		RadioAccessStatus AccessStatus = RadioAccessStatus_Unspecified;
		BlockingWaitValue(AccessOp, AccessStatus);
		if (AccessStatus == RadioAccessStatus_DeniedByUser || AccessStatus == RadioAccessStatus_DeniedBySystem)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		ComPtr<IAsyncOperation<IVectorView<Radio*>*>> RadiosOp;
		Hr = RadioStatics->GetRadiosAsync(&RadiosOp);
		if (FAILED(Hr))
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		ComPtr<IVectorView<Radio*>> Radios;
		BlockingWaitInterface(RadiosOp, Radios);
		if (!Radios)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		unsigned int Count = 0;
		Radios->get_Size(&Count);
		for (unsigned int i = 0; i < Count; ++i)
		{
			ComPtr<IRadio> CandidateRadio;
			Radios->GetAt(i, &CandidateRadio);
			if (!CandidateRadio)
			{
				continue;
			}

			RadioKind Kind = RadioKind_Other;
			CandidateRadio->get_Kind(&Kind);
			if (Kind == RadioKind_Bluetooth)
			{
				Com->BluetoothRadio = CandidateRadio;
				break;
			}
		}

		if (!Com->BluetoothRadio)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		auto StateHandler = Callback<ITypedEventHandler<Radio*, IInspectable*>>(
			[Com, &Owner](IRadio*, IInspectable*) -> HRESULT
			{
				return OnRadioStateChanged(*Com, Owner);
			});
		Com->BluetoothRadio->add_StateChanged(StateHandler.Get(), &Com->RadioStateChangedToken);
		Com->bRadioRegistered = true;

		// Check the state we already have immediately, so a bike that never sends an
		// advert (because BT is off) still results in a prompt to the user.
		OnRadioStateChanged(*Com, Owner);
	}

	static HRESULT OnValueChanged(FBikeConnectWinRTImpl::FComImpl& Com, UBikeConnectSubsystem& Owner, IGattValueChangedEventArgs* Args)
	{
		if (!Args)
		{
			return S_OK;
		}

		ComPtr<IBuffer> Buffer;
		Args->get_CharacteristicValue(&Buffer);
		if (!Buffer)
		{
			return S_OK;
		}

		ComPtr<Windows::Storage::Streams::IBufferByteAccess> ByteAccess;
		if (FAILED(Buffer.As(&ByteAccess)) || !ByteAccess)
		{
			return S_OK;
		}

		BYTE* RawBytes = nullptr;
		ByteAccess->Buffer(&RawBytes);

		UINT32 Length = 0;
		Buffer->get_Length(&Length);

		const FBikeCSCData Sample = ParseCscMeasurement(RawBytes, Length);
		Owner.HandlePlatformDataReceived(Sample);
		return S_OK;
	}

	static HRESULT OnConnectionStatusChanged(FBikeConnectWinRTImpl::FComImpl& Com, UBikeConnectSubsystem& Owner)
	{
		if (!Com.Device)
		{
			return S_OK;
		}

		BluetoothConnectionStatus Status = BluetoothConnectionStatus_Disconnected;
		Com.Device->get_ConnectionStatus(&Status);

		if (Status == BluetoothConnectionStatus_Disconnected)
		{
			FScopeLock ScopeLock(&Com.Lock);
			CleanupConnection(Com);
			Owner.HandlePlatformDisconnected();
			// The watcher was left running, so scanning for the bike resumes automatically.
		}
		return S_OK;
	}

	static void CleanupConnection(FBikeConnectWinRTImpl::FComImpl& Com)
	{
		if (Com.NotifyCharacteristic && Com.bValueChangedRegistered)
		{
			Com.NotifyCharacteristic->remove_ValueChanged(Com.ValueChangedToken);
			Com.bValueChangedRegistered = false;
		}
		Com.NotifyCharacteristic.Reset();

		if (Com.Device && Com.bConnectionStatusRegistered)
		{
			Com.Device->remove_ConnectionStatusChanged(Com.ConnectionStatusToken);
			Com.bConnectionStatusRegistered = false;
		}
		Com.Device.Reset();

		Com.bConnectingOrConnected = false;
	}

	static void ConnectToDevice(FComRef Com, UBikeConnectSubsystem& Owner, UINT64 Address)
	{
		Owner.HandlePlatformConnecting();

		ComPtr<IBluetoothLEDeviceStatics> DeviceStatics;
		HRESULT Hr = RoGetActivationFactory(
			HStringReference(RuntimeClass_Windows_Devices_Bluetooth_BluetoothLEDevice).Get(),
			IID_PPV_ARGS(&DeviceStatics));
		if (FAILED(Hr) || !DeviceStatics)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ConnectionFailed);
			return;
		}

		ComPtr<IAsyncOperation<BluetoothLEDevice*>> DeviceOp;
		Hr = DeviceStatics->FromBluetoothAddressAsync(Address, &DeviceOp);
		if (FAILED(Hr))
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ConnectionFailed);
			return;
		}

		ComPtr<IBluetoothLEDevice> NewDevice;
		BlockingWaitInterface(DeviceOp, NewDevice);
		if (!NewDevice)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ConnectionFailed);
			return;
		}

		ComPtr<IBluetoothLEDevice3> Device3;
		if (FAILED(NewDevice.As(&Device3)) || !Device3)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ConnectionFailed);
			return;
		}

		ComPtr<IAsyncOperation<GattDeviceServicesResult*>> ServicesOp;
		Device3->GetGattServicesForUuidAsync(CSC_SERVICE_UUID, &ServicesOp);
		ComPtr<IGattDeviceServicesResult> ServicesResult;
		BlockingWaitInterface(ServicesOp, ServicesResult);

		GattCommunicationStatus ServicesStatus = GattCommunicationStatus_Unreachable;
		ComPtr<IVectorView<GattDeviceService*>> Services;
		if (ServicesResult)
		{
			ServicesResult->get_Status(&ServicesStatus);
			ServicesResult->get_Services(&Services);
		}

		unsigned int ServiceCount = 0;
		if (Services)
		{
			Services->get_Size(&ServiceCount);
		}

		if (ServicesStatus != GattCommunicationStatus_Success || ServiceCount == 0)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ServiceNotFound);
			return;
		}

		ComPtr<IGattDeviceService> Service;
		Services->GetAt(0, &Service);

		ComPtr<IGattDeviceService3> Service3;
		if (!Service || FAILED(Service.As(&Service3)) || !Service3)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ServiceNotFound);
			return;
		}

		ComPtr<IAsyncOperation<GattCharacteristicsResult*>> CharsOp;
		Service3->GetCharacteristicsForUuidAsync(CSC_MEASUREMENT_CHAR_UUID, &CharsOp);
		ComPtr<IGattCharacteristicsResult> CharsResult;
		BlockingWaitInterface(CharsOp, CharsResult);

		GattCommunicationStatus CharsStatus = GattCommunicationStatus_Unreachable;
		ComPtr<IVectorView<GattCharacteristic*>> Chars;
		if (CharsResult)
		{
			CharsResult->get_Status(&CharsStatus);
			CharsResult->get_Characteristics(&Chars);
		}

		unsigned int CharCount = 0;
		if (Chars)
		{
			Chars->get_Size(&CharCount);
		}

		if (CharsStatus != GattCommunicationStatus_Success || CharCount == 0)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ServiceNotFound);
			return;
		}

		ComPtr<IGattCharacteristic> Characteristic;
		Chars->GetAt(0, &Characteristic);
		if (!Characteristic)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ServiceNotFound);
			return;
		}

		ComPtr<IAsyncOperation<GattCommunicationStatus>> CccdOp;
		Characteristic->WriteClientCharacteristicConfigurationDescriptorAsync(
			GattClientCharacteristicConfigurationDescriptorValue_Notify, &CccdOp);

		GattCommunicationStatus CccdStatus = GattCommunicationStatus_Unreachable;
		BlockingWaitValue(CccdOp, CccdStatus);
		if (CccdStatus != GattCommunicationStatus_Success)
		{
			CleanupConnection(*Com);
			Owner.HandlePlatformError(EBikeBluetoothError::ConnectionFailed);
			return;
		}

		auto ValueHandler = Callback<ITypedEventHandler<GattCharacteristic*, GattValueChangedEventArgs*>>(
			[Com, &Owner](IGattCharacteristic*, IGattValueChangedEventArgs* Args) -> HRESULT
			{
				return OnValueChanged(*Com, Owner, Args);
			});
		Characteristic->add_ValueChanged(ValueHandler.Get(), &Com->ValueChangedToken);
		Com->bValueChangedRegistered = true;
		Com->NotifyCharacteristic = Characteristic;

		auto ConnHandler = Callback<ITypedEventHandler<BluetoothLEDevice*, IInspectable*>>(
			[Com, &Owner](IBluetoothLEDevice*, IInspectable*) -> HRESULT
			{
				return OnConnectionStatusChanged(*Com, Owner);
			});
		NewDevice->add_ConnectionStatusChanged(ConnHandler.Get(), &Com->ConnectionStatusToken);
		Com->bConnectionStatusRegistered = true;
		Com->Device = NewDevice;

		Owner.HandlePlatformConnected();
	}

	static HRESULT OnAdvertisementReceived(
		FComRef Com,
		UBikeConnectSubsystem& Owner,
		IBluetoothLEAdvertisementReceivedEventArgs* Args)
	{
		if (!Args)
		{
			return S_OK;
		}

		FScopeLock ScopeLock(&Com->Lock);
		if (Com->bConnectingOrConnected || !Com->bWantsScanning)
		{
			return S_OK;
		}

		ComPtr<IBluetoothLEAdvertisement> Advertisement;
		Args->get_Advertisement(&Advertisement);
		if (!Advertisement)
		{
			return S_OK;
		}

		ComPtr<IVector<GUID>> ServiceUuids;
		Advertisement->get_ServiceUuids(&ServiceUuids);
		if (!ServiceUuids)
		{
			return S_OK;
		}

		unsigned int UuidCount = 0;
		ServiceUuids->get_Size(&UuidCount);

		bool bFoundCscService = false;
		for (unsigned int i = 0; i < UuidCount; ++i)
		{
			GUID Uuid{};
			ServiceUuids->GetAt(i, &Uuid);
			if (IsEqualGUID(Uuid, CSC_SERVICE_UUID))
			{
				bFoundCscService = true;
				break;
			}
		}

		if (!bFoundCscService)
		{
			return S_OK;
		}

		UINT64 Address = 0;
		Args->get_BluetoothAddress(&Address);

		Com->bConnectingOrConnected = true;

		// The actual connect + GATT discovery handshake below can block for several seconds
		// across multiple WinRT async calls. Do it on a background task rather than inline here -
		// this handler runs on a WinRT-owned event-dispatch thread, and blocking that thread for
		// the whole handshake (every single time the bike's advertisement is received) is what
		// was causing the multi-second engine hitches. Com is captured by value (a ref-counted
		// TSharedRef) so the connection state stays alive for the task's duration even if
		// scanning is stopped/torn down in the meantime.
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Com, &Owner, Address]()
		{
			ConnectToDevice(Com, Owner, Address);
		});
		return S_OK;
	}

	static void StartWatcher(FComRef Com, UBikeConnectSubsystem& Owner)
	{
		if (!Com->Watcher)
		{
			ComPtr<IInspectable> Inspectable;
			HRESULT Hr = RoActivateInstance(
				HStringReference(RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementWatcher).Get(),
				&Inspectable);
			if (FAILED(Hr) || !Inspectable)
			{
				Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
				return;
			}
			Inspectable.As(&Com->Watcher);
		}

		if (!Com->Watcher)
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
			return;
		}

		Com->Watcher->put_ScanningMode(BluetoothLEScanningMode_Active);

		if (!Com->bWatcherRegistered)
		{
			auto ReceivedHandler = Callback<ITypedEventHandler<BluetoothLEAdvertisementWatcher*, BluetoothLEAdvertisementReceivedEventArgs*>>(
				[Com, &Owner](IBluetoothLEAdvertisementWatcher*, IBluetoothLEAdvertisementReceivedEventArgs* Args) -> HRESULT
				{
					return OnAdvertisementReceived(Com, Owner, Args);
				});
			Com->Watcher->add_Received(ReceivedHandler.Get(), &Com->ReceivedToken);
			Com->bWatcherRegistered = true;
		}

		const HRESULT StartHr = Com->Watcher->Start();
		Com->bWatcherRunning = SUCCEEDED(StartHr);
		if (FAILED(StartHr))
		{
			Owner.HandlePlatformError(EBikeBluetoothError::RadioUnavailable);
		}
	}

	static void StopWatcher(FBikeConnectWinRTImpl::FComImpl& Com)
	{
		if (Com.Watcher && Com.bWatcherRunning)
		{
			Com.Watcher->Stop();
		}
		if (Com.Watcher && Com.bWatcherRegistered)
		{
			Com.Watcher->remove_Received(Com.ReceivedToken);
			Com.bWatcherRegistered = false;
		}
		Com.bWatcherRunning = false;
	}
}

#endif // WITH_BIKECONNECT_WINRT

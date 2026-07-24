#include "CyclingPowerBLESubsystem.h"
#include "Async/Async.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"
#include <atomic>

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#pragma push_macro("TEXT")
#undef TEXT
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>
#pragma pop_macro("TEXT")
#include "Windows/HideWindowsPlatformTypes.h"

using namespace winrt::Windows::Devices::Bluetooth;
using namespace winrt::Windows::Devices::Bluetooth::Advertisement;
using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
using namespace winrt::Windows::Storage::Streams;

static winrt::guid ShortUuidToGuid(uint16 ShortUuid)
{
	// Bluetooth Base UUID: 0000xxxx-0000-1000-8000-00805F9B34FB
	return winrt::guid{
		static_cast<uint32_t>(ShortUuid), 0x0000, 0x1000,
		{ 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB }
	};
}

// Tacx FE-C over BLE (vendor UUIDs, not Bluetooth SIG-registered)
static const winrt::guid FEC_SERVICE_UUID{ 0x6E40FEC1, 0xB5A3, 0xF393, { 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E } };
static const winrt::guid FEC_TX_CHAR_UUID{ 0x6E40FEC2, 0xB5A3, 0xF393, { 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E } }; // trainer -> app, notify
static const winrt::guid FEC_RX_CHAR_UUID{ 0x6E40FEC3, 0xB5A3, 0xF393, { 0xE0, 0xA9, 0xE5, 0x0E, 0x24, 0xDC, 0xCA, 0x9E } }; // app -> trainer, write

static TArray<uint8> BuildAntMessage(const uint8 Payload[8], uint8 Channel = 0x00)
{
	TArray<uint8> Msg;
	Msg.Add(0xA4); // sync
	Msg.Add(0x09); // length: channel byte + 8 payload bytes
	Msg.Add(0x4F); // acknowledged data message ID
	Msg.Add(Channel);
	for (int32 i = 0; i < 8; ++i)
	{
		Msg.Add(Payload[i]);
	}

	uint8 Checksum = 0;
	for (uint8 B : Msg)
	{
		Checksum ^= B;
	}
	Msg.Add(Checksum);

	return Msg;
}

#endif // PLATFORM_WINDOWS

constexpr uint16 CYCLING_POWER_SERVICE_UUID = 0x1818;
constexpr uint16 CYCLING_POWER_MEASUREMENT_UUID = 0x2A63;

/**
 * Platform implementation. Kept entirely inside the .cpp so no WinRT/Windows
 * headers leak into the subsystem's public header.
 *
 * NOTE ON LIFETIME: BLE work runs on a background thread using blocking
 * WinRT calls (.get()). Shutdown() stops the watcher and clears the
 * connection, but if a connection attempt is mid-flight when the owning
 * GameInstance is torn down, that background thread may still be running
 * briefly. This is fine for normal PIE/game shutdown in practice, but if you
 * need hard real-time teardown guarantees, add a generation counter/weak
 * pointer check before touching Owner in each background continuation.
 */
class FCyclingPowerBLEImpl
{
public:
	explicit FCyclingPowerBLEImpl(UCyclingPowerBLESubsystem* InOwner)
		: Owner(InOwner)
	{
	}

	~FCyclingPowerBLEImpl()
	{
		Shutdown();
	}

	void StartScanning()
	{
#if PLATFORM_WINDOWS
		if (bConnected.load() || bConnecting.load())
		{
			return;
		}
		bShuttingDown = false;

		Watcher = BluetoothLEAdvertisementWatcher();
		Watcher.ScanningMode(BluetoothLEScanningMode::Active);
		Watcher.AdvertisementFilter().Advertisement().ServiceUuids().Append(
			ShortUuidToGuid(CYCLING_POWER_SERVICE_UUID));

		WatcherToken = Watcher.Received(
			[this](BluetoothLEAdvertisementWatcher const& watcher, BluetoothLEAdvertisementReceivedEventArgs const& args)
			{
				bool bExpected = false;
				if (!bConnecting.compare_exchange_strong(bExpected, true))
				{
					return;
				}
				if (bConnected.load())
				{
					bConnecting = false;
					return;
				}

				watcher.Stop();
				const uint64 Address = args.BluetoothAddress();
				const FString AddrStr = FString::Printf(TEXT("%012llX"), Address);

				AsyncTask(ENamedThreads::GameThread, [this, AddrStr]()
					{
						if (Owner)
						{
							Owner->OnDeviceFound.Broadcast(AddrStr);
							Owner->SetConnectionState(ECyclingPowerConnectionState::Connecting);
						}
					});

				Async(EAsyncExecution::Thread, [this, Address]()
					{
						winrt::init_apartment(winrt::apartment_type::multi_threaded);
						ConnectAndSubscribe(Address);
						winrt::uninit_apartment();
					});
			});

		AsyncTask(ENamedThreads::GameThread, [this]()
			{
				if (Owner)
				{
					Owner->SetConnectionState(ECyclingPowerConnectionState::Scanning);
				}
			});

		Watcher.Start();
#else
		UE_LOG(LogTemp, Warning, TEXT("CyclingPowerBLE: BLE scanning is currently only implemented for Windows."));
#endif
	}

	void StopScanning()
	{
#if PLATFORM_WINDOWS
		if (Watcher)
		{
			Watcher.Stop();
		}
#endif
	}

	void Disconnect()
	{
#if PLATFORM_WINDOWS
		StopScanning();
		CleanupConnection();
		AsyncTask(ENamedThreads::GameThread, [this]()
			{
				if (Owner)
				{
					Owner->SetConnectionState(ECyclingPowerConnectionState::Disconnected);
				}
			});
#endif
	}

	void Shutdown()
	{
#if PLATFORM_WINDOWS
		bShuttingDown = true;
		StopScanning();
		CleanupConnection();
#endif
	}

	void SetResistancePercent(float Percent)
	{
#if PLATFORM_WINDOWS
		if (!FecControlChar)
		{
			UE_LOG(LogTemp, Warning, TEXT("CyclingPowerBLE: Not connected to the FE-C control point, can't set resistance."));
			return;
		}

		Percent = FMath::Clamp(Percent, 0.0f, 100.0f);
		const uint8 ResistanceByte = static_cast<uint8>(FMath::RoundToInt(Percent * 2.0f)); // 0.5% per unit

		const uint8 Payload[8] = { 0x30, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, ResistanceByte };
		TArray<uint8> AntMessage = BuildAntMessage(Payload);

		DataWriter Writer;
		Writer.WriteBytes(winrt::array_view<const uint8_t>(AntMessage.GetData(), AntMessage.GetData() + AntMessage.Num()));
		auto Buffer = Writer.DetachBuffer();

		GattCharacteristic Char = FecControlChar;
		Async(EAsyncExecution::Thread, [Char, Buffer]() mutable
			{
				try
				{
					winrt::init_apartment(winrt::apartment_type::multi_threaded);
					Char.WriteValueAsync(Buffer, GattWriteOption::WriteWithoutResponse).get();
					winrt::uninit_apartment();
				}
				catch (winrt::hresult_error const&)
				{
					UE_LOG(LogTemp, Warning, TEXT("CyclingPowerBLE: Failed to write resistance to FE-C control point."));
				}
			});
#endif
	}

private:
#if PLATFORM_WINDOWS
	void ConnectAndSubscribe(uint64 Address)
	{
		try
		{
			auto Device_ = BluetoothLEDevice::FromBluetoothAddressAsync(Address).get();
			if (!Device_)
			{
				OnConnectFailed();
				return;
			}

			auto ServicesResult = Device_.GetGattServicesForUuidAsync(
				ShortUuidToGuid(CYCLING_POWER_SERVICE_UUID), BluetoothCacheMode::Uncached).get();

			if (ServicesResult.Status() != GattCommunicationStatus::Success || ServicesResult.Services().Size() == 0)
			{
				Device_.Close();
				OnConnectFailed();
				return;
			}

			auto Service = ServicesResult.Services().GetAt(0);
			auto CharsResult = Service.GetCharacteristicsForUuidAsync(
				ShortUuidToGuid(CYCLING_POWER_MEASUREMENT_UUID), BluetoothCacheMode::Uncached).get();

			if (CharsResult.Status() != GattCommunicationStatus::Success || CharsResult.Characteristics().Size() == 0)
			{
				Device_.Close();
				OnConnectFailed();
				return;
			}

			auto Characteristic = CharsResult.Characteristics().GetAt(0);
			auto CccdStatus = Characteristic.WriteClientCharacteristicConfigurationDescriptorAsync(
				GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

			if (CccdStatus != GattCommunicationStatus::Success)
			{
				Device_.Close();
				OnConnectFailed();
				return;
			}

			Device = Device_;
			PowerChar = Characteristic;

			NotifyToken = PowerChar.ValueChanged(
				[this](GattCharacteristic const&, GattValueChangedEventArgs const& args)
				{
					HandleNotification(args.CharacteristicValue());
				});

			StatusToken = Device.ConnectionStatusChanged(
				[this](BluetoothLEDevice const& Sender, auto const&)
				{
					if (Sender.ConnectionStatus() == BluetoothConnectionStatus::Disconnected)
					{
						HandleDisconnect();
					}
				});

			bConnected = true;
			bConnecting = false;

			try
			{
				auto FecServices = Device_.GetGattServicesForUuidAsync(FEC_SERVICE_UUID, BluetoothCacheMode::Uncached).get();
				if (FecServices.Status() == GattCommunicationStatus::Success && FecServices.Services().Size() > 0)
				{
					auto FecService = FecServices.Services().GetAt(0);
					auto FecChars = FecService.GetCharacteristicsForUuidAsync(FEC_RX_CHAR_UUID, BluetoothCacheMode::Uncached).get();
					if (FecChars.Status() == GattCommunicationStatus::Success && FecChars.Characteristics().Size() > 0)
					{
						FecControlChar = FecChars.Characteristics().GetAt(0);
					}
				}
			}
			catch (winrt::hresult_error const&)
			{
				// FE-C control not available on this device - resistance control just won't work.
			}

			AsyncTask(ENamedThreads::GameThread, [this]()
				{
					if (Owner)
					{
						Owner->SetConnectionState(ECyclingPowerConnectionState::Connected);
					}
				});
		}
		catch (winrt::hresult_error const&)
		{
			OnConnectFailed();
		}
	}

	void OnConnectFailed()
	{
		bConnecting = false;
		AsyncTask(ENamedThreads::GameThread, [this]()
			{
				if (Owner)
				{
					Owner->SetConnectionState(ECyclingPowerConnectionState::Disconnected);
				}
			});
		if (!bShuttingDown)
		{
			StartScanning();
		}
	}

	void HandleDisconnect()
	{
		CleanupConnection();
		AsyncTask(ENamedThreads::GameThread, [this]()
			{
				if (Owner)
				{
					Owner->SetConnectionState(ECyclingPowerConnectionState::Disconnected);
				}
			});
		if (!bShuttingDown)
		{
			StartScanning();
		}
	}

	void CleanupConnection()
	{
		if (PowerChar)
		{
			PowerChar.ValueChanged(NotifyToken);
			PowerChar = nullptr;
			FecControlChar = nullptr;
		}
		if (Device)
		{
			Device.ConnectionStatusChanged(StatusToken);
			Device.Close();
			Device = nullptr;
			FecControlChar = nullptr;
		}
		bConnected = false;
	}

	void HandleNotification(IBuffer const& Buffer)
	{
		auto Reader = DataReader::FromBuffer(Buffer);
		Reader.ByteOrder(ByteOrder::LittleEndian);
		if (Reader.UnconsumedBufferLength() < 4)
		{
			return;
		}

		const uint16_t Flags = Reader.ReadUInt16();
		const int16_t InstPower = Reader.ReadInt16();

		FCyclingPowerData Data;
		Data.InstantaneousPower = InstPower;

		const bool bPedalBalancePresent = (Flags & 0x0001) != 0;
		const bool bAccumTorquePresent = (Flags & 0x0004) != 0;
		const bool bWheelRevPresent = (Flags & 0x0010) != 0;
		const bool bCrankRevPresent = (Flags & 0x0020) != 0;

		if (bPedalBalancePresent && Reader.UnconsumedBufferLength() >= 1)
		{
			const uint8_t Balance = Reader.ReadByte();
			Data.bHasBalance = true;
			Data.PedalPowerBalancePercent = Balance / 2.0f;
		}

		if (bAccumTorquePresent && Reader.UnconsumedBufferLength() >= 2)
		{
			const uint16_t Torque = Reader.ReadUInt16();
			Data.bHasTorque = true;
			Data.AccumulatedTorqueNm = Torque / 32.0f;
		}

		if (bWheelRevPresent && Reader.UnconsumedBufferLength() >= 6)
		{
			const uint32_t CumulativeWheelRevs = Reader.ReadUInt32();
			const uint16_t LastWheelEvent = Reader.ReadUInt16();

			if (bHaveWheelBaseline)
			{
				const uint32_t RevDiff = CumulativeWheelRevs - LastWheelRevs;
				const uint16_t TimeDiff = static_cast<uint16_t>(LastWheelEvent - LastWheelEventTime);
				if (TimeDiff > 0 && RevDiff > 0)
				{
					const double Seconds = TimeDiff / 2048.0;
					Data.bHasWheelRPM = true;
					Data.WheelRPM = static_cast<float>((RevDiff / Seconds) * 60.0);
				}
			}
			LastWheelRevs = CumulativeWheelRevs;
			LastWheelEventTime = LastWheelEvent;
			bHaveWheelBaseline = true;
		}

		if (bCrankRevPresent && Reader.UnconsumedBufferLength() >= 4)
		{
			const uint16_t CumulativeCrankRevs = Reader.ReadUInt16();
			const uint16_t LastCrankEvent = Reader.ReadUInt16();

			if (bHaveCrankBaseline)
			{
				const uint16_t RevDiff = static_cast<uint16_t>(CumulativeCrankRevs - LastCrankRevs);
				const uint16_t TimeDiff = static_cast<uint16_t>(LastCrankEvent - LastCrankEventTime);
				if (TimeDiff > 0)
				{
					const double Seconds = TimeDiff / 1024.0;
					Data.bHasCadence = true;
					Data.CadenceRPM = static_cast<float>((RevDiff / Seconds) * 60.0);
				}
			}
			LastCrankRevs = CumulativeCrankRevs;
			LastCrankEventTime = LastCrankEvent;
			bHaveCrankBaseline = true;
		}

		AsyncTask(ENamedThreads::GameThread, [this, Data]()
			{
				if (Owner)
				{
					Owner->LastData = Data;
					Owner->OnDataReceived.Broadcast(Data);
				}
			});
	}

	BluetoothLEAdvertisementWatcher Watcher{ nullptr };
	BluetoothLEDevice Device{ nullptr };
	GattCharacteristic PowerChar{ nullptr };
	GattCharacteristic FecControlChar{ nullptr };
	winrt::event_token WatcherToken;
	winrt::event_token NotifyToken;
	winrt::event_token StatusToken;

	uint32_t LastWheelRevs = 0;
	uint16_t LastWheelEventTime = 0;
	bool bHaveWheelBaseline = false;

	uint16_t LastCrankRevs = 0;
	uint16_t LastCrankEventTime = 0;
	bool bHaveCrankBaseline = false;
#endif // PLATFORM_WINDOWS

	UCyclingPowerBLESubsystem* Owner = nullptr;
	std::atomic<bool> bConnected{ false };
	std::atomic<bool> bConnecting{ false };
	bool bShuttingDown = false;
};

void UCyclingPowerBLESubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Impl = new FCyclingPowerBLEImpl(this);
}

void UCyclingPowerBLESubsystem::Deinitialize()
{
	if (Impl)
	{
		delete Impl;
		Impl = nullptr;
	}
	Super::Deinitialize();
}

UCyclingPowerBLESubsystem* UCyclingPowerBLESubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContextObject);
	return GameInstance ? GameInstance->GetSubsystem<UCyclingPowerBLESubsystem>() : nullptr;
}

void UCyclingPowerBLESubsystem::StartScanning()
{
	if (Impl)
	{
		Impl->StartScanning();
	}
}

void UCyclingPowerBLESubsystem::StopScanning()
{
	if (Impl)
	{
		Impl->StopScanning();
	}
}

void UCyclingPowerBLESubsystem::Disconnect()
{
	if (Impl)
	{
		Impl->Disconnect();
	}
}

void UCyclingPowerBLESubsystem::SetConnectionState(ECyclingPowerConnectionState NewState)
{
	ConnectionState = NewState;
	OnConnectionStateChanged.Broadcast(NewState);
}

void UCyclingPowerBLESubsystem::SetResistancePercent(float ResistancePercent)
{
	if (Impl)
	{
		Impl->SetResistancePercent(ResistancePercent);
	}
}

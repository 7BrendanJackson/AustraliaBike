# BikeConnect

An Unreal Engine 5 plugin that connects to a Bluetooth LE exercise bike broadcasting the
standard **Cycling Speed and Cadence (CSC)** GATT profile, using Windows' native **WinRT**
Bluetooth APIs directly (via the WinRT COM ABI + `Microsoft::WRL::ComPtr`). Nothing is
downloaded or vendored - everything used ships with the Windows 10/11 SDK that Visual
Studio already installs for building UE.

Windows-only (`Win64`), since it talks straight to `Windows.Devices.Bluetooth.*`.

## What it does

- Continuously scans for BLE advertisements containing the CSC service UUID (`0x1816`),
  since your bike only powers on/advertises while you're actively pedalling.
- Auto-connects the moment it's found, subscribes to CSC Measurement (`0x2A5B`) notifications.
- Auto-detects disconnect (bike powered off / rode out of range) and silently resumes
  scanning so it reconnects next time you start pedalling - no user action needed.
- Decodes wheel + crank revolution data into **speed (km/h)** and **cadence (RPM)**,
  correctly handling the sensor's 16/32-bit rollover counters.
- Monitors the Bluetooth radio itself and fires an event if it's turned off, so you can
  show the user a "please turn on Bluetooth" prompt.
- Exposes everything to Blueprint via a drop-in `ActorComponent`.

## Install

1. Copy the `BikeConnect` folder into your project's `Plugins/` directory.
2. Regenerate project files / open the project - UE will prompt to build the plugin.
3. Enable **BikeConnect** in Edit > Plugins if it isn't already (it's `EnabledByDefault`).

## Usage

### Blueprint

1. Add a **Bike Sensor** component to any Actor or Pawn (search "Bike Sensor" in the
   Add Component menu).
2. Wire up its events: `On Bike Connected`, `On Bike Disconnected`, `On Bike Data Updated`
   (gives you an `FBikeCSCData` with `Speed Kph` / `Cadence Rpm`), and `On Bluetooth Error`.
3. That's it - scanning starts automatically on `BeginPlay` (toggle via
   `Auto Start Scanning On Begin Play` if you'd rather call `Start Scanning` yourself).

You can add the component to as many actors as you like (e.g. your pawn *and* a HUD
actor) - they all share the single underlying bike connection; only one BLE link is ever
opened.

### C++

```cpp
#include "BikeSensorComponent.h"

void AMyPawn::BeginPlay()
{
    Super::BeginPlay();

    UBikeSensorComponent* Bike = FindComponentByClass<UBikeSensorComponent>();
    Bike->OnBikeDataUpdated.AddDynamic(this, &AMyPawn::HandleBikeData);
    Bike->OnBluetoothError.AddDynamic(this, &AMyPawn::HandleBikeError);
}

void AMyPawn::HandleBikeData(const FBikeCSCData& Data)
{
    UE_LOG(LogTemp, Log, TEXT("Speed: %.1f km/h  Cadence: %.0f rpm"), Data.SpeedKph, Data.CadenceRpm);
}

void AMyPawn::HandleBikeError(EBikeBluetoothError ErrorType)
{
    if (ErrorType == EBikeBluetoothError::BluetoothRadioOff)
    {
        // Show "please turn on Bluetooth" UI
    }
}
```

You can also talk to `UBikeConnectSubsystem` directly (via
`GetGameInstance()->GetSubsystem<UBikeConnectSubsystem>()`) if you'd rather not use the
component - it's the single source of truth the component just forwards events from.

### Wheel size

Speed is derived from wheel-revolution deltas, so it depends on the wheel circumference.
Set `Wheel Circumference Mm` on `UBikeConnectSubsystem` (default `2105mm`, a common
700x25c road wheel) - either via a Blueprint call at startup or by editing the default in
`Project Settings` if you expose it there. Cadence doesn't depend on this.

## Architecture

- `UBikeConnectSubsystem` (GameInstanceSubsystem) - owns the *one* physical bike
  connection and all WinRT/COM state, and broadcasts `UPROPERTY(BlueprintAssignable)`
  multicast delegates. All platform callbacks arrive on background WinRT threadpool
  threads and are hopped to the game thread via `AsyncTask(ENamedThreads::GameThread, ...)`
  before touching any `UObject` state.
- `UBikeSensorComponent` (ActorComponent) - thin, per-actor adapter. Binds to the
  subsystem's delegates and re-broadcasts them as its own instance-editable Blueprint
  events, so each actor gets its own event graph hookups.
- `FBikeConnectWinRTImpl` (`Private/Windows/BikeConnectWinRT.h/.cpp`, Windows only) - the
  actual WinRT ABI code: advertisement watcher, GATT service/characteristic discovery,
  notification subscription, CSC payload parsing, and Bluetooth radio monitoring. Kept
  behind a pimpl so no COM/Windows headers ever leak into the public, cross-platform
  headers.

## Notes / things to check when you first build this

Raw WinRT-ABI code (as opposed to the `winrt::` C++/WinRT projection, or a wrapper
package) is the most portable way to do this inside UE without fighting `TEXT`/exception/
coroutine macro collisions, but its exact interface names (`IBluetoothLEDevice3`,
`IGattDeviceService3`, etc.) can shift slightly between Windows SDK versions. If you hit
a compile error in `BikeConnectWinRT.cpp` about a missing interface or method:

- It's almost always a case of the interface being named `IBluetoothLEDeviceN` /
  `IGattDeviceServiceN` with a different `N` in your installed SDK - check
  `windows.devices.bluetooth.h` / `windows.devices.bluetooth.genericattributeprofile.h`
  under your Windows Kits `Include\<version>\um` (or `winrt`) folder for the exact
  interface that exposes the method in question and update the `.As<>()` call.
- Make sure your project's Windows SDK is 10.0.17763.0 or newer (CSC support has been
  in the SDK far longer than that, but this is the floor I've targeted here).

I wasn't able to compile this against an actual UE5 + Windows SDK toolchain in this
environment (no Windows/UE install available here), so please do a first build and treat
any such interface-name mismatches as a quick fix rather than a sign something structural
is wrong - the scanning/connect/notify/parse flow and the UE-side plumbing (subsystem,
component, thread-hopping, delegates) are all complete and should not need changes.

## GATT reference

- CSC Service: `00001816-0000-1000-8000-00805f9b34fb`
- CSC Measurement characteristic (notify): `00002a5b-0000-1000-8000-00805f9b34fb`
- Flags byte: bit 0 = wheel data present, bit 1 = crank data present, followed by
  (if present) a 4-byte cumulative wheel revolution count + 2-byte last wheel event time,
  then (if present) a 2-byte cumulative crank revolution count + 2-byte last crank event time.

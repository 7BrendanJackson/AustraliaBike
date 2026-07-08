# CyclingPowerBLE (UE 5.4 plugin)

Scans for, auto-connects to, and streams data from a BLE **Cycling Power
Service** device (smart trainer, power meter pedals/crank, etc.), exposed via
a `UGameInstanceSubsystem` so it's reachable from any Actor, Pawn, Widget, or
Blueprint.

Windows-only implementation (uses `Windows.Devices.Bluetooth` via C++/WinRT).
On other platforms the API is present but calls just log a warning and no-op.

## Install

1. Copy the `CyclingPowerBLE` folder into your project's `Plugins/` directory
   (create that directory if it doesn't exist), so you have:
   `YourProject/Plugins/CyclingPowerBLE/CyclingPowerBLE.uplugin`
2. Regenerate project files / open the `.uproject` — Unreal will prompt to
   build the plugin.
3. Enable it in **Edit > Plugins** if it isn't already enabled.

## Usage (Blueprint)

- Call **Get Cycling Power BLE Subsystem** (any World Context, e.g. `Self`)
  to get the subsystem.
- Call **Start Scanning**.
- Bind to **On Data Received** to get an `FCyclingPowerData` struct each time
  a notification arrives (Power, Cadence, Balance, Torque, Wheel RPM — each
  optional field has a matching `bHas...` flag since not every device sends
  every field).
- Bind to **On Connection State Changed** for `Disconnected / Scanning /
  Connecting / Connected` transitions.
- Bind to **On Device Found** if you want to show the MAC address as soon as
  a matching device is seen (before the connection completes).

It will keep re-scanning automatically any time it's not connected — including
right after a device drops out — so you generally only need to call
**Start Scanning** once, e.g. in `BeginPlay` of your bike/pawn actor.

## Usage (C++)

```cpp
#include "CyclingPowerBLESubsystem.h"

void AMyBikePawn::BeginPlay()
{
    Super::BeginPlay();

    if (UCyclingPowerBLESubsystem* BLE = UCyclingPowerBLESubsystem::Get(this))
    {
        BLE->OnDataReceived.AddDynamic(this, &AMyBikePawn::HandlePowerData);
        BLE->OnConnectionStateChanged.AddDynamic(this, &AMyBikePawn::HandleConnectionState);
        BLE->StartScanning();
    }
}

void AMyBikePawn::HandlePowerData(const FCyclingPowerData& Data)
{
    UE_LOG(LogTemp, Log, TEXT("Power: %d W"), Data.InstantaneousPower);
}
```

## Notes / caveats

- **Windows only** for now — `Windows.Devices.Bluetooth` is a WinRT API.
  A Linux/SteamDeck build would need a different backend (e.g. BlueZ via
  D-Bus); ask if you want that added as a second backend behind the same
  subsystem API.
- **Wheel RPM** isn't converted to speed since that requires wheel
  circumference, which varies by bike/tyre — add a constant/setting for that
  if you want actual km/h.
- Background BLE work runs on worker threads and hops back to the game
  thread via `AsyncTask` before touching any `UObject` state, so it's safe to
  bind Blueprint events directly.
- Only one device is tracked at a time — first match wins. Say the word if
  you want multi-device (e.g. power meter + separate speed/cadence sensor)
  support.

#!/usr/bin/env python3
"""
Exercise Bike Speed Reader
Connects to an exercise bike via Bluetooth LE and reads speed/cadence data.
"""

import asyncio
import logging
import struct
from bleak import BleakClient, BleakScanner
import vgamepad as vg

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(levelname)s: %(message)s')
logger = logging.getLogger(__name__)

# Gamepad setup
gamepad = vg.VX360Gamepad()
MAX_SPEED = 15.0  # m/s

# Connection settings
DEFAULT_DEVICE_ADDRESS = "F2:D6:BF:32:06:BC"
RECONNECT_DELAY = 2  # seconds (minimal delay for immediate reconnect)
MAX_RETRIES = None  # None for infinite retries
CONNECTION_CHECK_INTERVAL = 5  # seconds - check connection health

# Bluetooth UUIDs for cycling services
CYCLING_SPEED_CADENCE_SERVICE_UUID = "00001816-0000-1000-8000-00805f9b34fb"
CYCLING_SPEED_CADENCE_MEASUREMENT_CHAR_UUID = "00002a5b-0000-1000-8000-00805f9b34fb"
FITNESS_MACHINE_SERVICE_UUID = "00001826-0000-1000-8000-00805f9b34fb"
FITNESS_MACHINE_STATUS_CHAR_UUID = "00002ada-0000-1000-8000-00805f9b34fb"

# Wheel circumference in meters (standard 27.5 inch wheel)
# Adjust this based on your bike's actual wheel size
WHEEL_CIRCUMFERENCE = 2.096  # meters

# Track previous measurement for speed calculation
last_wheel_revs = None
last_wheel_time = None

# Track joystick position for smooth transitions
current_joystick_y = 0.0
target_joystick_y = 0.0
last_speed_update = None
DECAY_CHECK_INTERVAL = 0.05  # Check every 60fps (16.6ms)
SPEED_TIMEOUT = 2  # Start decaying after 1 second of no data
SMOOTHING_FACTOR = 0.5  # Interpolation factor for smooth acceleration/deceleration (0-1, higher = faster response)


def parse_csc_measurement(data):
    """Parse Cycling Speed and Cadence measurement data and calculate speed."""
    global last_wheel_revs, last_wheel_time, target_joystick_y, last_speed_update
    
    flags = data[0]
    offset = 1
    
    result = {}
    
    # Check if wheel revolutions data is present
    if flags & 0x01:
        wheel_revs = struct.unpack_from('<I', data, offset)[0]
        wheel_event_time = struct.unpack_from('<H', data, offset + 4)[0]
        result['wheel_revolutions'] = wheel_revs
        result['wheel_event_time'] = wheel_event_time
        
        # Calculate speed from wheel revolutions
        if last_wheel_revs is not None and last_wheel_time is not None:
            rev_delta = wheel_revs - last_wheel_revs
            time_delta_ms = wheel_event_time - last_wheel_time
            
            # Handle timer wrap-around (16-bit, max 65535)
            if time_delta_ms < 0:
                time_delta_ms += 65536
            
            if time_delta_ms > 0:
                # Speed = distance / time
                distance = rev_delta * WHEEL_CIRCUMFERENCE
                time_delta_s = time_delta_ms / 1000.0
                speed_ms = distance / time_delta_s
                result['speed_ms'] = speed_ms

                # Normalize speed and set target joystick position (smoothing applied in main loop)
                normalized_speed = min(speed_ms / MAX_SPEED, 1.0)
                target_joystick_y = normalized_speed
                last_speed_update = asyncio.get_event_loop().time()
                
                logger.info(f"Speed: {speed_ms:.2f} m/s | Wheel Revs: {wheel_revs} | Target: {normalized_speed:.2f}")
            else:
                logger.info(f"Wheel Revolutions: {wheel_revs}, Event Time: {wheel_event_time}ms")
                target_joystick_y = 0.0
        else:
            logger.info(f"Wheel Revolutions: {wheel_revs}, Event Time: {wheel_event_time}ms (waiting for speed calculation)")
            target_joystick_y = 0.0
        
        last_wheel_revs = wheel_revs
        last_wheel_time = wheel_event_time
        offset += 6
    
    # Check if crank revolutions data is present
    if flags & 0x02:
        crank_revs = struct.unpack_from('<H', data, offset)[0]
        crank_event_time = struct.unpack_from('<H', data, offset + 2)[0]
        result['crank_revolutions'] = crank_revs
        result['crank_event_time'] = crank_event_time
        logger.info(f"Cadence - Crank Revolutions: {crank_revs}")
    
    return result


def notification_handler(sender, data):
    """Handle BLE notifications from the bike."""
    logger.info(f"Data received from {sender}: {data.hex()}")
    try:
        parsed = parse_csc_measurement(data)
    except Exception as e:
        logger.debug(f"Could not parse data: {e}")


async def connect_and_read_speed(device_address: str) -> None:
    """
    Connect to an exercise bike and listen for speed/cadence notifications.
    Automatically reconnects immediately if connection is lost.
    
    Args:
        device_address: Bluetooth MAC address of the bike
    """
    retry_count = 0
    
    while True:
        try:
            retry_count += 1
            logger.info(f"Connection attempt {retry_count}: Connecting to {device_address}...")
            
            async with BleakClient(device_address) as client:
                retry_count = 0  # Reset retry counter on successful connection
                logger.info("Connected successfully!")
                
                # Press 'A' on successful connection
                gamepad.press_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_A)
                gamepad.update()
                await asyncio.sleep(0.1)
                gamepad.release_button(button=vg.XUSB_BUTTON.XUSB_GAMEPAD_A)
                gamepad.update()

                # Flag to track if we should reconnect
                should_reconnect = False
                
                # Try to enable CSC notifications
                try:
                    # Get all characteristics to find the right one
                    services = client.services
                    csc_char = None
                    
                    for service in services:
                        if service.uuid == CYCLING_SPEED_CADENCE_SERVICE_UUID:
                            logger.info("Found Cycling Speed/Cadence Service")
                            for char in service.characteristics:
                                if char.uuid == CYCLING_SPEED_CADENCE_MEASUREMENT_CHAR_UUID:
                                    csc_char = char
                                    break
                    
                    if csc_char:
                        logger.info(f"Starting notifications on {csc_char.uuid}")
                        await client.start_notify(csc_char.uuid, notification_handler)
                        logger.info("Listening for speed/cadence data...")
                    else:
                        logger.warning("CSC Measurement characteristic not found")
                
                except Exception as e:
                    logger.warning(f"Could not set up CSC notifications: {e}")
                
                # Keep connection alive and monitor health
                try:
                    last_check = asyncio.get_event_loop().time()
                    global current_joystick_y, target_joystick_y, last_speed_update
                    
                    while True:
                        await asyncio.sleep(DECAY_CHECK_INTERVAL)  # Check at 60fps for smooth transitions
                        
                        now = asyncio.get_event_loop().time()
                        
                        # If no speed data received for SPEED_TIMEOUT, set target to 0
                        if last_speed_update is not None and (now - last_speed_update) > SPEED_TIMEOUT:
                            target_joystick_y = 0.0
                        
                        # Smooth interpolation towards target (handles both acceleration and deceleration)
                        if abs(current_joystick_y - target_joystick_y) > 0.001:  # Only update if difference is significant
                            current_joystick_y += (target_joystick_y - current_joystick_y) * SMOOTHING_FACTOR
                            gamepad.left_joystick_float(x_value_float=0.0, y_value_float=current_joystick_y)
                            gamepad.update()
                        elif current_joystick_y != target_joystick_y:
                            # Snap to target when very close
                            current_joystick_y = target_joystick_y
                            gamepad.left_joystick_float(x_value_float=0.0, y_value_float=current_joystick_y)
                            gamepad.update()
                        
                        # Check connection health periodically
                        if now - last_check >= CONNECTION_CHECK_INTERVAL:
                            last_check = now
                            
                            # Check if still connected
                            if not client.is_connected:
                                logger.error("Connection lost!")
                                should_reconnect = True
                                break
                
                except KeyboardInterrupt:
                    logger.info("\nInterrupted by user")
                    return
                except Exception as e:
                    logger.error(f"Error during connection: {e}")
                    should_reconnect = True
                
                # If connection was lost, loop will retry
                if should_reconnect:
                    logger.info("Reconnecting...")
        
        except Exception as e:
            logger.error(f"Connection failed: {e}")
            
            # Check if we should stop retrying
            if MAX_RETRIES is not None and retry_count >= MAX_RETRIES:
                logger.error(f"Maximum retries ({MAX_RETRIES}) reached. Giving up.")
                raise
            
            # Wait briefly before retrying
            logger.info(f"Retrying in {RECONNECT_DELAY} seconds...")
            await asyncio.sleep(RECONNECT_DELAY)


async def scan_for_devices() -> None:
    """Scan for available Bluetooth LE devices."""
    try:
        logger.info("Scanning for BLE devices (this may take a moment)...")
        devices = await BleakScanner.discover()
        
        if devices:
            logger.info(f"Found {len(devices)} device(s):")
            for device in devices:
                logger.info(f"  - {device.address}: {device.name if device.name else '(no name)'}")
        else:
            logger.info("No devices found")
    
    except Exception as e:
        logger.error(f"Error scanning: {e}")
        raise


async def main():
    """Main entry point."""
    import sys
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "scan":
            await scan_for_devices()
        else:
            # Use provided device address
            await connect_and_read_speed(sys.argv[1])
    else:
        # Use default device address
        logger.info(f"Using default device address: {DEFAULT_DEVICE_ADDRESS}")
        await connect_and_read_speed(DEFAULT_DEVICE_ADDRESS)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        logger.info("Program terminated by user.")
    finally:
        # Reset gamepad state on exit
        gamepad.reset()
        gamepad.update()
        logger.info("Gamepad reset and program exited.")

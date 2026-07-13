# Nearcade Godot 4.6 Binding

GDExtension wrapper for embedding Nearcade streaming into Godot 4.6+ games.

## Quick Start

```bash
# Build the SDK first
cd ../..
mkdir -p build && cd build
cmake .. -DNEARCADE_BUILD_SIGNALING=ON && make -j$(nproc)

# Build the GDExtension
cd ../bindings/godot
mkdir -p build && cd build
cmake .. -DCMAKE_PREFIX_PATH=../../build
make -j$(nproc)
```

Then open `demo/project.godot` in Godot 4.6 and run.

## API

The GDExtension exposes a `NearcadeSDK` singleton with these methods:

| Method | Description |
|---|---|
| `init(config: Dictionary)` | Initialize the SDK |
| `start_capture()` | Start screen capture |
| `stop_capture()` | Stop screen capture |
| `shutdown()` | Shut down all subsystems |
| `submit_gamepad(packet: Dictionary)` | Submit gamepad input |
| `submit_kbm(viewer_id, event_type, key, dx, dy)` | Submit keyboard/mouse input |
| `get_signaling_url()` | Get the WebSocket URL |
| `get_lan_ip()` | Get detected LAN IP |
| `get_pin()` | Get current PIN |
| `regenerate_pin()` | Generate new PIN |
| `flush_slot(slot: int)` | Reset a gamepad slot |
| `disconnect_viewer(viewer_id)` | Disconnect a viewer |

### Gamepad Packet Dictionary Format

```gdscript
{
    "type": 0x01,
    "lx": 0, "ly": -32767,  # left stick
    "rx": 0, "ry": 0,       # right stick
    "lt": 0, "rt": 0,       # triggers 0-255
    "buttons": 1,            # bitmask: A=1, B=2, Y=4, X=8, LB=16, RB=32, ...
    "hx": 0, "hy": 0,        # D-pad -1/0/1
    "slot": 0                # controller slot 0-15
}
```

### Signals (Event Callbacks)

| Signal | Payload |
|---|---|
| `viewer_joined(viewer_id, name)` | A viewer connected |
| `viewer_left(viewer_id)` | A viewer disconnected |
| `input_packet(packet, viewer_id)` | Raw input received |
| `signaling_message(viewer_id, data)` | WebRTC signaling data |
| `error(code, message)` | SDK error occurred |

## Build Dependencies

- Godot 4.6+ (with GDExtension support)
- godot-cpp (fetched automatically by CMake)
- libnearcade (from this repo)
- libwebsockets (optional, for signaling)

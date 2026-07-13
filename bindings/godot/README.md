# Nearcade Godot 4.6 Binding

GDExtension wrapper for embedding Nearcade P2P game streaming into Godot 4.6+ games.

## How it Works (P2P, no tunnels)

```
HOST (Godot game)                    VIEWER (browser)
       │                                    │
       │── WebSocket ──────────────────────→│  Signaling (SDP/ICE exchange)
       │←───────────────────────────────────│  via the embedded signaling server
       │                                    │
       │── WebRTC P2P ────────────────────→│  Video/audio stream (direct)
       │←───────────────────────────────────│  Gamepad input (direct)
       │                                    │
       │ uinput virtual gamepad ← viewer's  │
       │ keyboard/mouse/gamepad states      │
```

The embedded signaling server only handles the initial WebRTC handshake (offer/answer/ICE candidates). Once that's done, **all media and input data flows directly** between the host and viewer — no relay server, no tunnels.

If viewers are on a different network, the user provides their own connectivity (port forwarding, Tailscale, VPN, or a tunnel like cloudflared/zrok — the SDK does not bundle any).

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

The GDExtension exposes a `NearcadeSDK` class with these methods:

| Method | Description |
|---|---|
| `init(config: Dictionary)` | Initialize the SDK, start signaling server |
| `start_capture()` | Start screen capture (FFmpeg) |
| `stop_capture()` | Stop screen capture |
| `shutdown()` | Shut down all subsystems |
| `submit_gamepad(packet: Dictionary)` | Submit gamepad input |
| `submit_kbm(viewer_id, event_type, key, dx, dy)` | Submit keyboard/mouse input |
| `get_signaling_url()` | Get the WebSocket signaling URL |
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

### Signals

| Signal | Payload | When |
|---|---|---|
| `viewer_joined(viewer_id, name)` | Viewer connected via WebSocket | Viewer opens the page |
| `viewer_left(viewer_id)` | Viewer disconnected | Viewer closes/closes WebSocket |
| `signaling_message(viewer_id, data)` | Raw WebRTC SDP/ICE JSON | During WebRTC handshake |
| `error(code, message)` | SDK error | On failure |

The host is responsible for taking the `signaling_message` events, passing them to Godot's `WebRTCPeerConnection`, and calling `send_offer()`/`send_answer()`/`send_ice_candidate()` back through the SDK to relay to the viewer.

## Complete Host Flow (GDScript)

```gdscript
# 1. Init
var sdk = NearcadeSDK.new()
sdk.init({"port": 3000})
sdk.start_capture()

# 2. Handle WebRTC signaling
sdk.signaling_message.connect(func(viewer_id, data):
    var msg = JSON.parse_string(data)
    match msg.type:
        "offer":   host_pc.set_remote_description("offer", msg.sdp)
        "answer":  host_pc.set_remote_description("answer", msg.sdp)
        "ice":     host_pc.add_ice_candidate(msg.candidate)
)

# 3. Forward WebRTC answers back to viewer
host_pc.session_description_created.connect(func(type, sdp):
    sdk.send_answer(viewer_id, sdp)
)

# 4. In your _process(), poll for queued events
func _process(delta):
    var ev = sdk.poll_event()
    while not ev.is_empty():
        if ev.type == 4:  # signaling
            handle_signaling(ev.viewer_id, ev.data)
        ev = sdk.poll_event()
```

## Build Dependencies

- Godot 4.6+ (with GDExtension support)
- godot-cpp (fetched automatically by CMake)
- libnearcade (from this repo)
- libwebsockets (optional, for signaling)

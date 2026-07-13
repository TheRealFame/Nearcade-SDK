# Nearcade Streaming SDK

Standalone C ABI shared library for hosting **WebRTC P2P** video streams with remote gamepad input relay, designed for **multiplayer game streaming** from any game engine.

**v0.2.0 — WebRTC is now internal. The SDK handles ICE/DTLS/SRTP/SCTP via [libdatachannel](https://github.com/paullouisageneau/libdatachannel). Game engines just push frames and receive input.**

## Features

- **Internal WebRTC stack** — no browser/engine WebRTC API needed. SDK handles all P2P negotiation internally via libdatachannel.
- Lightweight **WebSocket signaling server** for viewer discovery (optional, libwebsockets)
- **Push rendered frames from your game engine** — not a screen grabber. Engines call `nearcade_send_h264()` every frame with their rendered output.
- **ViGEmBus on Windows** required (user-installed) with clear error guidance
- **Platform input injection**: Linux `uinput` (full), Windows `ViGEmBus` (requires install), macOS (stub)
- Runtime log control via `NEARCADE_LOG_LEVEL` env var
- **No tunnels** — pure P2P. Users provide their own WAN connectivity (Tailscale, port-forward, etc.)

## Engine Integrations

| Engine | Status | Path |
|--------|--------|------|
| Godot 4.6+ | ✅ Active | [`bindings/godot/`](bindings/godot/) |
| Unity | ⏳ Coming Soon | [`bindings/unity/`](bindings/unity/) |
| Unreal | ⏳ Coming Soon | [`bindings/unreal/`](bindings/unreal/) |

## How it Works

```
GAME ENGINE (your app)          VIEWER (browser)
      │                               │
      │── WebSocket (signaling) ─────→│  SDP + ICE handshake
      │←─────────────────────────────│  (SDK handles internally)
      │                               │
      │── WebRTC P2P (direct) ──────→│  H.264 video frames (engine-pushed)
      │←─────────────────────────────│  Gamepad input (16-byte packets)
      │                               │
      ▼                               ▼
  uinput/ViGEmBus            Browser Gamepad API
  (input injection)          or keyboard/mouse
```

The game engine calls `nearcade_send_h264()` each frame with encoded video. The SDK streams it to all connected viewers via WebRTC. Viewer gamepad input arrives via data channel and is injected into the host system.

### Three-lane P2P design

1. **Signaling lane** (WebSocket) — SDP/ICE handshake only. Runs on the embedded server (`ws://HOST:PORT`).
2. **Media lane** (WebRTC video track) — H.264 frames from engine → viewer. Direct P2P.
3. **Input lane** (WebRTC data channel) — Viewer gamepad → host. Direct P2P. Injected via uinput/ViGEmBus.

## Build

```sh
cmake -B build -DNEARCADE_BUILD_WEBRTC=ON
cmake --build build

# Run tests
NEARCADE_LOG_LEVEL=debug ctest --test-dir build
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `-DNEARCADE_BUILD_SHARED=ON` | ON | Build shared library |
| `-DNEARCADE_BUILD_SIGNALING=ON` | ON | Embedded WebSocket signaling (libwebsockets) |
| `-DNEARCADE_BUILD_WEBRTC=ON` | ON | Internal WebRTC via libdatachannel (requires C++17) |
| `-DNEARCADE_BUILD_TESTS=ON` | ON | Build unit tests |

## Quick Start (C)

```c
#include "nearcade.h"

int main() {
    nearcade_config cfg = NEARCADE_DEFAULT_CONFIG;
    nearcade_init(&cfg);
    nearcade_start_streaming();

    // Every frame: send your encoded H.264 video
    while (streaming) {
        uint8_t h264_data[] = { /* ... your NAL unit ... */ };
        nearcade_send_h264(h264_data, sizeof(h264_data), 0);
    }

    nearcade_stop_streaming();
    nearcade_shutdown();
    return 0;
}
```

## Quick Start (Godot)

```gdscript
var sdk = NearcadeSDK.new()
sdk.init({"port": 3000})
sdk.start_streaming()

func _process(delta):
    # Push your frame
    var nals = get_rendered_h264()  # from your encoder
    sdk.send_h264(nals, Time.get_ticks_usec())

    # Process events
    var ev = sdk.poll_event()
    while not ev.is_empty():
        if ev["type"] == 0:  # viewer joined
            print("Viewer: " + ev["viewer_id"])
        ev = sdk.poll_event()
```

## API

See `include/nearcade.h` for the complete C API. Key functions:

| Function | Purpose |
|----------|---------|
| `nearcade_init()` | Start SDK, signaling, input backends |
| `nearcade_start_streaming()` | Accept viewer connections |
| `nearcade_send_h264()` | Push encoded H.264 frame to all viewers |
| `nearcade_submit_gamepad()` | Inject gamepad state (from viewer input) |
| `nearcade_set_event_callback()` | Receive viewer join/leave events |

## Platform Input Backends

| Platform | Backend | Status |
|----------|---------|--------|
| Linux | uinput (kernel) | ✅ Full |
| Windows | ViGEmBus | ❌ Requires user install |
| macOS | — | ❌ No HID injection API available |

### Windows ViGEmBus

The Windows backend only works with [ViGEmBus](https://github.com/ViGEm/ViGEmBus/releases) installed. The SDK will log a clear warning and gracefully degrade (input calls become no-ops).

## Debug Logging

```sh
NEARCADE_LOG_LEVEL=trace ./my_app    # Everything (file:line)
NEARCADE_LOG_LEVEL=debug ./my_app    # Detailed state changes
NEARCADE_LOG_LEVEL=info  ./my_app    # Lifecycle events (default)
NEARCADE_LOG_LEVEL=warn  ./my_app    # Warnings and errors only
NEARCADE_LOG_LEVEL=error ./my_app    # Errors only
NEARCADE_LOG_LEVEL=none  ./my_app    # Silent
```

## License

MIT

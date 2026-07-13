# Nearcade Streaming SDK

Standalone C ABI shared library for hosting WebRTC peer-to-peer video/audio streams with remote gamepad input relay.

## Features

- Host a WebRTC video/audio stream from a capture source
- Relay gamepad input from multiple remote viewers back to the host
- Flat C API (`extern "C"`): `nearcade_init()`, `nearcade_start_capture()`, etc.
- Lightweight WebSocket signaling server (optional, requires libwebsockets)
- Platform backends: Linux (uinput), Windows (ViGEmBus stub), macOS (stub)
- Runtime log control via `NEARCADE_LOG_LEVEL` env var (trace/debug/info/warn/error/none)

## Engine Integrations

| Engine | Status | Path |
|---|---|---|
| Godot 4.6+ | ✅ Active | [`bindings/godot/`](bindings/godot/) |
| Unity | ⏳ Coming Soon | [`bindings/unity/`](bindings/unity/) |
| Unreal | ⏳ Coming Soon | [`bindings/unreal/`](bindings/unreal/) |

## Build

```sh
cmake -B build
cmake --build build

# Run tests
NEARCADE_LOG_LEVEL=debug ctest --test-dir build
```

### Options

| Flag | Default | Description |
|------|---------|-------------|
| `-DNEARCADE_BUILD_SHARED=ON` | ON | Build shared library (.so/.dylib/.dll) |
| `-DNEARCADE_BUILD_SIGNALING=ON` | ON | Embedded WebSocket signaling (requires libwebsockets) |
| `-DNEARCADE_BUILD_TESTS=ON` | ON | Build unit tests |

## API

See `include/nearcade.h` for the full C API.

```c
nearcade_config cfg = NEARCADE_DEFAULT_CONFIG;
nearcade_init(&cfg);
nearcade_start_capture();

nearcade_gamepad_packet pkt = {0};
pkt.type = 0x01;
pkt.ly = -32767;
pkt.buttons = NEARCADE_BTN_A;
nearcade_submit_gamepad(&pkt);

nearcade_poll_events(0);
nearcade_shutdown();
```

## Debug Logging

Set `NEARCADE_LOG_LEVEL` environment variable to control verbosity:

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

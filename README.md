# Nearcade Streaming SDK

Standalone C ABI shared library for hosting WebRTC peer-to-peer video/audio streams with remote gamepad input relay.

## Features

- Host a WebRTC video/audio stream from a capture source
- Relay gamepad input from multiple remote viewers back to the host
- Flat C API (`extern "C"`): `nearcade_init()`, `nearcade_start_capture()`, etc.
- Lightweight WebSocket signaling server (optional, requires libwebsockets)
- Platform backends: Linux (uinput), Windows (ViGEmBus stub), macOS (stub)

## Build

```sh
cmake -B build
cmake --build build
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

## License

MIT

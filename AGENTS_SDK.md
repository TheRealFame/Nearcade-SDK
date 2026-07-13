# Nearcade Streaming SDK — Agent Brief

This is a fresh repository for extracting Nearcade's core streaming engine into a standalone **C ABI shared library** (`.so`/`.dylib`/`.dll`) that game engines (Godot, Unity, Unreal) and custom apps can link against.

## What the SDK should do

- Host a WebRTC peer-to-peer video/audio stream from a capture source
- Relay gamepad input from multiple remote viewers back to the host
- Expose a **flat C API** (`extern "C"`): `nearcade_init()`, `nearcade_start_capture()`, `nearcade_stop_capture()`, `nearcade_poll_events()`, `nearcade_shutdown()`
- Bundle a lightweight signaling server (WebSocket) for offer/answer/ICE exchange
- Platform backends: Linux (uinput), Windows (ViGEmBus / HIDMaestro), macOS (stub)

## What it is NOT

- Not the full Nearcade Electron app (no UI, no arcade, no Discord, no auto-updater)
- Not a tunnel client (user provides their own zrok/cloudflared or LAN IP)

## First steps

1. Read the full Nearcade codebase at `../NearsecTogether` for reference architecture
2. Start with `src/sidecar/input_backends/InputOrchestrator.js` and `src/scripts/server.js` as the core to extract
3. Design the C API header (`include/nearcade.h`) first
4. Build system: **CMake** (cross-platform, no npm dependency)
5. License: MIT (no restrictions for consumers)

## Key design notes

- The protocol buffer format (16-byte gamepad packet) in `InputOrchestrator.js:12-24` must stay byte-exact for backward compat
- Viewer WebSocket signaling protocol lives in `server.js:2100-2260`
- The `CaptureManager.js` abstraction for screen/audio capture sources should be adapter-based
- No Node.js dependency for the final SDK — pure C/C++ with optional Python backends as sidecars

When the C API compiles on all 3 platforms, tag `v0.1.0`.

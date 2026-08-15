# platform/ — Platform abstraction layer

This directory is the Windows port's own platform layer, per the brief
(§34) and `docs/architecture.md`. The rule: **common upstream code stays
common; platform differences live behind these interfaces.**

Planned layout (implemented in Phases 2–3+):

```
platform/
├── include/                 # abstraction interfaces (headers only)
│   ├── capture.h            #   gsr_capture Windows backends (WGC, DXGI)
│   ├── audio.h              #   WASAPI device capture (sound_device_* impl)
│   ├── display.h            #   monitor/output enumeration, DPI, HDR state
│   ├── hotkeys.h            #   global hotkeys (RegisterHotKey / Raw Input)
│   ├── ipc.h                #   named-pipe transport (gsr-cli / gsr-ui-cli / RPC)
│   ├── filesystem.h         #   %APPDATA%/%LOCALAPPDATA% mapping, Unicode paths
│   ├── process.h            #   CreateProcess wrapper (UI spawns engine, -sc scripts)
│   ├── notifications.h      #   gsr-notification.exe invocation
│   ├── startup.h            #   HKCU Run / Startup folder
│   ├── time.h               #   QPC-based clock (CLOCK_MONOTONIC equivalent)
│   └── thread.h             #   pthread subset shim (mutex/thread/cond)
├── windows/                 # Windows implementations
│   ├── capture_wgc.c        #   Windows Graphics Capture backend
│   ├── capture_dxgi.c       #   DXGI Desktop Duplication backend
│   ├── audio_wasapi.c       #   WASAPI loopback/capture
│   ├── display_dxgi.c       #   monitor enumeration
│   ├── ipc_pipe.c           #   named-pipe server/client
│   ├── process.c, filesystem.c, time.c, thread.c, startup.c
│   └── render/              #   D3D11 or GL-on-Windows render/encode glue
├── linux/                   # (reference only; NOT built on Windows)
└── README.md                # this file
```

The engine/UI call these interfaces; Windows-only code lives under
`platform/windows/`. Nothing Linux-specific is compiled into the Windows
build. See `docs/architecture.md` for the design rationale and
`docs/upstream-porting-notes.md` for the file-by-file mapping.

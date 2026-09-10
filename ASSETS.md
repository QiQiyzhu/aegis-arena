# Asset and tool provenance

| Item | Origin | Distribution |
|---|---|---|
| Arena layout, debug layout, trace renderer | Original code authored for this project with AI assistance | MIT repository license |
| Native arena PNG/GIF | Actual UE 5.8.2 D3D12 captures; GIF samples thirteen frames at 0.5s intervals | Included, no fabricated engine imagery |
| Portable replay GIF/PNG | Generated from this project's actual compiled episode CSV using Pillow | Included; explicitly labelled non-Unreal |
| `/Engine/BasicShapes/Cube` reference | Unreal Engine bundled content | Source repository contains references; packaged runtime includes cooked Engine content under Unreal terms |
| BT/EQS/map/material binaries | Created and saved by the real UE 5.8.2 editor through project scripts | Ten original project assets included; Engine primitive references resolve from a licensed installation |
| Music / marketplace packs / third-party game art | Not used | None |
| Zig 0.15.2 portable compiler | Official ziglang.org download, SHA256 verified locally | `.tools/` ignored; not committed |
| Unreal / MSVC | User-installed UE 5.8.2; actual MSVC 14.50 / Windows SDK 26100 builds | Engine/toolchain binaries are not committed |

Verified local Zig archive SHA256: `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`. Official [download metadata](https://ziglang.org/download/index.json). The project can be built with an existing system Clang/GCC instead; no Zig runtime dependency is linked into the project API.

The trace PNG/GIF is diagnostic data visualization, not AI-generated artwork and not a mock Unreal screenshot. Pillow 11.3.0 generated the checked-in media. No external media was copied from another game.

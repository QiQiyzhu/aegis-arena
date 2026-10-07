# Asset and tool provenance

## v2.0 PRISM FALL additions

`scripts/unreal/create_v2_assets.py` authors three original faceted meshes (crystal shard, beveled armor, tapered pylon), a deterministic surface mask and two synthesized sound effects. Sources are under `Saved/V2AssetSource` and `assets/v2_source_audio`; the source manifest records their hashes. Unreal imports only into `/Game/Aegis/V2`. Four original material graphs provide basalt, ceramic, crystal and energy treatments. These assets are original procedural work, not artwork copied from the referenced games.

`-AegisV2` selects new scene and character presentation in `AegisPortfolioPresentation.cpp`. Decoration has no collision and does not affect navigation. Charged-shot and overclock audio are triggered by actual accepted actions; the film uses those logged events and the same original WAV files. Gameplay screenshots and video frames must come from the native engine; concept art is not substituted for a playable scene. Historical v1.5 assets and deliverables remain separate.

## v1.5 Portfolio additions

`AegisPortfolioPresentation.cpp` constructs original facility panels, floor tiles, machinery, cover trim and robot silhouettes using the same licensed Engine primitive references. `M_PortfolioMetal` and `M_PortfolioGlow` are project-authored material graphs with instancing support. These cosmetic components do not change collision or navigation. No external game artwork, generated fake gameplay, downloaded textures or marketplace pack was used.

`assets/source_audio/S_*.wav` contains six original deterministic synthesized effects (shot, impact, pulse, break, upgrade, repair). `scripts/unreal/create_portfolio_assets.py` contains their oscillator/envelope/noise recipes and imports them as project SoundWave assets. No samples from commercial games or music were used. The playable game triggers these sounds from actual actions; the portfolio video mixes the same WAVs from the engine's recorded trigger timestamps. This is explicitly an event-based post mix, not microphone or hardware loopback audio.

The portfolio MP4 uses native UE D3D11 frames, Chinese editorial explanatory cards, and this original event audio. Editorial cards are not game UI. The PDF/DOCX use original native screenshots and two explicitly labelled design diagrams. Microsoft YaHei is used through the installed local font for editorial rendering; the font file is not redistributed. Bundled ffmpeg / Python, isolated LibreOffice and Poppler render the video and documents; those authoring executables are not included in the game package.

## v2.1 audiovisual assets

`scripts/generate_v21_audio.py` synthesizes 24 original PCM16 / 48 kHz WAVs under `assets/v21_source_audio`: seven phase music pieces and seventeen effects. The original score uses a D-minor, 100 BPM motif, authored harmony, bass, arpeggios and synthesized percussion. Three shot variants per player/ally/enemy role are selected without consuming gameplay randomness. No commercial recordings, external samples or trained music model are used. `manifest.json` binds every source hash, and `scripts/unreal/import_v21_audio.py` imports them into `/Game/Aegis/V21/Audio` with explicit looping and playback settings.

Native v2.1 weapon presentation uses original code with a fixed 24-slot shard/muzzle pool and three existing portfolio tracer components per character. Effects use real shot and hit data; all added effect components have collision and navigation disabled. Existing Engine primitive meshes and the project's own crystal mesh/materials remain the asset sources. References to Returnal, VALORANT and Hades II are design research, not copied game assets or a claim that Aegis won an award; source links and transfer limits are in `docs/portfolio-v2.1-audiovisual.md`.

The v2.1 video reconstructs actual issued music and SFX commands from those original WAVs, retaining logged volumes, channel reuse, loop flags and fades. It is not hardware-loopback audio. Separate sound-enabled Unreal master-submix exports test audible signal, M mute/restore and lifecycle. Offscreen audio tests explicitly override unfocused muting and submix auto-disable; those test overrides are not part of normal launch. Documentary diagrams are labelled design diagrams; all gameplay illustrations are actual engine frames.

## Earlier versions

| Item | Origin | Distribution |
|---|---|---|
| Arena layout, debug layout, trace renderer | Original code authored for this project with AI assistance | MIT repository license |
| Native arena PNG/GIF | Actual UE 5.8.2 D3D12 captures; GIF samples thirteen frames at 0.5s intervals | Included, no fabricated engine imagery |
| Portable replay GIF/PNG | Generated from this project's actual compiled episode CSV using Pillow | Included; explicitly labelled non-Unreal |
| `/Engine/BasicShapes/{Cube,Sphere,Cylinder,Cone}` references | Unreal Engine bundled content; v1.1 combines them into distinct original role silhouettes | Source repository contains references; packaged runtime includes cooked Engine content under Unreal terms |
| `/Engine/EngineFonts/RobotoDistanceField` reference | Unreal Engine bundled font asset; baked glyphs used by the v1.1 Canvas HUD | Referenced and cooked with the game under the bundled font/Engine terms; not copied into the source repository |
| BT/EQS/map/material binaries | Created and saved by the real UE 5.8.2 editor through project scripts | Ten original assets plus the v1.1 Trial and v1.2 Tactical, Weapons and Operation isolated test maps; Engine primitive references resolve from a licensed installation |
| v1.2 enemy roles, relay ring, pulse and windup geometry | Original code combining the referenced Engine primitive meshes | Real runtime meshes, shared by Development and Shipping; no borrowed game art or diagnostic-only effect presented as shipped art |
| Music / marketplace packs / third-party game art | Not used | None |
| Zig 0.15.2 portable compiler | Official ziglang.org download, SHA256 verified locally | `.tools/` ignored; not committed |
| Unreal / MSVC | User-installed UE 5.8.2; actual MSVC 14.50 / Windows SDK 26100 builds | Engine/toolchain binaries are not committed |

Verified local Zig archive SHA256: `3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c`. Official [download metadata](https://ziglang.org/download/index.json). The project can be built with an existing system Clang/GCC instead; no Zig runtime dependency is linked into the project API.

The trace PNG/GIF is diagnostic data visualization, not AI-generated artwork and not a mock Unreal screenshot. Pillow 11.3.0 generated the checked-in media. No external media was copied from another game.

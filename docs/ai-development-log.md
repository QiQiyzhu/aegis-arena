# AI-assisted development and evidence log

## Authorship and responsibility

This project was implemented with substantial Codex AI assistance. The assistant authored the portable C++ rules/model, Unreal adapter source, test and reporting tools, primitive arena generator, debugging code and documentation. It also executed available local checks and reviewed their results. These activities do **not** imply that the user personally wrote every line, has mastered every API, or has completed an Unreal deployment.

Before using a resume bullet, the user should read the linked implementation, reproduce its results, explain a failure case, and make a small independently understood change. The [interview guide](interview-guide.md) lists ten source files and evidence-bounded wording. Do not present AI-generated architecture prose or uncompiled code as proof of engine experience.

## Work actually performed

| Stage | Implementation / observation | Evidence |
|---|---|---|
| Environment check | No usable Unreal or MSVC toolchain found. An old Unreal registry path was stale; engine installation was left to the user. | [Unreal setup and remaining gates](unreal-setup.md), [status](status.md) |
| Portable foundation | Health, hostile team filtering, cooldown, explicit PRNG, geometry/LOS, authorized observation, Utility scoring and hysteresis, bounded Director. | [Shared source](../core/include/aegis/rules.hpp) |
| Build and assertions | Real C++17 build through Clang 20.1.2 / Zig 0.15.2, `-O2 -Wall -Wextra -Werror -pedantic`. **325 assertions passed**, not 325 independent scenarios. | [Validation](../evidence/portable/validation.json), [build provenance](../evidence/portable/evaluation/report.json) |
| Integration checks | **Initially 7, now 9 Python test cases passed**, including actual compiled binary execution, strict scenario validation, JSON/CSV output and overwrite refusal. | [Test log](../evidence/portable/python-tests.txt) |
| Evaluation | **60 actual portable-model episodes**, paired seeds 1001–1030. Priority: 20 wins / 16 companion deaths; Utility: 19 wins / 6 companion deaths. Unfavorable win-rate result retained. | [Raw episode records](../evidence/portable/evaluation/episodes.json), [analysis](evaluation.md) |
| CPU sampling | **20 actual portable-model episodes** at initial 1/10/25/50 enemies. Engine timing and memory fields are null. | [Raw measurements](../evidence/portable/performance/episodes.json), [limits](performance.md) |
| Replay media | GIF/PNG rendered from an actual CSV trajectory; inspected visually and permanently labelled non-Unreal. | [CSV](../evidence/portable/trace.csv), [GIF](media/portable-trace.gif) |
| UE integration source | Runtime/editor modules, health/combat/characters, Perception/BT/EQS adapters, scenario actor, director observer, debug controls, five Automation tests and native functional-test actor. At the initial portable delivery, these were uncompiled. The subsequent UE 5.8.2 Game build now compiles; Editor and execution gates remain separate. | [Acceptance ledger](status.md) |
| Delivery tooling | Existing portable workflow expanded with explicit Ubuntu `g++`, least-privilege permissions, smoke/full scenarios and raw evidence artifact upload; added a fresh-clone verification entry point. | [Workflow](../.github/workflows/portable.yml), [one-command driver](../scripts/verify_portable.py) |

## A concrete correction made during review

The first Director implementation applied its population cap only when the policy adjustment cooldown elapsed. That could leave a stale nonzero spawn recommendation when the living-enemy cap had already been reached. The implementation now applies the hard capacity clamp on every valid update, even during the five-second policy cooldown. A regression assertion checks this exact case.

The pre-correction/generated working files were not used to replace final evidence. The committed validation report includes the final core source SHA256. Pure formatting was followed by rebuilding and refreshing final reports so those digests refer to the checked-in core.

## Initial portable checkpoint limitations (historical, superseded below)

- No completed Editor build, saved native asset generation, functional-map execution or Unreal Automation pass at that initial checkpoint; the Game Development target has separately compiled successfully.
- No actual UE gameplay video, BT screenshot, EQS score capture, Unreal Insights profile or Shipping launch validation. Game Development and Shipping have compiled; a separate eight-marker binary comparison passed.
- No Learning Agents training, imitation dataset, learned policy, training curve, RL improvement or commercial stability claim.
- Hosted CI was inspected after publication: run 34440960072 succeeded using Ubuntu GCC 13.3 and Python 3.11, running only the portable layer.

## Reproduction

The default `verify_portable.py` command performs a fresh strict build, assertions, all nine Python tests and two short smoke episodes. `--full` additionally runs the existing 60-episode evaluation configuration. The delivery check does not overwrite the earlier 60+20 reference evidence. All generated logs are labelled `portable-cpp-model`; the wrapper never invokes engine binaries.

The one-command path was also executed from a clean source copy containing neither `build/` nor `.tools/`, using an explicitly supplied external compiler. It passed the strict build, 325 assertions, seven Python test cases and two smoke episodes. Eleven generated/credential path patterns were checked with Git, and README/documentation local links were checked for missing targets. [Delivery check record](../evidence/portable/delivery-check.json). That clean-copy check was Windows; a later hosted Ubuntu/GCC Actions pass is separately linked above.

Local compilers, outputs and credential/environment files are excluded from Git. No secrets, downloaded toolchain binaries or Unreal content packs are needed to execute the portable workflow.

## Native UE 5.8 continuation

The user installed UE 5.8.2 on D:. The actual source headers, UHT and UBT were used to adapt the project; the code was not pinned to an older requested engine merely for convenience. Initial UBT failures identified missing Windows SDK and then the separate .NET Framework SDK requirement for Editor SwarmInterface. The Game target could proceed with the installed compiler while the Editor prerequisite was being repaired.

Real C++ compilation exposed member-shadowing warnings treated as errors, an omitted PathFollowing include, changed 5.8 Automation flag constants and explicit TObjectPtr access requirements. These were fixed rather than disabling warnings. `AegisArena Win64 Development` subsequently compiled and linked, including the native batch runner, bounded encounter-owner spawning and performance instrumentation. Raw UBT logs are in `evidence/unreal/environment/`.

The editor authoring implementation now builds real BB/BT/EQS graph structures and a real NavMesh volume brush through engine APIs; subsequent actual Editor compilation, generation and reload inspection all passed. Tactical geometry traces use a dedicated channel that ignores character capsules: otherwise the observed target inside the trace endpoint can make every candidate appear occluded. Source inspection does not substitute for the remaining spatial tests.

The full A–T [interview dossier](interview-dossier.md) was added during this continuation. It marks RAG/MCP/LLMOps as runtime N/A rather than inventing unrelated AI infrastructure. It includes the unfavorable portable policy outcomes and ten concrete code exercises.

C: disk pressure was handled by placing Binaries/Intermediate/Saved/DDC, UBA, temporary build files and local toolchains on D:. An old Windows runtime cache issue required a user-run official repair; automatic review rejected agent elevation. No alternate command was used to bypass that rejection. The residual empty C: outputs directory was left alone after cleanup was rejected; historical generated outputs remain on D:.

## Actual native asset and runtime debugging

The Editor module compiled and its five Core Automation tests passed. The initial Functional JSON-only success was later invalidated; the final PIE execution and all twelve assertions are verified below. Ten native assets were saved by UE and inspected after reloading. This supersedes the historical uncompiled checkpoint above; see the current acceptance ledger for packaging and media gates.

Actual failures changed the implementation: lock BT graph updates while creating the full tree, otherwise partial graph reconstruction removes children; initialize EQS node versions before saving, otherwise the legacy migration can invert a new distance score; count successful cover queries separately from all tactical queries; inject the companion policy before deferred actor spawning finishes. Explicit module dependencies exposed a missing Editor Json link dependency, which was fixed.

Presentation was checked by viewing actual engine PNGs. Python Rotator positional arguments use roll/pitch/yaw, so named arguments are required. A correct saved camera still did not fix the picture: choosing the first CameraActor can select PlayerCameraManager's animation camera. The final runtime uses the map's AegisEvaluationCamera tag and logs the actual player viewpoint. Movable lights remove the dependency on unbuilt lightmaps.

Reloading a commandlet-saved empty Recast actor exposed a cold-start failure. The runner now requests one asynchronous ANavigationData::RebuildAll after nav data exists, and waits with a wall-clock deadline. Synchronous NavigationSystem::Build was rejected after a real watchdog timeout; its wait-for-completion path is unsuitable for this game startup. The final rebuild completed in 0.03 seconds in the capture run; that one observation is not a general navigation performance claim.

The checked wrapper rejects missing completion markers even if Unreal returns process exit 0. Failed navigation and timed-out render runs remain outside successful evidence. DDC/shader warmup delay and fixed-game-time policy throughput are never called gameplay FPS. The implementation and documentation were AI-assisted; a candidate should rerun and understand the system before representing it as personal expertise.


## Final native corrections and verification

Independent review plus an explicit query diagnostic found a real spatial-context defect: Controller-owned EQS grids stayed at spawn because the default controller did not attach to its Pawn. In a measured query, the Controller was at (-1200,0,100) while the Pawn had moved to (-9.06,419.09,90.15); the Distance filter rejected every candidate. Setting bAttachToPawn=true preserves the Controller-based threat context. Tactical tasks also stop reporting unconditional Success when no query/move is viable, so the existing BT selector can fall back. Review caught the arrival boundary: an accepted tactical point remains viable after navigation reports Idle, until the next budgeted query replaces it; otherwise Retreat could immediately fall through to Follow on arrival. A matched short diagnostic changed from 5 failed queries / 11 to 0 / 9, without loosening the filter. All pre-fix 60 policy and 12 render rows remain in a separate historical evidence directory.

The post-fix 60 native episodes retain unfavorable policy results: both policies win 0/30; priority/utility companion deaths are 25/6, player damage taken 106.68/126.06, allied damage 170.46/139.40. EQS failures are 16/878 and 8/892. Better spatial correctness does not establish overall strategy superiority. These episodes are fixed game-time NullRHI execution, not rendered FPS.

A separate evidence failure was discovered by reading the Functional engine log: UE's Automation JSON said Success even though the wrong default world had no test actor. Explicitly loading the map exposed a second issue: an Editor-module fixture ran in an Editor World. IsEditorOnlyLoadedInPIE now requests a real begun-play PIE world. The final runner requires all twelve named assertion messages, an explicit AEGIS_FUNCTIONAL_PASS world marker, a clean report and no handled ensure. It actually passed ranged trace damage, cooldown, team filtering, death/healing, possession, moving Querier and three missing-query fallback checks. Two Python regression cases reject JSON-only success, missing assertions and handled ensures; combined with the seven original tool tests, the suite has nine cases.

No RL training, policy generalization, full Editor debugger screenshot, GPU Insights capture, memory-leak proof or commercial performance claim is implied by these results. The repository's current status and release record distinguish the Editor game measurements from the actual packaged executables.


Final Development and Shipping targets both completed cook/stage/archive. The standalone Development binary completed two validated episodes with no Editor process. The actual Shipping window rendered running AI with no debug HUD; the grave key did not open a console. Nine development markers were present in Development and absent from Shipping. Basic controls are implemented, but the final Shipping inspection is not described as exhaustive input coverage. Source, runtime binaries and archive checksums are linked by the release manifest.

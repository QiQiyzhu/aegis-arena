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
| Integration checks | **7 Python test cases passed**, including actual compiled binary execution, strict scenario validation, JSON/CSV output and overwrite refusal. | [Test log](../evidence/portable/python-tests.txt) |
| Evaluation | **60 actual portable-model episodes**, paired seeds 1001–1030. Priority: 20 wins / 16 companion deaths; Utility: 19 wins / 6 companion deaths. Unfavorable win-rate result retained. | [Raw episode records](../evidence/portable/evaluation/episodes.json), [analysis](evaluation.md) |
| CPU sampling | **20 actual portable-model episodes** at initial 1/10/25/50 enemies. Engine timing and memory fields are null. | [Raw measurements](../evidence/portable/performance/episodes.json), [limits](performance.md) |
| Replay media | GIF/PNG rendered from an actual CSV trajectory; inspected visually and permanently labelled non-Unreal. | [CSV](../evidence/portable/trace.csv), [GIF](media/portable-trace.gif) |
| UE integration source | Runtime/editor modules, health/combat/characters, Perception/BT/EQS adapters, scenario actor, director observer, debug controls, five Automation tests and native functional-test actor. **Not compiled or run in Unreal.** | [Acceptance ledger](status.md) |
| Delivery tooling | Existing portable workflow expanded with explicit Ubuntu `g++`, least-privilege permissions, smoke/full scenarios and raw evidence artifact upload; added a fresh-clone verification entry point. | [Workflow](../.github/workflows/portable.yml), [one-command driver](../scripts/verify_portable.py) |

## A concrete correction made during review

The first Director implementation applied its population cap only when the policy adjustment cooldown elapsed. That could leave a stale nonzero spawn recommendation when the living-enemy cap had already been reached. The implementation now applies the hard capacity clamp on every valid update, even during the five-second policy cooldown. A regression assertion checks this exact case.

The pre-correction/generated working files were not used to replace final evidence. The committed validation report includes the final core source SHA256. Pure formatting was followed by rebuilding and refreshing final reports so those digests refer to the checked-in core.

## What was not performed

- No Unreal compilation, UHT/UBT success, actual BT/EQS graph authoring, saved `.umap`, NavMesh build, functional-map execution or Unreal Automation pass.
- No actual UE gameplay video, BT screenshot, EQS score capture, Unreal Insights profile or Shipping build validation.
- No Learning Agents training, imitation dataset, learned policy, training curve, RL improvement or commercial stability claim.
- No hosted GitHub Actions success was assumed from writing YAML. The repository owner must inspect the real run after publication.

## Reproduction

The default `verify_portable.py` command performs a fresh strict build, assertions, all seven Python tests and two short smoke episodes. `--full` additionally runs the existing 60-episode evaluation configuration. The delivery check does not overwrite the earlier 60+20 reference evidence. All generated logs are labelled `portable-cpp-model`; the wrapper never invokes engine binaries.

The one-command path was also executed from a clean source copy containing neither `build/` nor `.tools/`, using an explicitly supplied external compiler. It passed the strict build, 325 assertions, seven Python test cases and two smoke episodes. Eleven generated/credential path patterns were checked with Git, and README/documentation local links were checked for missing targets. [Delivery check record](../evidence/portable/delivery-check.json). This local Windows check does not claim that Ubuntu/GCC or hosted Actions has already run.

Local compilers, outputs and credential/environment files are excluded from Git. No secrets, downloaded toolchain binaries or Unreal content packs are needed to execute the portable workflow.

# Local squad planner feasibility — preserved negative result

This run tested the real CPU model with the proposed native planner schema and six English/Chinese commands. JSON formatting succeeded 6/6, target grounding succeeded 1/6, and **grounded plans containing the requested primary skill succeeded 0/6**. The one grounded plan attacked when asked to protect the player. These six examples are a narrow integration check, not a general benchmark. No regex or manual plan repair was applied. No Unreal workload was launched or measured.

The user subsequently chose a cloud model. The larger local model was **not downloaded**. Its public metadata only is retained. The local server is being stopped; see server-stop.json for verified process ownership and completion.

## Runtime and official artifacts

- Hardware: Intel Core i9-13900HX; generation and batch threads 4; CPU only (`-ngl 0`); context 2048; one server slot.
- Official stable llama.cpp `v0.4.0` maps through its `nightly-tag.txt` to binary release `b10809`, commit `5266f24da75dc449bd56cbed7addb9c8e4a6a73e`.
- CPU x64 ZIP: 18,407,457 bytes; SHA-256 `9df3158ed228a641a4b127942d7f459f24c9e13f04682659d05c00c80099b6b5`.
- Official Qwen3-0.6B GGUF revision `23749fefcc72300e3a2ad315e1317431b06b590a`; Q8_0 is the only quantization in that official repository.
- Model: 639,446,688 bytes; SHA-256 `9465e63a22add5354d9bb4b99e90117043c7124007664907259bd16d043bb031`.
- Both file sizes and hashes matched publisher metadata before execution. URLs/revisions and extracted executable hashes are in artifact-provenance.json.
- Original MIT llama.cpp license and Apache-2.0 Qwen license are retained alongside pinned official READMEs. No global installation, PATH change, API key, or credentials were used.

## Exact startup

```text
D:/AegisWork/AI/v1.3/llama-cpu/llama-server.exe -m D:/AegisWork/AI/v1.3/Qwen3-0.6B-Q8_0.gguf --alias Qwen3-0.6B --host 127.0.0.1 --port 18765 -ngl 0 -c 2048 -t 4 -tb 4 --parallel 1 --threads-http 2 --poll 0 --reasoning off
```

PowerShell Start-Process used `-WindowStyle Hidden`, redirected stdout/stderr, and saved owned PID 31020. Server log reports listening after 1.355075 seconds. Snapshot after inference: working set 1,157,668,864 bytes, private memory 575,774,720 bytes (not peak memory).

## Actual API and latency

POST `/v1/chat/completions`, alias `Qwen3-0.6B`, temperature 0.1, max_tokens 384, stream false, chat_template_kwargs.enable_thinking false. OpenAI-style response_format.json_schema was accepted. Exact prompts, constrained schema, observations, native HTTP responses, output tokens, and server timing fields remain in inference-run02/.

Run02 reused prompt caches: 1,054.308–2,342.301 ms HTTP latency, median 1,784.080 ms. These are cached/partly cached requests, not cold figures. The first actual uncached request from the initial partial run took 2,456.0813 ms (267 prompt tokens, cached 0; response retained in inference/capture_en.response.json). That initial harness stopped while printing Chinese to a cp932 console; the follow-up run fixed reporting and completed all six. No response was discarded or repaired.

| Case | HTTP ms | Schema valid | Grounded | Original steps |
|---|---:|---|---|---|
| capture_en | 1644.358 | True | False | [{"skill": "capture_relay", "target": "t0"}, {"skill": "focus_visible", "target": "t0"}] |
| guard_en | 1054.308 | True | True | [{"skill": "focus_visible", "target": "t0"}] |
| focus_en | 1554.650 | True | False | [{"skill": "focus_visible", "target": "t0"}, {"skill": "capture_relay", "target": "t0"}] |
| capture_zh | 2342.301 | True | False | [{"skill": "focus_visible", "target": "t0"}, {"skill": "capture_relay", "target": "t0"}, {"skill": "guard", "target": "t0"}] |
| guard_zh | 1923.802 | True | False | [{"skill": "capture_relay", "target": "t0"}, {"skill": "focus_visible", "target": "t0"}] |
| focus_zh | 1963.287 | True | False | [{"skill": "focus_visible", "target": "t0"}, {"skill": "capture_relay", "target": "t0"}] |

## Interpretation

The small model runs quickly enough for an asynchronous request, but this original prompt/model combination did not demonstrate acceptable command reliability. The strict native validator must continue rejecting invalid target combinations. Structural validity alone cannot detect a legal but unwanted attack plan. The six heldout paraphrases were frozen before proposed prompt tuning but were not used to claim reliability. Review cloud-model choices using the same raw-evidence discipline.

## One revised prompt, same six tuning cases

The agreed single revision added explicit skill-target rules, intent/order instructions and four examples. Original requests were replayed once: schema 6/6, grounding 6/6, but **complete requested sequence only 4/6**. The English capture plan omitted the subsequent guard step; the English attack plan omitted regroup. Both corresponding Chinese sequences were correct. HTTP latency was 1,489.277–2,777.548 ms, median 1,789.144 ms, with prompt caching. `desiredSkillPresentCount=6` in the raw summary measures only the primary skill and must not be presented as complete command accuracy. `revision-analysis.json` records the full-sequence check.

The heldout paraphrases were not evaluated, and no larger model was downloaded after the user selected cloud. Owned server PID 31020 was stopped at 2026-09-11T06:45:56.8806253Z after executable/start-time verification; the process and port 18765 listener were both absent. See server-stop.json. All raw requests, responses and measurements remain unchanged.

## Cloud selection evidence: twelve real API requests

After the user chose a cloud provider and configured a key, `deepseek-flash` was evaluated with the actual runtime system prompt and appended schema. **All 12 single-attempt requests passed JSON format, legal skills/targets and the complete predeclared ordered intent: 6/6 existing tuning commands and 6/6 new heldout paraphrases.** English and Chinese no-visible-hostile cases both produced `guard` with target `none`; their labels explicitly stated that no visible target was available. Median HTTP latency was 1,021.077 ms (range 775.161–1,537.949 ms). These were synthetic snapshots with runtime fields, not gameplay execution or a general reliability guarantee. No training, retries or response repair occurred.

The complete ordered skill/target objects were independently reread from all raw provider responses; this confirmed the result does not count only the first requested skill. Both no-visible labels were separately reviewed for meaning. The evaluator's automatic label checks cover ASCII/length, while the semantic label check for those two cases is documented in [classification-review.json](D:/AegisWork/AI/v1.3/cloud-evaluation-01/classification-review.json).

The user's cloud preference, the local model's preserved 4/6 complete-intent result after one prompt revision, and the cloud model's 12/12 result support the selected cloud implementation. This is model-selection evidence from small task-specific samples, not a controlled general model ranking. Runtime plan validation and visible-target revalidation remain necessary.

See [the cloud evaluation report](D:/AegisWork/AI/v1.3/cloud-evaluation-01/RESULTS.md), [machine-readable results with every provider response ID/model](D:/AegisWork/AI/v1.3/cloud-evaluation-01/report.json), and [the preregistered cases](D:/AegisWork/AI/v1.3/cloud-evaluation-01/declared-cases.json). The local negative results and original response files above remain unchanged.

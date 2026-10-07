# Actual cloud semantic evaluation

All 12 single-attempt HTTPS responses were valid JSON, legal skills with allowed targets, and matched the complete predeclared skill sequence: 6/6 existing tuning commands and 6/6 new heldout paraphrases. English and Chinese cases included explicit no-visible-hostile conditions, which correctly produced guard plans.

Requested model and every provider response model: `deepseek-flash`. Overall HTTP latency: 775.161–1537.949 ms; median 1021.077 ms. Heldout median: 1021.077 ms. Server response IDs, usage and all 12 request/response bodies remain in this directory.

## Scope and method

The system prompt and appended schema were copied unchanged from the actual runtime fixture request. Cloud adaptation changed only the configured model, removed the local chat-template flag and added `thinking: {type: disabled}`. Temperature 0.1, max_tokens 384, stream false and json_object output were retained. Observations use the runtime field structure with explicitly synthetic visible-hostile snapshots. This measures command-to-plan semantics, not Unreal execution, combat balance or general reliability. No training, retries, response repair, or post-response expectation changes occurred.

All cases, acceptable complete sequences, observations and request bodies were saved before the first API call in `declared-cases.json`. Guard accepts either guard alone or regroup followed by guard; capture requires capture_relay then guard; visible attack requires focus_visible then regroup. Conditional attack with no visible target requires guard. Merely containing the first requested skill is insufficient.

Credentials were decrypted privately with current-user DPAPI and used only in in-memory HTTP authorization. Redirects were disabled, each call had a 20-second timeout, and request/response headers were never saved. All responses were scanned for the exact credential and header fields; none required redaction, so the saved raw response bytes are unchanged. Original and saved response hashes match. The evaluator performs defensive redaction if a future provider echoes credentials.

## Results

| Case | JSON | Legal skills/targets | Full intent | HTTP ms | Response ID |
|---|---|---|---|---:|---|
| tuning_capture_en | True | True | True | 1194.495 | 100be7fc-e960-4477-8bf8-71010a2d9a04 |
| tuning_guard_en | True | True | True | 775.161 | 500b762d-f6ec-42c7-8e5f-df4da6eb18bf |
| tuning_focus_en | True | True | True | 1422.025 | c3ddb150-8d5e-4e03-afed-f070f89555a9 |
| tuning_capture_zh | True | True | True | 1092.328 | a0944359-6c2f-41ea-8b87-16902bb4140a |
| tuning_guard_zh | True | True | True | 813.117 | c8874c90-30ec-4200-a2bc-4f1ed20ba114 |
| tuning_focus_zh | True | True | True | 777.269 | 3f52d076-3ec6-462d-824a-0d64d597de5a |
| heldout_capture_en | True | True | True | 1088.687 | c04c0514-b399-4f4b-86c2-e10a871bcb6e |
| heldout_guard_en | True | True | True | 1055.609 | c4adc01d-3605-4fd0-9f76-a9a12adf56a2 |
| heldout_no_visible_en | True | True | True | 986.544 | b3e3c715-1b91-473b-9241-860d5828a005 |
| heldout_capture_zh | True | True | True | 1537.949 | df19e85a-aa56-4bb0-bd53-44b85ac09fa5 |
| heldout_guard_zh | True | True | True | 820.440 | 2ed6a7b0-b991-40bf-bfea-38e136e92958 |
| heldout_no_visible_zh | True | True | True | 944.934 | 22231567-2f41-4587-893f-379896f5c64c |

Usage reported by the provider: `{"prompt_tokens": 8471, "completion_tokens": 351, "total_tokens": 8822, "prompt_cache_hit_tokens": 4224, "prompt_cache_miss_tokens": 4247}`. These are actual token counts, not a price estimate.

Frozen declaration SHA-256: `8a5659cd350bd96a867279f2cbdfbe9f09bd949a37c4a07fd7555de477eed3e8`.
Evaluator SHA-256 at evaluation: `10cf9e230f33232942d04665312e54af12313fefcb28a6739c53efad4919286f`.

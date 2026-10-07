"""Twelve predeclared real cloud requests; save separate format, legality and intent evidence."""
import argparse
import copy
import datetime
import hashlib
import json
from pathlib import Path
import re
import statistics
import time
import urllib.error
import urllib.request

from aegis_ai_config import build_environment, _NoRedirect

SKILLS = {"guard", "capture_relay", "focus_visible", "regroup"}
TOKENS = {"none", *(f"t{i}" for i in range(6))}


def strict_json_loads(content):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON key")
            result[key] = value
        return result
    def reject_constant(_value):
        raise ValueError("Non-finite JSON constant")
    return json.loads(content, object_pairs_hook=unique_object, parse_constant=reject_constant)


def declared_cases(observation):
    rows = [
        ("tuning_capture_en", "tuning", "Secure the relay, then cover me.", "capture", True),
        ("tuning_guard_en", "tuning", "Stay close and protect me.", "guard", True),
        ("tuning_focus_en", "tuning", "Attack the visible threat, then regroup.", "focus", True),
        ("tuning_capture_zh", "tuning", "先控制信号中继，再掩护我。", "capture", True),
        ("tuning_guard_zh", "tuning", "靠近我并保护我。", "guard", True),
        ("tuning_focus_zh", "tuning", "攻击视野中的敌人，然后重新集结。", "focus", True),
        ("heldout_capture_en", "heldout", "Take control of the current relay before you cover my position.", "capture", True),
        ("heldout_guard_en", "heldout", "Do not chase the hostile you can see. Stay with me and watch my back.", "guard", True),
        ("heldout_no_visible_en", "heldout", "Engage the threat if you can see it. If no hostile is visible, guard me instead.", "guard", False),
        ("heldout_capture_zh", "heldout", "把当前中继站拿下，随后保护我。", "capture", True),
        ("heldout_guard_zh", "heldout", "别去追你看见的敌人，跟着我并提供掩护。", "guard", True),
        ("heldout_no_visible_zh", "heldout", "优先打击你能看见的敌人；若没有可见敌人，就留在我身边保护我。", "guard", False),
    ]
    expected = {"capture": [["capture_relay", "guard"]], "guard": [["guard"], ["regroup", "guard"]],
                "focus": [["focus_visible", "regroup"]]}
    result = []
    for name, split, instruction, intent, visible in rows:
        snapshot = copy.deepcopy(observation)
        snapshot["visible_hostiles"] = [{"target": "t0", "distance_cm": 800, "health_fraction": 0.5}] if visible else []
        assert len(instruction.encode("utf-8")) <= 512
        result.append(dict(name=name, split=split, instruction=instruction, observation=snapshot,
                           acceptableSkillSequences=expected[intent],
                           expectation="Honor requested skill order, with no unrelated attack or invented target"))
    return result


def classify(content, case):
    verdict = dict(validJson=False, schemaValid=False, skillNamesLegal=False, targetsLegal=False,
                   skillsLegal=False, fullIntentCorrect=False)
    try:
        plan = strict_json_loads(content)
    except (TypeError, ValueError):
        return verdict, None
    verdict["validJson"] = True
    if not isinstance(plan, dict) or set(plan) != {"label", "steps"}:
        return verdict, plan
    label, steps = plan["label"], plan["steps"]
    shape = (isinstance(label, str) and 1 <= len(label) <= 80 and re.fullmatch(r"[ -~]+", label) and
             isinstance(steps, list) and 1 <= len(steps) <= 3 and all(
                 isinstance(step, dict) and set(step) == {"skill", "target"} and
                 isinstance(step["skill"], str) and isinstance(step["target"], str) for step in steps))
    if not shape:
        return verdict, plan
    verdict["skillNamesLegal"] = all(step["skill"] in SKILLS for step in steps)
    verdict["schemaValid"] = verdict["skillNamesLegal"] and all(step["target"] in TOKENS for step in steps)
    visible = {target["target"] for target in case["observation"]["visible_hostiles"]}
    verdict["targetsLegal"] = all(
        step["target"] in visible if step["skill"] == "focus_visible" else step["target"] == "none" for step in steps)
    objective_legal = not (case["observation"]["objective"]["complete"] and any(
        step["skill"] == "capture_relay" for step in steps))
    verdict["skillsLegal"] = verdict["schemaValid"] and verdict["targetsLegal"] and objective_legal
    verdict["fullIntentCorrect"] = verdict["skillsLegal"] and [step["skill"] for step in steps] in case["acceptableSkillSequences"]
    return verdict, plan


def redact_response(raw, secret):
    """Keep provider JSON/content intact except actual credential strings and header fields."""
    text = raw.decode("utf-8", errors="replace")
    if secret:
        text = text.replace(secret, "[REDACTED]")
    text = re.sub(r"(?i)(Bearer\s+)[A-Za-z0-9._~+/=-]+", r"\1[REDACTED]", text)
    try:
        value = json.loads(text)
        def visit(item):
            if isinstance(item, dict):
                return {key: "[REDACTED]" if key.casefold() in {
                    "authorization", "api_key", "api-key", "apikey", "headers", "request_headers", "response_headers"
                } else visit(child) for key, child in item.items()}
            if isinstance(item, list):
                return [visit(child) for child in item]
            return item
        cleaned = visit(value)
        if cleaned != value:
            text = json.dumps(cleaned, ensure_ascii=False)
    except ValueError:
        pass
    return text.encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runtime-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--config", type=Path)
    args = parser.parse_args()
    settings = build_environment(args.config, base_env={})
    if settings["AEGIS_AI_PROVIDER"] != "deepseek" or settings["AEGIS_AI_FORMAT"] != "json_object":
        raise ValueError("This declared evaluation requires the configured DeepSeek JSON provider")
    endpoint, secret = settings["AEGIS_AI_ENDPOINT"], settings["AEGIS_AI_API_KEY"]
    raw_fixture = args.runtime_log.read_bytes()
    template = json.loads(raw_fixture.decode("utf-8-sig").splitlines()[0])["request"]
    if set(template) - {"model", "temperature", "max_tokens", "stream", "messages", "response_format", "chat_template_kwargs", "thinking"}:
        raise ValueError("Unexpected fields in runtime request body")
    system = next(message["content"] for message in template["messages"] if message["role"] == "system")
    runtime_user = json.loads(next(message["content"] for message in template["messages"] if message["role"] == "user"))
    if "The exact JSON schema is:" not in system or secret in json.dumps(template):
        raise ValueError("Runtime schema is missing or fixture contains credential material")
    cases = declared_cases(runtime_user["observation"])
    requests = []
    for case in cases:
        payload = copy.deepcopy(template)
        payload.update(model=settings["AEGIS_AI_MODEL"], thinking={"type": "disabled"})
        payload.pop("chat_template_kwargs", None)
        payload["messages"] = [{"role": "system", "content": system}, {"role": "user", "content": json.dumps(
            {"instruction": case["instruction"], "observation": case["observation"]}, ensure_ascii=False)}]
        requests.append(payload)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    declared = dict(schemaVersion=1, declaredAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                    runtimeFixtureSha256=hashlib.sha256(raw_fixture).hexdigest(),
                    runtimeSystemSha256=hashlib.sha256(system.encode()).hexdigest(), model=settings["AEGIS_AI_MODEL"],
                    provider=settings["AEGIS_AI_PROVIDER"], endpoint=endpoint, cases=cases, requests=requests,
                    scope="Six existing tuning commands and six new heldout paraphrases; synthetic observations with runtime field shape. No model training, no Unreal execution.",
                    perCaseRequests=1, timeoutSeconds=20)
    declaration = json.dumps(declared, ensure_ascii=False, indent=2).encode("utf-8")
    if secret.encode("utf-8") in declaration:
        raise ValueError("Credential material appeared in evaluation metadata; refusing to save it")
    (output / "declared-cases.json").write_bytes(declaration)
    for case, payload in zip(cases, requests):
        (output / (case["name"] + ".request.json")).write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")
    print("Declared all 12 cases and requests before the first API call.", flush=True)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), _NoRedirect())
    results = []
    for case, payload in zip(cases, requests):
        request = urllib.request.Request(endpoint, json.dumps(payload, ensure_ascii=False).encode("utf-8"),
                                         {"Content-Type": "application/json", "Authorization": "Bearer " + secret})
        started = time.perf_counter()
        row = dict(case=case["name"], split=case["split"], httpStatus=None)
        raw = b""
        try:
            with opener.open(request, timeout=20) as response:
                row["httpStatus"] = response.status
                raw = response.read(1048577)
                if len(raw) > 1048576:
                    raise ValueError("Response size limit exceeded")
        except urllib.error.HTTPError as error:
            row["httpStatus"] = error.code
            raw = error.read(1048576)
            error.close()
            row["error"] = "Provider HTTP failure"
        except (OSError, ValueError):
            row["error"] = "Transport timeout, connection failure or oversized response"
        row["latencyMilliseconds"] = (time.perf_counter() - started) * 1000
        safe = redact_response(raw, secret)
        (output / (case["name"] + ".response.json")).write_bytes(safe)
        row.update(rawResponseSha256=hashlib.sha256(raw).hexdigest(), savedResponseSha256=hashlib.sha256(safe).hexdigest(),
                   credentialRedactionApplied=safe != raw)
        content = None
        try:
            strict_json_loads(raw)  # Redaction must never hide an invalid original envelope.
            answer = strict_json_loads(safe)
            row.update(responseId=answer.get("id"), responseModel=answer.get("model"), usage=answer.get("usage"))
            choice = answer["choices"][0]
            row["finishReason"] = choice.get("finish_reason")
            content = choice["message"]["content"]
        except (ValueError, KeyError, IndexError, TypeError):
            row.setdefault("error", "Response did not contain a chat completion")
        verdict, plan = classify(content, case)
        row.update(verdict, plan=plan)
        if row["httpStatus"] != 200 or row.get("finishReason") != "stop":
            row["fullIntentCorrect"] = False
        results.append(row)
        (output / (case["name"] + ".measurement.json")).write_text(json.dumps(row, ensure_ascii=False, indent=2), encoding="utf-8")
        print(json.dumps({key: row[key] for key in ("case", "httpStatus", "latencyMilliseconds", "validJson", "skillsLegal", "fullIntentCorrect")}), flush=True)
    summaries = {}
    for split in ("tuning", "heldout", "all"):
        selected = [row for row in results if split == "all" or row["split"] == split]
        summaries[split] = {key: sum(bool(row[key]) for row in selected) for key in (
            "validJson", "schemaValid", "skillNamesLegal", "targetsLegal", "skillsLegal", "fullIntentCorrect")}
        summaries[split].update(count=len(selected), latencyMilliseconds={
            "min": min(row["latencyMilliseconds"] for row in selected),
            "median": statistics.median(row["latencyMilliseconds"] for row in selected),
            "max": max(row["latencyMilliseconds"] for row in selected)})
    report = dict(schemaVersion=1, completedAtUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  declarationSha256=hashlib.sha256(declaration).hexdigest(), requestCount=len(results),
                  requestedModel=settings["AEGIS_AI_MODEL"], responseModels=sorted({row["responseModel"] for row in results if row.get("responseModel")}),
                  scope=declared["scope"], summary=summaries, results=results,
                  limits="Small preregistered semantic evaluation; not a gameplay, balance or general reliability guarantee. No retries or output repair.")
    (output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Completed 12 single-attempt requests; report.json retains separate tuning and heldout results.", flush=True)


if __name__ == "__main__":
    main()

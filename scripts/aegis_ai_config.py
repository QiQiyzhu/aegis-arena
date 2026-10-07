"""Local provider settings. Save uses current-user Windows DPAPI; only Test calls an API."""
import base64
import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import re
import threading
import urllib.error
import urllib.parse
import urllib.request

DEFAULT_ENDPOINT = "https://api.deepseek.com/chat/completions"
DEFAULT_MODEL = "deepseek-flash"
PROVIDERS = ("deepseek", "custom", "local")
ENV_NAMES = ("AEGIS_AI_ENDPOINT", "AEGIS_AI_MODEL", "AEGIS_AI_API_KEY", "AEGIS_AI_FORMAT", "AEGIS_AI_PROVIDER")


def default_config_path():
    local = os.environ.get("LOCALAPPDATA")
    if not local:
        raise ValueError("Windows LOCALAPPDATA is unavailable")
    return Path(local) / "AegisArena" / "ai-provider.json"


def validate_endpoint(endpoint, provider):
    if provider not in PROVIDERS or not isinstance(endpoint, str) or not 1 <= len(endpoint) <= 512:
        raise ValueError("Invalid provider or endpoint")
    if any(ord(c) <= 32 or ord(c) >= 127 for c in endpoint) or "\\" in endpoint:
        raise ValueError("Endpoint must be an ASCII URL without spaces")
    try:
        url = urllib.parse.urlsplit(endpoint)
        port = url.port
    except ValueError:
        raise ValueError("Invalid endpoint URL") from None
    if port is not None and not 1 <= port <= 65535:
        raise ValueError("Endpoint port must be between 1 and 65535")
    if (not url.hostname or url.username is not None or url.password is not None or
            url.query or url.fragment or "?" in endpoint or "#" in endpoint or
            not url.path.startswith("/") or not re.fullmatch(r"[A-Za-z0-9.-]+", url.hostname)):
        raise ValueError("Endpoint must have a host and path, without credentials, query or fragment")
    if url.scheme != "https" and not (provider == "local" and url.scheme == "http" and url.hostname == "127.0.0.1"):
        raise ValueError("Use HTTPS, or explicit http://127.0.0.1 for the local provider")
    if provider == "local" and url.hostname != "127.0.0.1":
        raise ValueError("Local provider must use 127.0.0.1")
    if provider == "deepseek" and (url.hostname != "api.deepseek.com" or port not in (None, 443)):
        raise ValueError("DeepSeek provider requires https://api.deepseek.com")
    return endpoint


def validate_config(config):
    if not isinstance(config, dict):
        raise ValueError("Invalid provider settings")
    provider = config.get("provider", "deepseek")
    endpoint = validate_endpoint(config.get("endpoint"), provider)
    model, output_format = config.get("model"), config.get("format", "json_object")
    if not isinstance(model, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:/-]{0,127}", model):
        raise ValueError("Model must be a short provider model identifier")
    if output_format not in ("json_object", "json_schema"):
        raise ValueError("Choose json_object or json_schema")
    if provider == "deepseek" and output_format != "json_object":
        raise ValueError("DeepSeek Chat requires json_object output")
    encrypted = config.get("key_dpapi", "")
    if not isinstance(encrypted, str) or len(encrypted) > 16384:
        raise ValueError("Invalid encrypted key")
    return dict(endpoint=endpoint, model=model, provider=provider, format=output_format, key_dpapi=encrypted)


def _key_bytes(key):
    if not isinstance(key, str) or len(key) > 4096 or any(ord(c) < 33 or ord(c) > 126 for c in key):
        raise ValueError("API key must contain printable characters without spaces")
    return key.encode("ascii")


def _dpapi(data, decrypt=False):
    if os.name != "nt":
        raise OSError("Encrypted provider settings require Windows DPAPI")
    class Blob(ctypes.Structure):
        _fields_ = [("size", wintypes.DWORD), ("data", ctypes.POINTER(ctypes.c_ubyte))]
    buffer = ctypes.create_string_buffer(data)
    source = Blob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte)))
    target = Blob()
    crypt = ctypes.WinDLL("crypt32", use_last_error=True)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    fn = crypt.CryptUnprotectData if decrypt else crypt.CryptProtectData
    fn.argtypes = [ctypes.POINTER(Blob), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p,
                   ctypes.c_void_p, wintypes.DWORD, ctypes.POINTER(Blob)]
    fn.restype = wintypes.BOOL
    kernel.LocalFree.argtypes, kernel.LocalFree.restype = [ctypes.c_void_p], ctypes.c_void_p
    if not fn(ctypes.byref(source), None, None, None, None, 1, ctypes.byref(target)):
        raise OSError("Windows could not unlock/store this user's API key")
    try:
        return ctypes.string_at(target.data, target.size)
    finally:
        kernel.LocalFree(target.data)


def protect_key(key):
    data = _key_bytes(key)
    return base64.b64encode(_dpapi(data)).decode("ascii") if data else ""


def unprotect_key(encoded):
    if not encoded:
        return ""
    try:
        result = _dpapi(base64.b64decode(encoded, validate=True), decrypt=True).decode("ascii")
        _key_bytes(result)
        return result
    except (ValueError, UnicodeError, OSError):
        raise ValueError("API key cannot be decrypted for this Windows user") from None


def save_config(config, key, path=None):
    clean = validate_config(config)
    clean["key_dpapi"] = protect_key(key)
    target = Path(path) if path is not None else default_config_path()
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text(json.dumps(clean, indent=2) + "\n", encoding="utf-8")
    temporary.replace(target)


def load_config(path=None):
    target = Path(path) if path is not None else default_config_path()
    if target.stat().st_size > 32768:
        raise ValueError("Provider settings file is too large")
    return validate_config(json.loads(target.read_text(encoding="utf-8-sig")))


def build_environment(path=None, base_env=None):
    config = load_config(path)
    key = unprotect_key(config["key_dpapi"])
    if config["provider"] != "local" and not key:
        raise ValueError("Configure an API key before launching the cloud provider")
    env = dict(os.environ if base_env is None else base_env)
    for name in ENV_NAMES:
        env.pop(name, None)
    env.update(AEGIS_AI_ENDPOINT=config["endpoint"], AEGIS_AI_MODEL=config["model"],
               AEGIS_AI_FORMAT=config["format"], AEGIS_AI_PROVIDER=config["provider"])
    if key:
        env["AEGIS_AI_API_KEY"] = key
    return env


class _NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def test_connection(config, key):
    config = validate_config(config)
    _key_bytes(key)
    if config["provider"] != "local" and not key:
        return {"ok": False, "message": "Enter an API key first"}
    payload = {"model": config["model"], "max_tokens": 32, "stream": False,
               "messages": [{"role": "user", "content": 'Return only JSON: {"ok":true}'}],
               "response_format": {"type": "json_object"}}
    if config["provider"] == "deepseek":
        payload["thinking"] = {"type": "disabled"}
    if config["format"] == "json_schema":
        payload["response_format"] = {"type": "json_schema", "json_schema": {
            "name": "connection_test", "strict": True, "schema": {
                "type": "object", "properties": {"ok": {"type": "boolean"}},
                "required": ["ok"], "additionalProperties": False}}}
    if config["provider"] == "local":
        payload["chat_template_kwargs"] = {"enable_thinking": False}
    headers = {"Content-Type": "application/json"}
    if key:
        headers["Authorization"] = "Bearer " + key
    request = urllib.request.Request(config["endpoint"], json.dumps(payload).encode(), headers)
    try:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), _NoRedirect())
        with opener.open(request, timeout=20) as response:
            raw = response.read(65537)
        if len(raw) > 65536:
            raise ValueError("Response too large")
        reply = json.loads(raw)["choices"][0]["message"]["content"]
        if json.loads(reply).get("ok") is not True:
            raise ValueError("Unexpected reply")
        return {"ok": True, "message": "Connection succeeded; JSON response verified"}
    except urllib.error.HTTPError as error:
        return {"ok": False, "status": error.code, "message": f"Provider returned HTTP {error.code}; check settings"}
    except (OSError, ValueError, KeyError, IndexError, TypeError, AttributeError):
        return {"ok": False, "message": "Connection failed or returned unexpected JSON; check settings"}


def configure(path=None):
    import tkinter as tk
    from tkinter import ttk, messagebox
    target = Path(path) if path is not None else default_config_path()
    root = tk.Tk()
    root.title("Aegis Arena — AI provider")
    root.resizable(False, False)
    values = dict(provider="deepseek", endpoint=DEFAULT_ENDPOINT, model=DEFAULT_MODEL, format="json_object")
    key = ""
    if target.exists():
        try:
            values = load_config(target)
            key = unprotect_key(values["key_dpapi"])
        except (OSError, ValueError):
            messagebox.showwarning("Provider settings", "Saved settings could not be unlocked. Enter them again.")
    fields = {name: tk.StringVar(value=values[name]) for name in ("provider", "endpoint", "model", "format")}
    secret, status = tk.StringVar(value=key), tk.StringVar(value="Save only stores settings. Test connection makes one small API request.")
    frame = ttk.Frame(root, padding=18)
    frame.grid()
    for row, (name, label) in enumerate((("provider", "Provider"), ("endpoint", "Endpoint"), ("model", "Model"), ("format", "JSON format"))):
        ttk.Label(frame, text=label).grid(row=row, column=0, sticky="w", pady=5)
        widget = (ttk.Combobox(frame, textvariable=fields[name], values=PROVIDERS if name == "provider" else ("json_object", "json_schema"), state="readonly", width=58)
                  if name in ("provider", "format") else ttk.Entry(frame, textvariable=fields[name], width=61))
        widget.grid(row=row, column=1, sticky="ew")
        if name == "provider":
            def preset(_event):
                provider = fields["provider"].get()
                if provider == "deepseek":
                    fields["endpoint"].set(DEFAULT_ENDPOINT); fields["model"].set(DEFAULT_MODEL); fields["format"].set("json_object")
                elif provider == "local":
                    fields["endpoint"].set("http://127.0.0.1:18765/v1/chat/completions"); fields["model"].set("Qwen3-0.6B"); fields["format"].set("json_schema")
            widget.bind("<<ComboboxSelected>>", preset)
    ttk.Label(frame, text="API key").grid(row=4, column=0, sticky="w", pady=5)
    ttk.Entry(frame, textvariable=secret, show="*", width=61).grid(row=4, column=1)
    def current():
        return validate_config({name: value.get() for name, value in fields.items()})
    def save():
        try:
            save_config(current(), secret.get(), target)
            status.set("Saved for this Windows user. No API request was sent.")
        except (OSError, ValueError):
            messagebox.showerror("Provider settings", "Could not save. Check endpoint, model and API key fields.")
    def test():
        try:
            config, token = current(), secret.get()
            _key_bytes(token)
        except ValueError:
            messagebox.showerror("Provider settings", "Check endpoint, model and API key fields."); return
        test_button.configure(state="disabled")
        status.set("Testing with one small JSON API request…")
        def worker():
            result = test_connection(config, token)
            def done():
                status.set(result["message"]); test_button.configure(state="normal")
            try: root.after(0, done)
            except RuntimeError: pass
        threading.Thread(target=worker, daemon=True).start()
    ttk.Button(frame, text="Save settings", command=save).grid(row=5, column=0, pady=14)
    test_button = ttk.Button(frame, text="Test connection (calls API)", command=test)
    test_button.grid(row=5, column=1, sticky="w", pady=14)
    ttk.Label(frame, textvariable=status, wraplength=550).grid(row=6, column=0, columnspan=2, sticky="w")
    ttk.Label(frame, text="API key is encrypted with Windows DPAPI. It is never passed on the command line.", wraplength=550).grid(row=7, column=0, columnspan=2, sticky="w", pady=(10, 0))
    root.mainloop()


if __name__ == "__main__":
    configure()

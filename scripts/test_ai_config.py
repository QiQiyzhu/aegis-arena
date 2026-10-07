"""Offline provider/launcher checks; only synthetic keys and temporary files."""
import copy
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aegis_ai_config as config
import launch_copilot as launcher

FAKE_KEY = "aegis-test-only-not-a-real-api-key"


def settings(**changes):
    value = dict(provider="deepseek", endpoint=config.DEFAULT_ENDPOINT,
                 model=config.DEFAULT_MODEL, format="json_object", key_dpapi="")
    value.update(changes)
    return value


class ProviderValidationTests(unittest.TestCase):
    def test_cloud_and_explicit_loopback_endpoints(self):
        for provider, endpoint in (("deepseek", config.DEFAULT_ENDPOINT),
                                   ("custom", "https://example.invalid/v1/chat/completions"),
                                   ("local", "http://127.0.0.1:18765/v1/chat/completions")):
            with self.subTest(provider=provider):
                self.assertEqual(config.validate_endpoint(endpoint, provider), endpoint)

    def test_endpoint_rejects_credential_and_parser_ambiguity(self):
        bad = ["https://u:p@api.deepseek.com/chat/completions",
               "https://api.deepseek.com@other.invalid/chat/completions",
               "https://api.deepseek.com/chat/completions?",
               "https://api.deepseek.com/chat/completions#",
               "https://api.deepseek.com/chat\\completions",
               "https://api.deepseek.com/\r\nAuthorization:x",
               "https://api.deepseek.com/chat completions",
               "https://api.deepseek.com/中", "https://api.deepseek.com"]
        for endpoint in bad:
            with self.subTest(endpoint=endpoint), self.assertRaises(ValueError):
                config.validate_endpoint(endpoint, "deepseek")

    def test_provider_limits_hosts_transport_and_ports(self):
        bad = [("deepseek", "https://api.deepseek.com.evil.invalid/chat/completions"),
               ("deepseek", "https://api.deepseek.com:444/chat/completions"),
               ("deepseek", "http://api.deepseek.com/chat/completions"),
               ("custom", "http://127.0.0.1/v1/chat/completions"),
               ("local", "http://localhost:18765/v1/chat/completions"),
               ("local", "https://example.invalid/v1/chat/completions"),
               ("local", "http://127.0.0.1:0/v1/chat/completions"),
               ("local", "http://127.0.0.1:65536/v1/chat/completions"),
               ("unknown", config.DEFAULT_ENDPOINT)]
        for provider, endpoint in bad:
            with self.subTest(provider=provider, endpoint=endpoint), self.assertRaises(ValueError):
                config.validate_endpoint(endpoint, provider)

    def test_config_drops_unknown_fields_without_mutating_input(self):
        value = settings(api_key=FAKE_KEY, extra="ignored")
        before = copy.deepcopy(value)
        result = config.validate_config(value)
        self.assertEqual(value, before)
        self.assertEqual(set(result), {"provider", "endpoint", "model", "format", "key_dpapi"})
        self.assertNotIn(FAKE_KEY, json.dumps(result))

    def test_config_rejects_invalid_types_and_identifiers(self):
        cases = [None, [], settings(model=""), settings(model="x\nAuthorization:y"),
                 settings(model="x" * 129), settings(format="arbitrary"),
                 settings(key_dpapi=123), settings(key_dpapi="x" * 16385)]
        for value in cases:
            with self.subTest(value=value), self.assertRaises(ValueError):
                config.validate_config(value)

    def test_deepseek_chat_rejects_unsupported_schema_format(self):
        with self.assertRaises(ValueError):
            config.validate_config(settings(format="json_schema"))

    def test_empty_key_and_invalid_key_values(self):
        self.assertEqual(config.protect_key(""), "")
        self.assertEqual(config.unprotect_key(""), "")
        for key in (None, "contains space", "x\r\nBearer:y", "x" * 4097, "密码"):
            with self.subTest(key_type=type(key).__name__), self.assertRaises(ValueError):
                config.protect_key(key)

    def test_corrupt_encrypted_key_has_sanitized_error(self):
        for blob in ("not valid base64!", "YWVnaXMtdGVzdA=="):
            with self.subTest(blob=blob), self.assertRaises(ValueError) as caught:
                config.unprotect_key(blob)
            self.assertNotIn(blob, str(caught.exception))


@unittest.skipUnless(os.name == "nt", "actual DPAPI requires Windows")
class EncryptedSettingsTests(unittest.TestCase):
    def test_actual_dpapi_synthetic_key_roundtrip(self):
        encrypted = config.protect_key(FAKE_KEY)
        self.assertTrue(encrypted)
        self.assertNotIn(FAKE_KEY, encrypted)
        self.assertEqual(config.unprotect_key(encrypted), FAKE_KEY)

    def test_save_encrypts_key_and_builds_child_environment_only(self):
        with tempfile.TemporaryDirectory(prefix="aegis-config-test-") as folder:
            path = Path(folder) / "nested" / "settings.json"
            base = {"KEEP_THIS": "yes", **{name: "old" for name in config.ENV_NAMES}}
            original = dict(base)
            with mock.patch.object(config, "test_connection", side_effect=AssertionError("Save must not connect")):
                config.save_config(settings(), FAKE_KEY, path)
            self.assertNotIn(FAKE_KEY, path.read_text(encoding="utf-8"))
            self.assertEqual(config.unprotect_key(config.load_config(path)["key_dpapi"]), FAKE_KEY)
            child = config.build_environment(path, base)
            self.assertEqual(base, original)
            self.assertEqual(child["KEEP_THIS"], "yes")
            self.assertEqual(child["AEGIS_AI_API_KEY"], FAKE_KEY)
            self.assertEqual(child["AEGIS_AI_MODEL"], config.DEFAULT_MODEL)
            self.assertEqual(child["AEGIS_AI_ENDPOINT"], config.DEFAULT_ENDPOINT)
            self.assertEqual(child["AEGIS_AI_FORMAT"], "json_object")
            self.assertEqual(child["AEGIS_AI_PROVIDER"], "deepseek")
            self.assertFalse(path.with_name(path.name + ".tmp").exists())


class OfflineConnectionTests(unittest.TestCase):
    def test_local_configuration_clears_stale_cloud_key(self):
        with tempfile.TemporaryDirectory(prefix="aegis-config-test-") as folder:
            path = Path(folder) / "settings.json"
            config.save_config(settings(provider="local", endpoint="http://127.0.0.1:18765/v1/chat/completions",
                                        model="aegis-local", format="json_schema"), "", path)
            base = {"AEGIS_AI_API_KEY": FAKE_KEY, "KEEP_THIS": "ok"}
            child = config.build_environment(path, base)
            self.assertNotIn("AEGIS_AI_API_KEY", child)
            self.assertEqual(base["AEGIS_AI_API_KEY"], FAKE_KEY)

    def test_missing_cloud_key_blocks_launch_and_test_without_network(self):
        with tempfile.TemporaryDirectory(prefix="aegis-config-test-") as folder:
            path = Path(folder) / "settings.json"
            config.save_config(settings(), "", path)
            with self.assertRaises(ValueError):
                config.build_environment(path, {})
        with mock.patch.object(config.urllib.request, "build_opener") as opener:
            self.assertFalse(config.test_connection(settings(), "")["ok"])
            opener.assert_not_called()

    def test_oversized_and_malformed_settings_are_rejected(self):
        with tempfile.TemporaryDirectory(prefix="aegis-config-test-") as folder:
            path = Path(folder) / "settings.json"
            for text in (" " * 32769, "{broken", "[]"):
                path.write_text(text, encoding="utf-8")
                with self.subTest(size=len(text)), self.assertRaises(ValueError):
                    config.load_config(path)

    def test_connection_sends_non_thinking_json_and_verifies_reply(self):
        response = mock.MagicMock()
        response.__enter__.return_value = response
        response.read.return_value = json.dumps({"choices": [{"message": {"content": '{"ok":true}'}}]}).encode()
        opener = mock.Mock()
        opener.open.return_value = response
        with mock.patch.object(config.urllib.request, "build_opener", return_value=opener) as build:
            result = config.test_connection(settings(), FAKE_KEY)
        self.assertTrue(result["ok"])
        request = opener.open.call_args.args[0]
        payload = json.loads(request.data)
        self.assertEqual(payload["model"], config.DEFAULT_MODEL)
        self.assertEqual(payload["thinking"], {"type": "disabled"})
        self.assertEqual(payload["response_format"], {"type": "json_object"})
        self.assertEqual(request.get_header("Authorization"), "Bearer " + FAKE_KEY)
        self.assertNotIn(FAKE_KEY, request.full_url)
        self.assertNotIn(FAKE_KEY, request.data.decode())
        response.read.assert_called_once_with(65537)
        self.assertTrue(any(isinstance(item, config._NoRedirect) for item in build.call_args.args))

    def test_redirect_handler_and_http_error_do_not_read_or_echo_body(self):
        request = urllib.request.Request(config.DEFAULT_ENDPOINT)
        self.assertIsNone(config._NoRedirect().redirect_request(request, None, 302, "Found", {},
                                                                "https://other.invalid/leak"))
        body = mock.Mock()
        error = urllib.error.HTTPError(config.DEFAULT_ENDPOINT, 302, FAKE_KEY, {}, body)
        opener = mock.Mock()
        opener.open.side_effect = error
        with mock.patch.object(config.urllib.request, "build_opener", return_value=opener):
            result = config.test_connection(settings(), FAKE_KEY)
        self.assertFalse(result["ok"])
        self.assertEqual(result["status"], 302)
        self.assertNotIn(FAKE_KEY, json.dumps(result))
        body.read.assert_not_called()

    def test_custom_and_local_connection_use_configured_schema_format(self):
        for provider, endpoint, key in (("custom", "https://example.invalid/v1/chat/completions", FAKE_KEY),
                                         ("local", "http://127.0.0.1:18765/v1/chat/completions", "")):
            with self.subTest(provider=provider):
                opener = mock.MagicMock()
                opener.open.return_value.__enter__.return_value.read.return_value = json.dumps(
                    {"choices": [{"message": {"content": '{"ok":true}'}}]}).encode()
                with mock.patch.object(config.urllib.request, "build_opener", return_value=opener):
                    result = config.test_connection(settings(provider=provider, endpoint=endpoint,
                                                             format="json_schema"), key)
                self.assertTrue(result["ok"])
                payload = json.loads(opener.open.call_args.args[0].data)
                self.assertEqual(payload["response_format"]["type"], "json_schema")
                schema = payload["response_format"]["json_schema"]
                self.assertIs(schema["strict"], True)
                self.assertEqual(schema["schema"]["required"], ["ok"])
                self.assertNotIn("thinking", payload)
                if provider == "local":
                    self.assertEqual(payload["chat_template_kwargs"], {"enable_thinking": False})
                else:
                    self.assertNotIn("chat_template_kwargs", payload)

    def test_unexpected_response_or_transport_failure_is_sanitized(self):
        cases = [OSError(FAKE_KEY), b"x" * 65537, FAKE_KEY.encode(),
                 b'{"choices":[]}', b'{"choices":[{"message":{"content":"{}"}}]}']
        for value in cases:
            with self.subTest(case_type=type(value).__name__):
                opener = mock.MagicMock()
                if isinstance(value, Exception):
                    opener.open.side_effect = value
                else:
                    opener.open.return_value.__enter__.return_value.read.return_value = value
                with mock.patch.object(config.urllib.request, "build_opener", return_value=opener):
                    result = config.test_connection(settings(), FAKE_KEY)
                self.assertFalse(result["ok"])
                self.assertNotIn(FAKE_KEY, json.dumps(result))


class LauncherTests(unittest.TestCase):
    def test_launcher_preserves_argument_boundaries_without_shell(self):
        with tempfile.TemporaryDirectory(prefix="aegis launcher test ") as folder:
            game = Path(folder) / "AegisArena.exe"
            game.write_bytes(b"synthetic fixture, never executed")
            extras = ["-windowed", "value with spaces", "$(not-a-command)"]
            command = launcher.build_launch_command(game, extras)
            self.assertEqual(command, [str(game.resolve()), *extras])

    def test_launcher_rejects_missing_relative_non_exe_and_editor(self):
        with tempfile.TemporaryDirectory(prefix="aegis-launch-test-") as folder:
            root = Path(folder)
            wrong = root / "game.txt"
            editor = root / "UnrealEditor-Cmd.exe"
            wrong.touch(); editor.touch()
            for path in (root / "missing.exe", Path("relative.exe"), wrong, editor, root):
                with self.subTest(path=str(path)), self.assertRaises(ValueError):
                    launcher.build_launch_command(path)

    def test_main_launches_only_packaged_command_with_child_environment(self):
        with tempfile.TemporaryDirectory(prefix="aegis-launch-test-") as folder:
            game = Path(folder) / "AegisArena.exe"
            game.touch()
            env = {"AEGIS_AI_API_KEY": FAKE_KEY}
            with mock.patch.object(launcher, "build_environment", return_value=env), \
                 mock.patch.object(launcher.subprocess, "Popen") as launch:
                result = launcher.main(["--game-exe", str(game), "--", "-windowed"])
            self.assertEqual(result, 0)
            self.assertEqual(launch.call_args.args[0], [str(game.resolve()), "-windowed"])
            self.assertIs(launch.call_args.kwargs["env"], env)
            self.assertNotIn(FAKE_KEY, repr(launch.call_args.args))
            self.assertFalse(launch.call_args.kwargs.get("shell", False))

    def test_launcher_failure_is_sanitized_and_does_not_spawn(self):
        with tempfile.TemporaryDirectory(prefix="aegis-launch-test-") as folder:
            game = Path(folder) / "AegisArena.exe"
            game.touch()
            output = io.StringIO()
            with mock.patch.object(launcher, "build_environment", side_effect=ValueError(FAKE_KEY)), \
                 mock.patch.object(launcher.subprocess, "Popen") as launch, \
                 mock.patch("sys.stderr", output):
                self.assertEqual(launcher.main(["--game-exe", str(game)]), 2)
            launch.assert_not_called()
            self.assertNotIn(FAKE_KEY, output.getvalue())


if __name__ == "__main__":
    unittest.main()

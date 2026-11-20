#!/usr/bin/env python3
"""Deterministic integration tests of real HTTP session/turn/TTS contracts."""
import io
import json
import threading
import time
import unittest
import urllib.error
import urllib.request
import wave
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).parent))
from server import AlaService, BridgeError, make_server, OpenAIAdapter

TOKEN = "testing-bridge-auth-token-min-24-chars"
SID = "session_0123456789abcdef"


def wav():
    output = io.BytesIO()
    with wave.open(output, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(16000)
        f.writeframes(b"\x00\x00" * 16000)
    return output.getvalue()


class FakeModel:
    def __init__(self):
        self.action = "JOKE"
        self.transcript = "Zgubiłem telefon"
        self.tts_calls = 0
        self.block = None
        self.released = None
        self.bad = False

    def transcribe(self, data):
        if self.block:
            self.block.set()
            self.released.wait(timeout=2)
        return self.transcript

    def verdict(self, context):
        if self.bad:
            return {"action": "EXECUTE", "confidence": 99, "sensitive": False, "reason": "", "text": "unsafe"}
        return {"action": self.action, "confidence": 95, "sensitive": False,
                "reason": "ok", "text": "Sprawdź rękę!" if self.action == "JOKE" else ""}

    def speak(self, text):
        self.tts_calls += 1
        return b"\x00\x00" * 2400


class ServiceTests(unittest.TestCase):
    def setUp(self):
        self.model = FakeModel()
        self.service = AlaService(self.model)
        self.server = make_server("127.0.0.1", 0, TOKEN, self.service)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = "http://127.0.0.1:" + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def request(self, path, obj=None, pcm=None, headers=None, token=TOKEN):
        data = pcm if pcm is not None else json.dumps(obj).encode() if obj is not None else None
        typ = "audio/wav" if pcm is not None else "application/json"
        req = urllib.request.Request(self.url + path, data=data, method="POST" if data is not None else "GET",
                                     headers={"Authorization": "Bearer " + token,
                                              "Content-Type": typ, **(headers or {})})
        try:
            with urllib.request.urlopen(req, timeout=2) as res:
                binary = res.read()
                if path == "/v1/tts":
                    return res.status, binary
                return res.status, json.loads(binary)
        except urllib.error.HTTPError as err:
            return err.code, json.loads(err.read())

    def start(self):
        status, data = self.request("/v1/session/start", {
            "session_id": SID, "generation": 7, "ttl_ms": 600000})
        self.assertEqual(status, 200)
        return data

    def turn(self, rev=0, turn=1):
        return self.request("/v1/turn", pcm=wav(), headers={
            "X-Ala-Session": SID, "X-Ala-Generation": "7",
            "X-Ala-Turn": str(turn), "X-Ala-Revision": str(rev)})

    def test_authorization_and_ready(self):
        self.assertEqual(self.request("/v1/health", token="wrong")[0], 401)
        self.assertTrue(self.request("/v1/health")[1]["ready"])
        self.assertEqual(self.turn()[0], 409)  # no implicit session

    def test_separate_semantic_grant_and_one_time_tts(self):
        self.start()
        status, result = self.turn()
        self.assertEqual(status, 200)
        self.assertEqual(result["action"], "JOKE")
        self.assertEqual(result["context_revision"], 1)
        self.assertEqual(self.model.tts_calls, 0)  # NO speech before firmware authorization
        grant = {"session_id": SID, "generation": 7, "turn_id": 1,
                 "context_revision": 1, "ticket": result["ticket"]}
        self.assertEqual(self.request("/v1/tts", grant)[0], 200)
        self.assertEqual(self.model.tts_calls, 1)
        self.assertEqual(self.request("/v1/tts", grant)[0], 409)

    def test_ignored_turn_cannot_generate_speech(self):
        self.start()
        self.model.action = "IGNORE"
        status, result = self.turn()
        self.assertEqual(status, 200)
        self.assertEqual(result["action"], "IGNORE")
        self.assertEqual(result["ticket"], "")
        self.assertEqual(self.request("/v1/tts", {"session_id": SID, "generation": 7,
            "turn_id": 1, "context_revision": 1, "ticket": ""})[0], 409)

    def test_stop_invalidates_pending_and_new_turn(self):
        self.start()
        _, result = self.turn()
        self.assertEqual(self.request("/v1/session/stop",
                         {"session_id": SID, "generation": 7})[0], 200)
        self.assertEqual(self.request("/v1/tts", {"session_id": SID, "generation": 7,
            "turn_id": 1, "context_revision": 1, "ticket": result["ticket"]})[0], 409)
        self.assertEqual(self.turn(rev=1, turn=2)[0], 409)

    def test_stale_revision_and_replay(self):
        self.start()
        self.assertEqual(self.turn()[0], 200)
        self.assertEqual(self.turn()[0], 409)
        self.assertEqual(self.turn(rev=1, turn=1)[0], 409)
        self.assertEqual(self.turn(rev=1, turn=2)[0], 200)

    def test_corrupt_audio_and_invalid_model(self):
        self.start()
        headers = {"X-Ala-Session": SID, "X-Ala-Generation": "7",
                   "X-Ala-Turn": "1", "X-Ala-Revision": "0"}
        self.assertEqual(self.request("/v1/turn", pcm=b"bad", headers=headers)[0], 400)
        self.model.bad = True
        self.assertEqual(self.turn()[0], 502)
        self.assertEqual(self.model.tts_calls, 0)

    def test_stop_during_stt_discards_result(self):
        self.start()
        self.model.block = threading.Event()
        self.model.released = threading.Event()
        responses = []
        t = threading.Thread(target=lambda: responses.append(self.turn()))
        t.start()
        self.assertTrue(self.model.block.wait(timeout=2))
        self.request("/v1/session/stop", {"session_id": SID, "generation": 7})
        self.model.released.set()
        t.join(timeout=2)
        self.assertEqual(responses[0][0], 409)
        self.assertEqual(self.model.tts_calls, 0)

    def test_expiry(self):
        self.start()
        with self.service.lock:
            self.service.sessions[SID].expires = time.monotonic() - 1
        self.assertEqual(self.turn()[0], 409)

    def test_real_openai_http_adapter_against_deterministic_provider(self):
        # Exercise production multipart STT, JSON verdict and raw-PCM TTS
        # clients, while replacing the *external* OpenAI endpoint (no keys).
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

        calls = []
        class Provider(BaseHTTPRequestHandler):
            protocol_version = "HTTP/1.0"
            def log_message(self, *_args):
                pass

            def do_POST(self):
                content_type = self.headers.get("Content-Type", "")
                count = int(self.headers.get("Content-Length", "0"))
                body = self.rfile.read(count)
                calls.append((self.path, content_type, body))
                if self.path == "/audio/transcriptions":
                    assert "multipart/form-data" in content_type
                    assert b"speech.wav" in body and b"WAVEfmt " in body
                    output = json.dumps({"text": "Znowu zgubiłem telefon"}).encode()
                elif self.path == "/chat/completions":
                    message = json.loads(body)
                    assert message["response_format"]["type"] == "json_object"
                    assert "untrusted_transcript_context" in message["messages"][1]["content"]
                    verdict = {"action": "JOKE", "confidence": 96, "sensitive": False,
                               "reason": "harmless", "text": "Sprawdź rękę!"}
                    output = json.dumps({"choices": [{"message": {
                        "content": json.dumps(verdict, ensure_ascii=False)}}]}).encode()
                elif self.path == "/audio/speech":
                    request = json.loads(body)
                    assert request["response_format"] == "pcm"
                    output = b"\x00\x00" * 2400
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Type", "application/json" if self.path != "/audio/speech" else "audio/L16")
                self.send_header("Content-Length", str(len(output)))
                self.end_headers()
                self.wfile.write(output)

        provider = ThreadingHTTPServer(("127.0.0.1", 0), Provider)
        thread = threading.Thread(target=provider.serve_forever, daemon=True)
        thread.start()
        try:
            self.service.adapter = OpenAIAdapter("placeholder-test-only",
                base_url="http://127.0.0.1:" + str(provider.server_port))
            self.start()
            status, decision = self.turn()
            self.assertEqual(status, 200)
            self.assertEqual(decision["action"], "JOKE")
            self.assertTrue(decision["ticket"])
            self.assertEqual([call[0] for call in calls],
                             ["/audio/transcriptions", "/chat/completions"])
            grant = {"session_id": SID, "generation": 7, "turn_id": 1,
                     "context_revision": 1, "ticket": decision["ticket"]}
            self.assertEqual(self.request("/v1/tts", grant)[0], 200)
            self.assertEqual([call[0] for call in calls],
                             ["/audio/transcriptions", "/chat/completions", "/audio/speech"])
        finally:
            provider.shutdown()
            provider.server_close()
            thread.join(timeout=2)

    def test_bad_token_before_audio(self):
        self.start()
        self.assertEqual(self.request("/v1/turn", pcm=wav(), headers={
            "X-Ala-Session": SID, "X-Ala-Generation": "7",
            "X-Ala-Turn": "1", "X-Ala-Revision": "0"}, token="no")[0], 401)
        self.assertEqual(self.service.sessions[SID].last_turn, 0)


if __name__ == "__main__":
    unittest.main()

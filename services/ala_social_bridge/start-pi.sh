#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ "${1:-}" != "--accept-trusted-lan-http" ]]; then
  echo "HTTP+bearer NIE szyfruje nagrań. Używaj wyłącznie w zaufanej sieci LAN." >&2
  echo "Uruchom: bash start-pi.sh --accept-trusted-lan-http" >&2
  exit 2
fi
if [[ -z "${OPENAI_API_KEY:-}" ]]; then
  read -rsp "OPENAI_API_KEY (ukryty): " OPENAI_API_KEY
  echo
  export OPENAI_API_KEY
fi
if [[ -z "${ALA_BRIDGE_TOKEN:-}" ]]; then
  ALA_BRIDGE_TOKEN="$(python3 -c 'import secrets;print(secrets.token_urlsafe(32))')"
  export ALA_BRIDGE_TOKEN
  echo "Skopiuj TOKEN do panelu Ali (nie udostepniaj go): $ALA_BRIDGE_TOKEN"
fi
echo "EXPERIMENTAL: tylko zaufany LAN, bez TLS. Ctrl+C aby zakonczyc."
exec python3 server.py --host "0.0.0.0" --port "${ALA_BRIDGE_PORT:-8765}"

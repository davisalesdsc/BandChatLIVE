#!/bin/bash
# Baixa os modelos Whisper (whisper.cpp, Hugging Face) que vão dentro dos instaladores.
#   baixar-modelos.sh <pasta> <arquivo.bin> [arquivo.bin ...]
# Já baixado e válido: pula. Confere o cabeçalho ggml para não empacotar uma
# página de erro no lugar do modelo.
set -euo pipefail
DIR="$1"; shift
URL="${MODELS_URL:-https://huggingface.co/ggerganov/whisper.cpp/resolve/main}"
mkdir -p "$DIR"

valido () { [ -s "$1" ] && [ "$(head -c 4 "$1")" = "lmgg" ] && [ "$(wc -c < "$1")" -gt 10000000 ]; }

for f in "$@"; do
  if valido "$DIR/$f"; then
    echo "ok (cache): $f"
    continue
  fi
  echo "baixando: $f"
  curl -fL --retry 4 --retry-delay 5 -o "$DIR/$f.part" "$URL/$f"
  mv "$DIR/$f.part" "$DIR/$f"
  valido "$DIR/$f" || { echo "ERRO: $f não é um modelo ggml válido"; rm -f "$DIR/$f"; exit 1; }
  echo "ok: $f ($(( $(wc -c < "$DIR/$f") / 1048576 )) MB)"
done

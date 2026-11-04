#!/usr/bin/env python3
"""Falha se houver texto com acento passado ao JUCE sem conversão de UTF-8.

juce::String("Não") interpreta os bytes como Latin-1 e mostra "NÃ£o" na tela.
Todo literal com acento precisa estar dentro de u8(...) ou String::fromUTF8(...)
(ou numa tabela const char* convertida no uso). Comentários são ignorados.
Uso: checar-acentos.py <pasta Source>"""
import re, sys, pathlib

CONV = re.compile(r"(?:\bu8|fromUTF8)\s*$")

def check(path):
    src = path.read_text(encoding="utf-8")
    out, i, line = [], 0, 1
    stack = []                 # um item por "(" aberto: True se for u8(/fromUTF8(
    while i < len(src):
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i); i = len(src) if j < 0 else j; continue
        if src.startswith("/*", i):
            j = src.find("*/", i) + 2; line += src.count("\n", i, j); i = j; continue
        if c == "'":                                    # char literal
            j = i + 1
            while src[j] != "'": j += 2 if src[j] == "\\" else 1
            i = j + 1; continue
        if c == '"':
            j = i + 1
            while src[j] != '"': j += 2 if src[j] == "\\" else 1
            lit = src[i:j + 1]
            if re.search(r"[^\x00-\x7f]", lit) and not any(stack):
                linetext = src[src.rfind("\n", 0, i) + 1: src.find("\n", i)]
                if not re.search(r"const char\*.*\[\]", linetext):
                    out.append(f"{path}:{line}: {lit}")
            i = j + 1; continue
        if c == "(":
            stack.append(bool(CONV.search(src[max(0, i - 40):i])))
        elif c == ")" and stack:
            stack.pop()
        elif c == "\n":
            line += 1
        i += 1
    return out

problems = [p for f in sorted(pathlib.Path(sys.argv[1]).rglob("*.[ch]*")) for p in check(f)]
print("\n".join(problems) if problems else "ok: nenhum texto com acento sem conversão")
sys.exit(1 if problems else 0)

#!/usr/bin/env python3
"""Small, reproducible CasioGPT model benchmark; never prints the API key."""

from __future__ import annotations

import json
from pathlib import Path
import statistics
import time
import urllib.error
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
TOKEN_PATH = ROOT / "casiogpt_api.txt"
OUTPUT_PATH = ROOT / "verification/cloud-model-benchmark.json"
MODELS = (
    "gemma4:31b",
    "gpt-oss:20b",
    "gpt-oss:120b",
    "nemotron-3-nano:30b",
    "nemotron-3-super",
    "glm-5.3-flash",
)
SYSTEM = (
    "You are CasioGPT, a concise assistant for a secondary-school student. "
    "Answer in the user's language using plain text only, without Markdown. "
    "Explain reasoning briefly and accurately."
)
CASES = (
    {
        "id": "algebra",
        "prompt": "Resuelve 2(x - 3) + 4 = 3x - 5. Explica los pasos brevemente.",
        "groups": (("x = 3", "x=3"),),
    },
    {
        "id": "percentage",
        "prompt": "Una chaqueta cuesta 80 euros. Se aplica un descuento del 15% y despues IVA del 21%. Cual es el precio final?",
        "groups": (("82.28", "82,28"), ("68",)),
    },
    {
        "id": "science",
        "prompt": "Explica en cuatro frases por que la sal se disuelve en agua y por que el aceite no.",
        "groups": (("polar", "polaridad"), ("ion", "iones"), ("aceite",), ("no polar", "apolar")),
    },
    {
        "id": "history_language",
        "prompt": "Diferencia en tres frases la causa profunda y el detonante de la Primera Guerra Mundial. Despues identifica el tipo de subordinada en: Aunque llovia, salimos a pasear.",
        "groups": (("sarajevo", "archiduque", "fernando"), ("alianz", "nacionalis", "militaris", "imperialis"), ("concesiv",)),
    },
    {
        "id": "conversation",
        "prompt": "Estoy nervioso porque manana tengo un examen y siento que no llego. Respondeme de forma humana y util en tres frases.",
        "groups": (("normal", "entiendo", "comprensible", "respira"), ("repas", "plan", "descans", "prioriza")),
    },
    {
        "id": "context",
        "messages": (
            {"role": "user", "content": "Me llamo Nora y mi asignatura favorita es biologia."},
            {"role": "assistant", "content": "Encantado, Nora. Biologia es una asignatura fascinante."},
            {"role": "user", "content": "Como me llamo y cual es mi asignatura favorita? Anade una pregunta corta para seguir hablando."},
        ),
        "groups": (("nora",), ("biolog",), ("?",)),
    },
)


def score(text: str, groups) -> tuple[int, int]:
    normalized = text.casefold()
    hits = sum(any(option.casefold() in normalized for option in group) for group in groups)
    return hits, len(groups)


def request(token: str, model: str, case: dict) -> dict:
    messages = [{"role": "system", "content": SYSTEM}]
    if "messages" in case:
        messages.extend(case["messages"])
    else:
        messages.append({"role": "user", "content": case["prompt"]})
    body = json.dumps(
        {
            "model": model,
            "messages": messages,
            "stream": True,
            "think": False,
            "options": {"temperature": 0.1, "num_predict": 220},
        }
    ).encode()
    req = urllib.request.Request(
        "https://ollama.com/api/chat",
        data=body,
        headers={"Authorization": "Bearer " + token, "Content-Type": "application/json"},
    )
    started = time.monotonic()
    first = None
    pieces = []
    thinking_chars = 0
    done = False
    with urllib.request.urlopen(req, timeout=120) as response:
        for raw in response:
            item = json.loads(raw)
            message = item.get("message", {})
            piece = message.get("content", "")
            thinking_chars += len(message.get("thinking", ""))
            if piece and first is None:
                first = time.monotonic() - started
            pieces.append(piece)
            done = bool(item.get("done"))
    elapsed = time.monotonic() - started
    content = "".join(pieces).strip()
    hits, possible = score(content, case["groups"])
    return {
        "case": case["id"],
        "ok": done and bool(content),
        "first_content_s": round(first if first is not None else elapsed, 3),
        "total_s": round(elapsed, 3),
        "content_chars": len(content),
        "thinking_chars": thinking_chars,
        "score": hits,
        "possible": possible,
        "response": content,
    }


def main():
    token = TOKEN_PATH.read_text(encoding="ascii").strip()
    report = {"created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"), "models": {}}
    for model in MODELS:
        results = []
        access_error = None
        print(f"Testing {model}...", flush=True)
        for case in CASES:
            try:
                result = request(token, model, case)
                results.append(result)
                print(
                    f"  {case['id']}: {result['score']}/{result['possible']} "
                    f"first={result['first_content_s']}s total={result['total_s']}s "
                    f"chars={result['content_chars']}",
                    flush=True,
                )
            except urllib.error.HTTPError as exc:
                access_error = {"http_status": exc.code, "reason": exc.reason}
                print(f"  unavailable: HTTP {exc.code}", flush=True)
                break
            except Exception as exc:
                access_error = {"type": type(exc).__name__, "reason": str(exc)[:160]}
                print(f"  failed: {type(exc).__name__}", flush=True)
                break
        total_score = sum(item["score"] for item in results)
        total_possible = sum(item["possible"] for item in results)
        good = [item for item in results if item["ok"]]
        report["models"][model] = {
            "accessible_with_current_key": access_error is None,
            "error": access_error,
            "score": total_score,
            "possible": total_possible,
            "successes": len(good),
            "median_first_content_s": round(statistics.median(item["first_content_s"] for item in good), 3) if good else None,
            "median_total_s": round(statistics.median(item["total_s"] for item in good), 3) if good else None,
            "thinking_chars": sum(item["thinking_chars"] for item in results),
            "results": results,
        }
    OUTPUT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Saved {OUTPUT_PATH}")


if __name__ == "__main__":
    main()

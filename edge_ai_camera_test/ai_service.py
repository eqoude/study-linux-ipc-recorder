import json
import re
import time
from pathlib import Path
from transformers import pipeline
from transformers.utils import logging

logging.set_verbosity_error()

MODEL_DIR = "./models/SmolVLM2-500M-Video-Instruct"
SNAPSHOT_PATH = "./edge_ai_camera_test/snapshot.jpg"
EVENT_PATH = "./edge_ai_camera_test/event.json"

pipe = pipeline(
    "image-text-to-text",
    model=MODEL_DIR,
)

PROMPT = (
    "You are an edge AI camera assistant.\n"
    "Analyze the image briefly.\n"
    "Return ONLY one JSON object.\n"
    "Do not return a list.\n"
    "Do not add markdown.\n"
    "Use exactly these fields:\n"
    "person_visible, scene_summary, risk_level.\n"
    "person_visible must be true or false.\n"
    "risk_level must be one of: low, medium, high.\n"
    "If there is no obvious danger, risk_level must be low."
)


def normalize_bool(value):
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        return value.strip().strip("\"'").lower() in ("true", "1", "yes", "y")
    if isinstance(value, (int, float)):
        return value != 0
    return False


def clean_model_answer(answer: str) -> str:
    text = answer.strip()
    text = re.sub(r"^```(?:json)?\s*", "", text, flags=re.IGNORECASE)
    text = re.sub(r"\s*```$", "", text)
    return text.strip()


def normalize_risk_level(value) -> str:
    text = str(value).strip().strip("\"'").lower()
    if "no risk" in text:
        return "low"
    if "high" in text:
        return "high"
    if "medium" in text:
        return "medium"
    if "low" in text:
        return "low"
    if text == "0":
        return "low"
    if text == "1":
        return "medium"
    if text == "2":
        return "high"
    return "low"


def normalize_scene_summary(value) -> str:
    if value is None:
        text = ""
    elif isinstance(value, str):
        text = value
    else:
        text = str(value)

    text = text.strip().strip("\"'").strip()
    if not text:
        text = "No scene summary available."
    if len(text) > 200:
        text = text[:200]
    return text


def extract_json_object(text: str):
    start = text.find("{")
    end = text.rfind("}")
    if start == -1 or end == -1 or start >= end:
        return None

    try:
        parsed = json.loads(text[start:end + 1])
    except json.JSONDecodeError:
        return None

    return parsed if isinstance(parsed, dict) else None


def extract_json_array(text: str):
    start = text.find("[")
    end = text.rfind("]")
    if start == -1 or end == -1 or start >= end:
        return None

    try:
        parsed = json.loads(text[start:end + 1])
    except json.JSONDecodeError:
        return None

    if isinstance(parsed, list):
        return next((item for item in parsed if isinstance(item, dict)), None)
    return None


def regex_extract_person_visible(text: str):
    match = re.search(
        r'(?i)"?person[_\s]+visible"?\s*[:=]\s*"?\b(true|false|yes|no|1|0)\b"?',
        text,
    )
    if match:
        return normalize_bool(match.group(1))

    lower_text = text.lower()
    if (
        "no visible person" in lower_text
        or "no person" in lower_text
        or "person_visible: false" in lower_text
    ):
        return False
    if "person" in lower_text or "human" in lower_text or "someone" in lower_text:
        return True
    return False


def regex_extract_scene_summary(text: str):
    match = re.search(
        r'(?is)"?scene[_\s]+summary"?\s*[:=]\s*"([^"]*)"',
        text,
    )
    if match:
        return match.group(1)

    match = re.search(
        r"(?is)'?scene[_\s]+summary'?\s*[:=]\s*'([^']*)'",
        text,
    )
    if match:
        return match.group(1)

    match = re.search(
        r'(?is)"?scene[_\s]+summary"?\s*[:=]\s*([^,\]\n]+)',
        text,
    )
    if match:
        return match.group(1)

    return text


def regex_extract_risk_level(text: str):
    match = re.search(
        r'(?i)"?risk[_\s]+level"?\s*[:=]\s*("[^"]*"|\'[^\']*\'|[^,\]\n]+)',
        text,
    )
    if match:
        return normalize_risk_level(match.group(1))
    return "low"


def normalize_parsed_result(parsed, fallback_text: str) -> dict:
    if not isinstance(parsed, dict):
        parsed = {}

    return {
        "person_visible": normalize_bool(parsed.get("person_visible", False)),
        "scene_summary": normalize_scene_summary(
            parsed.get("scene_summary", fallback_text)
        ),
        "risk_level": normalize_risk_level(parsed.get("risk_level", "low")),
    }


def parse_model_answer(answer: str) -> dict:
    raw_answer = answer if isinstance(answer, str) else str(answer)
    text = clean_model_answer(raw_answer)

    parsed = extract_json_object(text)
    if parsed is not None:
        return normalize_parsed_result(parsed, text)

    parsed = extract_json_array(text)
    if parsed is not None:
        return normalize_parsed_result(parsed, text)

    return {
        "person_visible": regex_extract_person_visible(text),
        "scene_summary": normalize_scene_summary(regex_extract_scene_summary(text)),
        "risk_level": regex_extract_risk_level(text),
    }


def analyze_once():
    snapshot = Path(SNAPSHOT_PATH)

    if not snapshot.exists():
        print("[ai_service] snapshot not found")
        return

    messages = [
        {
            "role": "user",
            "content": [
                {"type": "image", "url": SNAPSHOT_PATH},
                {"type": "text", "text": PROMPT},
            ],
        },
    ]

    result = pipe(text=messages)
    answer = result[0]["generated_text"][-1]["content"]
    parsed = parse_model_answer(answer)
    should_alert = parsed["risk_level"] in ("medium", "high")

    event = {
        "timestamp": time.time(),
        "person_visible": parsed["person_visible"],
        "scene_summary": parsed["scene_summary"],
        "risk_level": parsed["risk_level"],
        "should_alert": should_alert,
        "raw_answer": answer,
    }

    tmp_path = EVENT_PATH + ".tmp"
    Path(EVENT_PATH).parent.mkdir(parents=True, exist_ok=True)

    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(event, f, ensure_ascii=False, indent=2)

    Path(tmp_path).replace(EVENT_PATH)

    print(
        "[ai_service] "
        f"person_visible={str(event['person_visible']).lower()} "
        f"risk={event['risk_level']} "
        f"alert={str(event['should_alert']).lower()} "
        f"summary={event['scene_summary']}"
    )


def main():
    while True:
        try:
            analyze_once()
        except Exception as e:
            print("ai_service error:", e)

        time.sleep(3)


if __name__ == "__main__":
    main()

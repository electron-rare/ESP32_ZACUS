#!/usr/bin/env python3
import os
import sys
import time
import requests
from pathlib import Path

API_KEY = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("OPENAI_API_KEY")

if not API_KEY:
    print("❌ No OpenAI API key provided")
    sys.exit(1)

OUTPUT_DIR = Path("/Users/cils/Documents/Lelectron_rare/ESP32_ZACUS/data/ui_amiga/icons")
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

APPS = [
    {
        "id": "audio_player",
        "prompt": "Retro Amiga demoscene style icon of a music player with colorful speaker waves, vibrant cyan and magenta neon colors, 1990s aesthetic, pixel art inspired, solid black background, 256x256 pixels, nostalgic retrowave",
    },
    {
        "id": "timer",
        "prompt": "Retro Amiga demoscene style icon of a timer/stopwatch with neon glow, bright magenta and cyan colors, 1990s demoscene style, solid black background, 256x256 pixels, nostalgic retro desktop",
    },
]

headers = {"Authorization": f"Bearer {API_KEY}"}

for app in APPS:
    filepath = OUTPUT_DIR / f"{app['id']}.png"
    if filepath.exists():
        print(f"✓ {app['id']} already exists, skipping")
        continue
    
    print(f"🎨 Regenerating {app['id']}...")
    
    payload = {
        "model": "dall-e-3",
        "prompt": app["prompt"],
        "n": 1,
        "size": "1024x1024",
        "quality": "standard",
        "style": "vivid"
    }
    
    try:
        response = requests.post(
            "https://api.openai.com/v1/images/generations",
            headers=headers,
            json=payload,
            timeout=60
        )
        
        if response.status_code == 200:
            img_url = response.json()["data"][0]["url"]
            img_response = requests.get(img_url, timeout=30)
            
            if img_response.status_code == 200:
                with open(filepath, "wb") as f:
                    f.write(img_response.content)
                size_kb = filepath.stat().st_size / 1024
                print(f"   ✅ Saved: {app['id']}.png ({size_kb:.1f} KB)")
            else:
                print(f"   ❌ Failed to download image")
        else:
            error = response.json().get("error", {}).get("message", "Unknown error")
            print(f"   ❌ API Error: {response.status_code} - {error[:80]}")
    
    except Exception as e:
        print(f"   ❌ Error: {e}")
    
    # Rate limiting - wait 4 seconds between requests
    time.sleep(4)

print("\n✅ Retry complete")

#!/usr/bin/env python3
"""
Phase 8: UI Amiga Theme + DALL-E Icon Generation
Generates retro Amiga-style icons for all child apps using OpenAI's DALL-E 3
"""

import os
import sys
import json
import time
import requests
from pathlib import Path
from typing import Optional

class DalleIconGenerator:
    """Generate Amiga-style icons using DALL-E 3"""
    
    def __init__(self, api_key: Optional[str] = None):
        self.api_key = api_key or os.environ.get("OPENAI_API_KEY")
        if not self.api_key:
            raise ValueError("OPENAI_API_KEY not found in environment or parameters")
        
        self.api_url = "https://api.openai.com/v1/images/generations"
        self.icons_dir = Path("/Users/cils/Documents/Lelectron_rare/ESP32_ZACUS/data/ui_amiga/icons")
        self.icons_dir.mkdir(parents=True, exist_ok=True)
        
        self.icon_specs = {
            "audio_player": {
                "prompt": "Retro Amiga demoscene style icon of a music player with colorful speaker waves, "
                         "vibrant cyan and magenta neon colors, 1990s aesthetic, pixel art inspired, "
                         "solid background, 256x256 pixels, nostalgic retrowave",
                "filename": "audio_player.png"
            },
            "calculator": {
                "prompt": "Retro Amiga demoscene style icon of a calculator, cyberpunk neon colors, "
                         "cyan and magenta with yellow accents, colorful 1990s aesthetic, "
                         "solid background, 256x256 pixels, nostalgic retro computer",
                "filename": "calculator.png"
            },
            "timer": {
                "prompt": "Retro Amiga demoscene style icon of a timer/stopwatch with neon glow, "
                         "bright magenta and cyan, 1990s computer art, colorful and playful, "
                         "solid background, 256x256 pixels, nostalgic digital clock",
                "filename": "timer.png"
            },
            "flashlight": {
                "prompt": "Retro Amiga demoscene style icon of a flashlight/torch with bright yellow light, "
                         "cyan and magenta frame, neon glow effect, 1990s aesthetic, "
                         "solid background, 256x256 pixels, nostalgic retro tech",
                "filename": "flashlight.png"
            },
            "camera": {
                "prompt": "Retro Amiga demoscene style icon of a camera with lens, colorful neon edges, "
                         "cyan magenta and yellow, 1990s computerized aesthetic, playful design, "
                         "solid background, 256x256 pixels, nostalgic digital camera",
                "filename": "camera.png"
            },
            "dictaphone": {
                "prompt": "Retro Amiga demoscene style icon of a dictaphone/recorder with sound waves, "
                         "neon cyan and magenta colors, 1990s audio equipment, colorful and toy-like, "
                         "solid background, 256x256 pixels, nostalgic retro microphone",
                "filename": "dictaphone.png"
            },
            "qr_scanner": {
                "prompt": "Retro Amiga demoscene style icon of QR code scanner with camera viewfinder, "
                         "bright neon colors cyan magenta yellow, 1990s cyberpunk aesthetic, "
                         "solid background, 256x256 pixels, nostalgic retro tech scanner",
                "filename": "qr_scanner.png"
            }
        }
    
    def generate_icon(self, app_name: str) -> bool:
        """Generate a single icon using DALL-E 3"""
        if app_name not in self.icon_specs:
            print(f"❌ Unknown app: {app_name}")
            return False
        
        spec = self.icon_specs[app_name]
        print(f"\n🎨 Generating {app_name}...")
        print(f"   Prompt: {spec['prompt'][:80]}...")
        
        try:
            response = requests.post(
                self.api_url,
                headers={"Authorization": f"Bearer {self.api_key}"},
                json={
                    "model": "dall-e-3",
                    "prompt": spec["prompt"],
                    "n": 1,
                    "size": "1024x1024",
                    "quality": "standard",
                    "style": "vivid"
                },
                timeout=60
            )
            
            if response.status_code != 200:
                print(f"❌ API Error: {response.status_code}")
                print(f"   {response.text[:200]}")
                return False
            
            data = response.json()
            if "data" not in data or len(data["data"]) == 0:
                print(f"❌ No image returned")
                return False
            
            image_url = data["data"][0]["url"]
            
            # Download the image
            img_response = requests.get(image_url, timeout=30)
            if img_response.status_code != 200:
                print(f"❌ Failed to download image")
                return False
            
            # Save to file
            output_path = self.icons_dir / spec["filename"]
            with open(output_path, "wb") as f:
                f.write(img_response.content)
            
            file_size_kb = len(img_response.content) / 1024
            print(f"✅ Saved: {output_path.name} ({file_size_kb:.1f} KB)")
            
            # Rate limiting for DALL-E
            time.sleep(2)
            return True
            
        except requests.exceptions.Timeout:
            print(f"❌ Timeout generating image for {app_name}")
            return False
        except Exception as e:
            print(f"❌ Error: {str(e)[:100]}")
            return False
    
    def generate_all(self) -> dict:
        """Generate all app icons"""
        results = {}
        
        print("="*60)
        print("🎨 Phase 8: DALL-E Icon Generation (Amiga Theme)")
        print("="*60)
        
        for app_name in self.icon_specs.keys():
            success = self.generate_icon(app_name)
            results[app_name] = success
            
            # Show progress
            completed = sum(1 for v in results.values() if v)
            total = len(self.icon_specs)
            print(f"\nProgress: {completed}/{total} complete")
        
        return results
    
    def create_palette_json(self):
        """Create Amiga color palette JSON"""
        palette = {
            "name": "Amiga Demoscene Retro",
            "description": "Nostalgic 1990s Amiga aesthetic with neon colors",
            "colors": {
                "background": "#000000",
                "primary_cyan": "#00FFFF",
                "primary_magenta": "#FF00FF",
                "accent_yellow": "#FFFF00",
                "accent_white": "#FFFFFF",
                "accent_blue": "#0088FF",
                "accent_green": "#00FF88",
                "shadow": "#444444"
            },
            "ui": {
                "button_bg": "#FF00FF",
                "button_text": "#FFFFFF",
                "active_bg": "#00FFFF",
                "active_text": "#000000",
                "disabled_bg": "#444444",
                "disabled_text": "#888888"
            }
        }
        
        palette_path = self.icons_dir.parent / "color_palette_amiga.json"
        with open(palette_path, "w") as f:
            json.dump(palette, f, indent=2)
        
        print(f"\n✅ Color palette saved: {palette_path.name}")

def main():
    print("\n" + "█"*60)
    print(" Phase 8: Amiga UI Theme + DALL-E Icon Generation")
    print("█"*60)
    
    # Get API key
    api_key = None
    if len(sys.argv) > 1:
        api_key = sys.argv[1]
        print(f"\n📌 Using API key from command line")
    else:
        api_key = os.environ.get("OPENAI_API_KEY")
        if api_key:
            print(f"📌 Using OPENAI_API_KEY from environment")
    
    if not api_key:
        print("""
⚠️  No API key provided!

Usage: python3 phase8_dalle_icons.py [API_KEY]

Or set environment variable:
  export OPENAI_API_KEY="sk-..."

Then run:
  python3 phase8_dalle_icons.py
        """)
        return 1
    
    # Generate icons
    try:
        generator = DalleIconGenerator(api_key)
        results = generator.generate_all()
        
        # Create palette
        generator.create_palette_json()
        
        # Summary
        print("\n" + "="*60)
        print("GENERATION SUMMARY")
        print("="*60)
        
        success_count = sum(1 for v in results.values() if v)
        total = len(results)
        
        for app_name, success in results.items():
            status = "✅" if success else "❌"
            print(f"{status} {app_name}")
        
        print(f"\nResult: {success_count}/{total} icons generated successfully")
        
        if success_count == total:
            print("\n🎉 Phase 8 Complete! All assets ready for integration.")
            return 0
        else:
            print(f"\n⚠️  {total - success_count} icon(s) failed. Check API quota/rate limits.")
            return 1
            
    except Exception as e:
        print(f"\n❌ Fatal error: {str(e)}")
        return 1

if __name__ == "__main__":
    sys.exit(main())

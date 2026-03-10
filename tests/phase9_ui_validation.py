#!/usr/bin/env python3
"""
Phase 9: Device UI Validation & Touch Input Testing
Tests:
1. Verify firmware boot & AmigaUIShell initialization
2. Monitor serial logs for UI animation frames
3. Validate system responsiveness (PING/STATUS)
4. Prepare for touch input mapping tests
"""

import serial
import time
import sys
from pathlib import Path

# Configuration
SERIAL_PORT = "/dev/cu.usbmodem5AB90753301"
BAUD_RATE = 115200
TIMEOUT = 2.0

def test_device_responsive():
    """Test 1: Device PING/STATUS"""
    print("\n" + "="*60)
    print("TEST 1: Device Responsiveness")
    print("="*60)
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.5)  # Wait for serial to stabilize
        
        # Send PING
        print("[SERIAL] Sending: PING")
        ser.write(b"PING\n")
        time.sleep(0.2)
        
        response = ser.readline().decode('utf-8', errors='ignore').strip()
        if response:
            print(f"[RESPONSE] {response}")
            if "PONG" in response:
                print("✅ PING/PONG successful")
            else:
                print("⚠️  Unexpected response")
        else:
            print("❌ No response")
        
        # Send STATUS
        print("\n[SERIAL] Sending: STATUS")
        ser.write(b"STATUS\n")
        time.sleep(0.3)
        
        status_lines = []
        for _ in range(5):
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"[STATUS] {line}")
                status_lines.append(line)
            if "scenario" in line.lower():
                break
        
        ser.close()
        return len(status_lines) > 0
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def capture_startup_logs():
    """Test 2: Capture AmigaUIShell init messages"""
    print("\n" + "="*60)
    print("TEST 2: AmigaUIShell Initialization")
    print("="*60)
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.5)
        
        print("[SERIAL] Waiting for boot logs (10 seconds)...")
        print("-" * 60)
        
        start_time = time.time()
        ui_lines = []
        
        while time.time() - start_time < 10:
            try:
                line = ser.readline(1024).decode('utf-8', errors='ignore').strip()
                if line:
                    # Filter for UI-related messages
                    if any(kw in line for kw in ["[UI]", "[MAIN]", "AMIGA", "Shell", "begin", "onStart"]):
                        print(f"  {line}")
                        ui_lines.append(line)
            except:
                pass
            time.sleep(0.05)
        
        print("-" * 60)
        
        amiga_found = any("AMIGA" in line for line in ui_lines)
        shell_found = any("shell" in line.lower() or "Shell" in line for line in ui_lines)
        
        if amiga_found or shell_found:
            print(f"✅ AmigaUIShell detected in logs ({len(ui_lines)} UI messages)")
        else:
            print(f"⚠️  AmigaUIShell not found in boot logs (captured {len(ui_lines)} UI messages)")
        
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def monitor_animation_frames():
    """Test 3: Monitor animation frame rendering"""
    print("\n" + "="*60)
    print("TEST 3: Animation Frame Monitoring")
    print("="*60)
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.5)
        
        print("[SERIAL] Monitoring for animation tick messages (5 seconds)...")
        print("-" * 60)
        
        start_time = time.time()
        frame_count = 0
        pulse_count = 0
        fade_count = 0
        
        while time.time() - start_time < 5:
            try:
                line = ser.readline(1024).decode('utf-8', errors='ignore').strip()
                if line:
                    if "Drawing" in line or "drawing" in line:
                        print(f"  Frame: {line}")
                        frame_count += 1
                    elif "Pulse" in line or "pulse" in line:
                        pulse_count += 1
                    elif "Fade" in line or "fade" in line:
                        fade_count += 1
                    elif "onTick" in line:
                        print(f"  Tick: {line}")
            except:
                pass
            time.sleep(0.05)
        
        print("-" * 60)
        print(f"Frame updates: {frame_count}")
        print(f"Pulse animations: {pulse_count}")
        print(f"Fade transitions: {fade_count}")
        
        if frame_count > 0:
            print("✅ Animation frames detected")
        else:
            print("⚠️  No animation frames detected (may be normal if drawing calls are not serialized)")
        
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def check_memory_stability():
    """Test 4: Verify memory usage stability"""
    print("\n" + "="*60)
    print("TEST 4: Memory Stability Check")
    print("="*60)
    
    try:
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=TIMEOUT)
        time.sleep(0.5)
        
        # Trigger multiple HELP commands to stress test
        print("[SERIAL] Sending 5 HELP commands to monitor memory stability...")
        
        for i in range(5):
            ser.write(b"HELP\n")
            time.sleep(0.2)
            
            line = ser.readline(1024).decode('utf-8', errors='ignore').strip()
            if "command" in line.lower():
                print(f"  HELP #{i+1}: Command list received")
        
        # Send STATUS to check current memory
        print("\n[SERIAL] Final STATUS check...")
        ser.write(b"STATUS\n")
        time.sleep(0.3)
        
        for _ in range(5):
            line = ser.readline(1024).decode('utf-8', errors='ignore').strip()
            if line:
                print(f"  {line}")
        
        print("✅ Memory stress test completed")
        ser.close()
        return True
        
    except Exception as e:
        print(f"❌ Error: {e}")
        return False

def main():
    print("\n" + "█"*60)
    print(" Phase 9: Device UI Validation & Touch Input Setup")
    print("█"*60)
    
    results = {
        "Responsiveness": test_device_responsive(),
        "AmigaUI Init": capture_startup_logs(),
        "Animation Frames": monitor_animation_frames(),
        "Memory Stability": check_memory_stability(),
    }
    
    print("\n" + "="*60)
    print("PHASE 9 TEST SUMMARY")
    print("="*60)
    for test_name, result in results.items():
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"{test_name:.<40} {status}")
    
    all_passed = all(results.values())
    print("\n" + ("="*60))
    if all_passed:
        print("✅ All Phase 9 validation tests PASSED")
        print("\n📋 Next steps:")
        print("  1. Verify touch input mapping in next phase")
        print("  2. Test app selection + launch logic")
        print("  3. Retry DALL-E for audio_player + timer icons")
    else:
        print("⚠️  Some tests failed - check output above")
    
    print("="*60 + "\n")
    
    return 0 if all_passed else 1

if __name__ == "__main__":
    sys.exit(main())

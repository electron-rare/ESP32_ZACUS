import serial
import time

s = serial.Serial('/dev/cu.usbmodem5AB90753301', 115200, timeout=0.1)
time.sleep(0.5)
s.setDTR(False)
time.sleep(0.1)
s.setDTR(True)
time.sleep(0.5)

print('=== Boot logs (20 seconds) ===')
start = time.time()
lines = []
while time.time() - start < 20:
    try:
        line = s.readline()
        if line:
            decoded = line.decode('utf-8', errors='ignore').strip()
            if decoded and ('[SCENARIO]' in decoded or '[UI_AMIGA]' in decoded or '[APP]' in decoded or '[BOOT]' in decoded or '[UI]' in decoded):
                print(decoded)
                lines.append(decoded)
    except: pass
s.close()

scenario_logs = [l for l in lines if 'SCENARIO' in l]
amiga_logs = [l for l in lines if 'AMIGA' in l or 'amiga' in l]
print(f'\n=== Results: {len(lines)} relevant logs ===')
print(f'Scenario logs: {len(scenario_logs)}')
print(f'AmigaUI logs: {len(amiga_logs)}')

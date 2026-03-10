# Legacy PlatformIO Multi-target Snapshot

`platformio.multi-target.ini` was moved out of the repository root on 2026-03-10.

Rationale:
- keep `platformio.ini` as the single release entrypoint
- avoid ambiguous root configs during firmware build/review
- preserve the old multi-target configuration for reference only

Policy:
- release and CI must target `platformio.ini` (`freenove_esp32s3_full_with_ui`)
- do not reintroduce alternate root `platformio*.ini` files

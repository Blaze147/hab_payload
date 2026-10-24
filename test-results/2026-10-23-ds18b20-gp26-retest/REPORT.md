# DS18B20 GP26 retest after wiring adjustments

**Successful:** current full balloon firmware is flashed with DS18B20 DataPin=26
and repeated valid temperature readings are now received.

- One DS18B20-family probe found; ROM 285FE4D500000051, valid ROM CRC.
- Presence response detected, idle bus high, configured for 12-bit conversion.
- 13 logged valid samples: 22.69..22.81 C / 72.84..73.06 F.
- Logged status counts: {'CONFIG_ERROR': 1, 'OK': 13, 'READ_ERROR': 1, 'REQUEST_ERROR': 1}. Errors recovered automatically after
  rediscovery. Communication is working but was not completely error-free.
- Failed samples are invalid and omitted from fresh telemetry.
- MessageManager telemetry includes the distinct internal low-temperature
  field; internal_probe_valid=1 and local packet_result=0 were observed together.
- One telemetry submission also reported packet_result=6 (HardwareError); the
  manager retains queued work for retry. No ground RF receipt was verified.
- External MAX31865 remains separate and retains its -0.89 C correction.
- BMP280 remains detected at address 0x76, chip ID 0x58, on GP2/GP3.
- GPS fix/data and camera CAPTURED status observed during the test.
- Balloon build and flash verification passed; git diff --check passed.

Firmware remains in the full balloon application, not a standalone diagnostic.
No additional temperature offset was applied to the DS18B20. Testing was at
room temperature; this does not establish cold-temperature accuracy.

UF2 SHA256: 0afcb71cb6bf2dfcb5654930d33fe4b717686114f68c40ab65011c5c1202f4b6

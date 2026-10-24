# Balloon sensor telemetry verification

Pressure-enabled full balloon firmware was flashed after the manual BOOT/RESET request. The automatic upload attempt in upload.log failed to enter the bootloader; subsequent UF2 deployment was confirmed by live BARO readings and the new TELEMETRY record log. Archived UF2 SHA-256: 519f9cb94ad6f20c19b09fb76ce3bea42408cabe272ccec444283b6fc19a08c9.

## Coverage

The application now submits DHT temperature and humidity (GP8), internal DS18B20 temperature (GP26), external MAX31865/PT100 temperature (CS13, including existing -0.89 C correction), BMP280 pressure (I2C 0x76 on GP2/GP3), and GPS position, altitude, satellite count and validity flags to MessageManager. Camera images are not submitted. GPS speed and UTC remain USB diagnostics only. BMP280 chip temperature is used for pressure compensation, not transmitted as an additional temperature record.

BMP280 previously had identity probing only. PressureMonitor now loads factory calibration, configures acquisition, compensates raw readings, and publishes pressure. Invalid or stale sensor readings are omitted individually. BMP280 pressure outside its rated 30000–110000 Pa range is marked unavailable, relevant at balloon altitudes.

## Hardware evidence

runtime.log includes:

```
TELEMETRY QUEUED schema=A1 records=0x3F dht_centi_c=2740 rh_centi_pct=2700 internal_centi_c=2238 external_centi_c=2315 pressure_pa=82573 gps_flags=0x0F packet_result=0 delivery=unknown
```

Mask 0x3F includes all six records in one 44-byte typed message. packet_result=0 indicates a successful local send command, not remote RF delivery confirmation. Live pressure was approximately 82573 Pa (825.73 hPa). DS18B20 and DHT intermittent read failures still occur; failed values are omitted.

Repeated RP2040-to-radio command timeouts were also observed (37 accumulated in the captured session). These were present before this deployment too. The cause is unresolved; UART buffering/service timing is a hypothesis, not a confirmed diagnosis. No transport change was made based on that hypothesis. Transmission reliability remains an open issue. Ground hardware was not connected, so neither actual remote reception nor ground firmware deployment was verified.

## Validation

- Native tests with address/undefined-behavior sanitizers: 17270672 checks passed, including Bosch compensation reference and full six-record encode/RF simulation/decode.
- Split UART/RF tests with sanitizers: 1375 checks passed.
- Python decoder tests: 32 passed, including the shared full-message golden vector.
- Balloon and ground RP2040 builds passed.
- See accompanying logs for build, tests and hardware output.

# DHT22 moved to GP8

Changed EnvironmentMonitor DataPin from GP7 to GP8 and made USB diagnostic pin
output follow the configured constant. Updated the pinout documentation.

Balloon split_host build, upload and flash verification passed on RP2040 Zero
at /dev/cu.usbmodem1201. Full balloon application remains running.

Observed 6 valid DHT22 readings in 7 USB ENV reports on pin=8.
Latest: 27.5 C, 30.5 %RH.

git diff --check passed. No temperature calibration or telemetry-format changes.

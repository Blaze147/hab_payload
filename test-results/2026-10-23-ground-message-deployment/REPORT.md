# Ground MessageManager deployment — 2026-10-23

Built and flashed current split_host_ground to RP2040 Zero USB serial 503359276A727B9F. PlatformIO reported flash verification OK and reboot success. UF2 SHA-256: b0560e6309e3415b503df22c63bccf1b96fcc74ddd5b3c4f3d24e63d38fd52fd.

Current ground application uses shared MessageManager reassembly/validation and typed sensor decoding. No firmware source changes were needed. Existing workspace changes were preserved.

Native ASan/UBSan tests passed 17270672 checks; split RF/UART tests passed 1375 checks; Python bridge/decoder suite passed 32 tests. Logs are adjacent.

Post-flash runtime confirmed radio UART connected, zero observed timeouts/bad frames/overflows, SD 1024-byte write/read/verify/delete PASS, and LCD address 0x27 with zero errors. During the 35-second observation the endpoint remained searching, with zero received packets/messages. Live balloon-to-ground decoding is therefore not verified.

Only the ground RP2040 was exposed over USB. The radio coprocessor was not flashed. Existing radio firmware supports ordinary sensor packets; updating its large-message validator remains necessary before large fragmented camera transfers. Automatic camera transmission is not enabled.

The prior dashboard process was stopped for exclusive USB access and restarted with current bridge code on localhost:8765, following the ground USB serial identity. New dashboard logs are in this directory.

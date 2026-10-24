# Restart flag and watchdog firmware deployment

Both RP2040 Zeros were flashed with verified loads and full pre-flash backups:
balloon `5303284748B66A1C` and ground `503359276A727B9F`. The RP2040 firmware
supports legacy radio UART status responses while retaining the pending restart
indication until a radio advertises support. The balloon required manual BOOTSEL
entry after software USB reset requests failed while RF was still operating.

The user then disconnected the RP2040 USB connections and connected both LoRa
boards. Both boards (VID:PID `239A:800C`, USB locations `0-1.3` and `0-1.4`) were
flashed with the identical normal `split_radio` image. Both uploads succeeded;
avrdude verified 21,522 bytes on each. See `radio-location-1.3-upload.log` and
`radio-location-1.4-upload.log`.

No runtime testing was performed after the radio uploads, at the user's explicit
request. RF operation, independent MCU restart flags, dark-red timeline packets,
and hardware watchdog fault recovery remain for the user to test. Earlier
simulation and pre-radio-deployment observation files are separate evidence and
do not establish live restart-flag behavior with the newly flashed radios.

`manifest.json` records image hashes and flashed identities. The radio firmware
is identical for balloon and ground; RP2040 application firmware selects the role.

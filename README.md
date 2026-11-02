# LEDUP ESPHome Bridge

Control a Home Depot LEDUP Christmas tree from Home Assistant with an ESP32-S3.

This project supports the older LEDUP `ctr010f` remote profile. It reproduces the one-way BLE advertisements sent by the LEDUP Android app.[2]

## Tested tree

| Field | Value |
|---|---|
| Product | Home Accents Holiday 9 ft Starry Light Fraser Fir Flocked Tree |
| Model | `21LE31009` |
| Home Depot SKU | `1006248591` |
| LEDUP product ID | `ctr010f` |
| Lighting functions | 10 |
| Tested bridge | ESP32-S3, 16 MB flash, 8 MB PSRAM |
| Home Assistant link | Native ESPHome API |

The tree manual lists the model, SKU, `ctr010f` product ID, and ten lighting functions.[1]

## Compatibility warning

LEDUP product IDs select different remote protocols.

The `ctr010f` and `nctr010f` profiles are not packet-compatible. The original community ESPHome bridge targets `nctr010f`.[4]

Check the product ID in your tree manual before you flash this firmware. This project does not claim support for other LEDUP profiles.

## Home Assistant controls

The bridge creates these controls:

- A mode selector with all ten lighting functions
- A power-toggle button
- A dimmer-cycle button
- A timer-toggle button

Available lighting functions:

1. Light Show
2. Steady Warm White
3. Steady Multicolor
4. Slow Fade Warm White
5. Slow Fade Multicolor
6. Twinkle Warm White
7. Twinkle Multicolor
8. Fast Flip
9. Slow Flip
10. Slow Fade Warm White to Multicolor

Power, dimmer, and timer are buttons because the tree returns no state. A Home Assistant switch would display a state that can become incorrect.

## How it works

The tree does not use a normal paired Bluetooth connection. The LEDUP app sends commands in BLE manufacturer advertisements with manufacturer ID `0xFFF0`.

The bridge generates the same 16-byte command payload. It applies the LEDUP CRC-16 and whitening steps before each transmission.

Each command contains:

- A fixed radio address
- A packet counter
- The `ctr010f` remote group
- A key code
- A click counter
- A checksum

The packet code was tested against a known LEDUP advertisement. The implementation also matches public reverse engineering of the native advertisement encoder.[3]

## Hardware

You need:

- An ESP32-S3 development board
- A data-capable USB cable for the first flash
- A USB power supply near the tree
- Home Assistant with the ESPHome integration

This project was tested with a CH341 UART bridge. Use the board port labeled `UART` for the first flash and serial logs.

The supplied YAML sets `flash_size: 16MB`. Do not flash it unchanged to a board with less flash memory.

## Install

### 1. Get the files

```bash
git clone https://github.com/wundrellama/ledup-esphome-bridge.git
cd ledup-esphome-bridge
```

### 2. Create local secrets

```bash
cp secrets.example.yaml secrets.yaml
```

Replace every value in `secrets.yaml`.

Generate an ESPHome API encryption key:

```bash
openssl rand -base64 32
```

Generate an OTA password:

```bash
openssl rand -hex 24
```

Generate a fallback access-point password:

```bash
openssl rand -base64 18
```

Do not commit `secrets.yaml`. The repository ignores this file.

### 3. Connect the ESP32-S3

Connect the board's `UART` USB port to your computer.

Find the serial device:

```bash
ls -l /dev/serial/by-id/
```

The tested board appeared as a CH341 device on `/dev/ttyUSB0`.

### 4. Test the packet code

```bash
g++ -std=c++17 -Wall -Wextra -Werror \
  tests/test_ledup_protocol.cpp \
  -o /tmp/test_ledup_protocol
/tmp/test_ledup_protocol

g++ -std=c++17 -Wall -Wextra -Werror \
  tests/test_advertise_state.cpp \
  -o /tmp/test_advertise_state
/tmp/test_advertise_state
```

The test exits without output when it passes.

### 5. Validate and compile the ESPHome configuration

```bash
uvx --from esphome esphome config ledup_bridge.yaml
uvx --from esphome esphome compile ledup_bridge.yaml
```

You can also use an existing ESPHome installation.

### 6. Flash the board

Replace the device path if necessary:

```bash
uvx --from esphome esphome upload ledup_bridge.yaml \
  --device /dev/ttyUSB0
```

The first serial flash erases the board's current firmware.

### 7. Configure Wi-Fi

After the first boot, connect to the `LEDUP Bridge Setup` Wi-Fi network.

Use the fallback password from `secrets.yaml`. Open `http://192.168.4.1` if the setup page does not open automatically.

Select a 2.4 GHz home Wi-Fi network and enter its password. The ESP32-S3 does not support 5 GHz Wi-Fi.

### 8. Add the bridge to Home Assistant

Home Assistant should discover the ESPHome device automatically.

If discovery does not appear:

1. Open **Settings**.
2. Open **Devices & services**.
3. Select **Add Integration**.
4. Select **ESPHome**.
5. Enter `ledup-bridge.local` or the board's IP address.
6. Enter the API encryption key from `secrets.yaml`.

### 9. Test the tree

Place the bridge near the tree. Power the tree and select **Steady MLT** in Home Assistant.

If the tree changes to steady multicolor, the full control path works.

## Updates

After Wi-Fi setup, upload updates over the network:

```bash
uvx --from esphome esphome upload ledup_bridge.yaml \
  --device ledup-bridge.local
```

An OTA update preserves the saved Wi-Fi credentials.

## Troubleshooting

### The setup network is visible, but clients cannot join

Do not add a continuous `esp32_ble_tracker` scan. Continuous scanning prevented Wi-Fi association on the tested ESP32-S3.

This project uses `esp32_ble` only. That component starts the BLE transmitter without a scanner.

### Home Assistant shows the controls, but the tree does not respond

Check these items:

1. Confirm that the tree product ID is `ctr010f`.
2. Move the ESP32-S3 closer to the tree.
3. Confirm that the LEDUP app still controls the tree.
4. Confirm that the tree is in Home mode.
5. Read the ESPHome logs for `Advertisement complete` or GAP errors.

### The power state looks wrong

The protocol has one power-toggle command. It does not have separate on and off commands.

Use the button entity. Do not convert it to an optimistic switch unless you accept state drift.

### The board lost its Wi-Fi settings

Connect to `LEDUP Bridge Setup` again. Open `http://192.168.4.1` and enter the home Wi-Fi details.

## Project files

| Path | Purpose |
|---|---|
| `ledup_bridge.yaml` | ESPHome device and Home Assistant entities |
| `components/ledup_bridge/` | BLE GAP callback state machine and transmitter |
| `ledup_protocol.h` | CRC-16, whitening, and LEDUP packet generation |
| `ledup_advertise_state.h` | Host-testable advertisement state transitions |
| `tests/test_ledup_protocol.cpp` | Packet regression tests |
| `tests/test_advertise_state.cpp` | Advertisement state-machine tests |
| `secrets.example.yaml` | Safe template for local secrets |
| `.github/workflows/test.yml` | Packet, state-machine, and ESPHome build tests |

## Security

The project contains no cloud service and no tree credentials. Home Assistant communicates with the bridge through the encrypted ESPHome API.

The public repository does not contain Wi-Fi passwords, API keys, OTA passwords, Home Assistant tokens, or device-specific secrets.

The tree's one-way radio protocol is not authenticated. A nearby device can observe and replay a valid LEDUP advertisement.

ESPHome API encryption protects Home Assistant traffic. It does not add authentication to the tree's radio receiver.

## Credits

- The `ctr010f` tree information and function list come from the Home Depot manual.[1]
- The LEDUP Android app defines the remote profile behavior.[2]
- The packet algorithm was cross-checked against the MuSe protocol research.[3]
- The original `nctr010f` ESPHome bridge proved that an ESP32 could control this tree family.[4]

This project is not affiliated with Home Depot, Home Accents Holiday, LEDUP, or the referenced projects.

## License

MIT. See [`LICENSE`](LICENSE).

## Sources

[1] https://images.thdstatic.com/catalog/pdfImages/7f/7fb77bdd-0fea-4cea-adb8-3452c0857798.pdf — Home Depot 9 ft Starry Light Fraser Fir Flocked manual (21LE31009)
[2] https://play.google.com/store/apps/details?id=com.tuner168.ledup — LEDUP Android app
[3] https://github.com/arz321/MuSe-Protocol — MuSe BLE advertisement protocol reverse engineering
[4] https://github.com/ottoxgam/LEDUP-Christmas-Tree — Original nctr010f ESPHome bridge

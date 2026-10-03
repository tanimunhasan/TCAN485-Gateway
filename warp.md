# T-CAN485 RS485-to-Wi-Fi Gateway

This PlatformIO firmware runs on a LilyGo T-CAN485 / ESP32 board. It provides:

- A USB serial terminal for manual RS485 controller commands.
- RS485 communication at 9600 baud, 8N1.
- Wi-Fi station connection with automatic reconnect.
- UDP forwarding to Node-RED.
- Legacy NB-IoT-compatible Manga/Sentinel-AURA packet formatting.
- A build-selected autonomous mode for periodic Manga log polling.

## Hardware connection

Connect the T-CAN485 RS485 interface to the controller:

- A to A
- B to B
- GND to GND

The firmware uses these T-CAN485 GPIO assignments:

| Function | GPIO |
|---|---:|
| RS485 TX | 22 |
| RS485 RX | 21 |
| RS485 enable | 19 |
| RS485 callback / echo control | 17 |
| 5 V enable | 16 |

## Serial terminal

The PlatformIO serial monitor runs at 9600 baud. Firmware-controlled local echo is enabled; PlatformIO monitor echo is disabled.

Normal controller commands can be entered directly:

```text
A=?
P=?
H=?
L=1h
```

The firmware wraps a command in the RS485 controller frame:

```text
@01#<CMD=value>*<checksum>\r\n
```

For example, entering `H=?` sends:

```text
@01#H=?*C4\r\n
```

Terminal editing:

- Backspace or Delete removes one character.
- Ctrl+U clears the current input line.
- Enter sends the line.

Useful local commands:

```text
/help
/status
/commands
/addr 01
/baud 9600
/hex 40 30 31 23 48 3D 3F 2A 43 34 0D 0A
```

`/hex` sends exact bytes and does not add a line ending.

## Local configuration

The following files are intentionally ignored by Git. Create/update them locally after cloning the repository.

### Wi-Fi credentials

Edit `include/wifi_credentials.h`:

```cpp
namespace WifiCredentials {
constexpr bool kEnabled = true;
constexpr char kSsid[] = "YOUR_WIFI_NAME";
constexpr char kPassword[] = "YOUR_WIFI_PASSWORD";
}
```

The serial terminal prints the assigned IP address after a successful connection.

### Node-RED UDP destination

Edit `include/udp_config.h`:

```cpp
namespace UdpConfig {
constexpr bool kEnabled = true;
constexpr char kNodeRedHost[] = "NODE_RED_SERVER_LAN_OR_CLOUD_IP";
constexpr uint16_t kNodeRedPort = 5000;
}
```

Use the IP address of the Node-RED host, not the ESP32's own Wi-Fi address.

### Legacy cloud-envelope settings

Edit `include/legacy_uplink_config.h`:

```cpp
namespace LegacyUplinkConfig {
constexpr bool kEnabled = true;
constexpr bool kForwardRawDiagnostics = false;

constexpr char kGatewayId[] = "860123456789012";
constexpr char kCompatibilityId[] = "ESP32WIFI00000000001";

constexpr char kNtpServer[] = "pool.ntp.org";
constexpr long kUtcOffsetSeconds = 0;
constexpr int kDaylightOffsetSeconds = 0;
}
```

`kGatewayId` must contain exactly 15 decimal digits. `kCompatibilityId` is the legacy SIM/compatibility field expected by the existing Node-RED decoder.

Set `kForwardRawDiagnostics` to `true` only while debugging raw UDP transport. When enabled, every RS485 response is forwarded unchanged and will not match the legacy Manga cloud format.

## UDP and legacy Manga format

The initial UDP test forwards raw RS485 responses. In normal operation, legacy formatting is enabled and only valid Manga log-page responses are sent to Node-RED.

The controller log request is:

```text
L=<count>h
```

The `h` suffix means hexadecimal log output; it does not mean hours. The number is the requested maximum log-record count.

A non-empty response starts with:

```text
L,H,<actual-count>,...
```

followed by:

```text
<column names>\r\n
<52-character hex record>\r\n
```

The firmware validates the RS485 response frame and checksum, requires and removes the column-name line, removes controller framing, and builds this plaintext legacy payload:

```text
IMEI,MsgIdx,08,SIM,RSSI C0<TimestampHex><CountHex><RecordHex...>
```

Where:

- `IMEI`: configured 15-digit gateway ID.
- `MsgIdx`: persistent, zero-padded 4-digit message counter.
- `08`: data message type.
- `SIM`: configured compatibility ID.
- `RSSI`: current Wi-Fi RSSI in decimal dBm.
- `C0`: serial/Sentinel-AURA data marker.
- `TimestampHex`: NTP-synchronized UTC epoch, encoded as 8 uppercase hex characters.
- `CountHex`: count of included records as 2 uppercase hex characters.
- `RecordHex`: each Sentinel-AURA record copied as 52 uppercase hex characters.

RS485 framing, controller checksums, log header lines, column names, and CR/LF are not included in the final UDP payload.

Empty pages such as `L,H,00,...` are intentionally not forwarded. They contain no telemetry record.

## Firmware modes

### Manual mode

The default environment is `esp32dev`. It contains no automatic RS485 polling.

Build:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev
```

Upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev -t upload --upload-port COM8
```

### Autonomous mode

The `esp32dev-autonomous` environment sets `FIRMWARE_RUN_MODE=1`.

Build:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev-autonomous
```

Upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32dev-autonomous -t upload --upload-port COM8
```

The autonomous build treats `R=3` (`OPM_NORMAL`) as the required production
controller mode. Its startup and one-minute health-check sequence is:

1. Send `R=?` and validate the addressed RS485 response and checksum.
2. If the controller reports `R=3`, leave the running BabyBear, MummyBear, or
   DaddyBear profile and controller UTC untouched. The health check ends
   without sending either `T=?` or `T=<epoch>`.
3. If the controller reports any other mode, including `R=0` (hibernate) or
   `R=4` (test), immediately send `R=3` in the same health-check pass.
4. Send `R=?` again and require confirmation of `R=3`.
5. After that mode recovery, once NTP UTC is valid, send a fresh
   `T=<epoch>`, then send `T=?` and require confirmation. If NTP is not ready,
   retain the UTC update as pending and perform it when valid UTC is available.
6. Start periodic Manga log polling with a fixed `L=6h` request every ten
   minutes. This requests up to six unread records in hexadecimal format.

Controller management uses one RS485 transaction at a time. `R=` and `T=`
responses are local control traffic and are never forwarded as cloud telemetry.
Only valid `L=` response frames are eligible for legacy UDP formatting.
Unsolicited RS485 bytes received while no controller command is pending are
discarded silently and never passed to the cloud formatter.

A waking hibernating controller can occasionally corrupt the response's
leading `@AA` bytes while leaving `#<payload>*CC\r\n` intact. Only for a
solicited `R` transaction, the gateway can recover a checksum-valid payload
that contains either `R=<mode>` or the expected `OPM_NORMAL` acknowledgement.
Address validation remains mandatory for normal responses, and this recovery
is never used for `L=` data forwarded to the cloud.

The production health check repeats every minute. It always queries `R=?`
first and only sends `R=3` when the reported mode is not already normal. It
does not query or write UTC while `R=3` is already active. UTC is written and
verified only after the gateway recovers the controller from another mode.
This avoids restarting or otherwise disturbing a controller that is
independently running its selected gas-cycle profile, while still recovering
quickly from a reset, hibernate mode, or test mode and replacing the reset RTC.

Startup cannot block forever waiting for the controller. A health check gets
three attempts with the configured response timeout and retry delay. If all
attempts fail, log polling continues in degraded mode and the health check
retries after one minute. UTC write or confirmation failures also retry
without blocking the ten-minute log-poll schedule indefinitely.

After two consecutive invalid or timed-out `L=` responses, the ESP32 starts a
new health check beginning with `R=?`; it does not blindly send `R=3`. It
continues using the same fixed `L=6h` request after recovery.

The defaults in `include/autonomous_config.h` are:

```cpp
#define AUTONOMOUS_INTERVAL_MS 600000UL
#define AUTONOMOUS_RESPONSE_TIMEOUT_MS 8000UL
#define AUTONOMOUS_LOG_PAGE_SIZE 6
#define AUTONOMOUS_CONTROLLER_RETRY_MS 5000UL
#define AUTONOMOUS_CONTROLLER_MAX_ATTEMPTS 3
#define AUTONOMOUS_HEALTH_CHECK_INTERVAL_MS 60000UL
#define AUTONOMOUS_DEGRADED_RETRY_MS 60000UL
#define AUTONOMOUS_LOG_FAILURES_BEFORE_AUDIT 2
#define AUTONOMOUS_MIN_VALID_EPOCH 1704067200UL
#define AUTONOMOUS_TIME_DRIFT_TOLERANCE_SEC 30UL
```

Override an autonomous build setting in `platformio.ini`:

```ini
[env:esp32dev-autonomous]
extends = env:esp32dev
build_flags =
  -DFIRMWARE_RUN_MODE=1
  -DAUTONOMOUS_INTERVAL_MS=300000UL
  -DAUTONOMOUS_LOG_PAGE_SIZE=9
```

This example changes the poll interval to five minutes and requests up to nine
unread hexadecimal log records per poll. The `L=<count>h` number remains a
requested record count, not a time duration.

## Node-RED test

1. Add a UDP input node listening on the configured UDP port.
2. Connect it to a Debug node.
3. Configure the UDP node to output a string for readable legacy packets.
4. Deploy the flow.
5. Configure the ESP32 UDP host to the Node-RED server address and flash the firmware.
6. Send a manual `L=6h` request or flash the autonomous build.

Successful legacy telemetry begins with a value similar to:

```text
860123456789012,0000,08,ESP32WIFI00000000001,-70 C0...
```

## Source layout

| File | Responsibility |
|---|---|
| `src/main.cpp` | Application startup and main service loop |
| `src/rs485_transport.cpp` | T-CAN485 transceiver and UART1 control |
| `src/controller_command_sender.cpp` | RS485 command transmission, active-command tracking, and validated response snapshots |
| `src/controller_protocol.cpp` | Controller command catalogue, frame construction, and response address/checksum validation |
| `src/terminal.cpp` | USB terminal, receive buffering, local control-response routing, and diagnostics |
| `src/wifi_manager.cpp` | Wi-Fi station connection and reconnect behavior |
| `src/clock_service.cpp` | NTP UTC synchronization |
| `src/legacy_uplink_formatter.cpp` | RS485 Manga-page parsing and legacy packet formatting |
| `src/udp_forwarder.cpp` | UDP datagram transmission |
| `src/autonomous_poller.cpp` | Production mode supervision, UTC synchronization, recovery, and fixed `L=6h` scheduling |

## Build verification

Both firmware environments have been built successfully:

```text
esp32dev
esp32dev-autonomous
```

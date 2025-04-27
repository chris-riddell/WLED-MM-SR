# File: memory-bank/04-api-documentation.md
# ----
# WLED API Documentation Summary

WLED offers multiple ways for external systems and users to interact with it.

## 1. HTTP API (GET Requests)

-   **Endpoint:** `/win`
-   **Method:** GET
-   **Function:** Primary method for controlling WLED state via simple URL parameters. Often used for basic integrations and scripts.
-   **Common Parameters:**
    *   `A=<0-255>`: Set brightness.
    *   `R=<0-255>`, `G=<0-255>`, `B=<0-255>`, `W=<0-255>`: Set primary color channels.
    *   `R2=`, `G2=`, `B2=`, `W2=`: Set secondary color channels.
    *   `FX=<0-max>`: Set effect ID.
    *   `SX=<0-255>`: Set effect speed.
    *   `IX=<0-255>`: Set effect intensity.
    *   `FP=<0-max>`: Set palette ID.
    *   `CL=h<RRGGBB>` or `CL=h<WWRRGGBB>`: Set primary color via hex.
    *   `C2=`, `C3=`: Set secondary/tertiary color via hex.
    *   `PS=<1-250>`: Apply preset ID. `PS=-1` or `PS=0` turns off presets.
    *   `PL=<~|0-max>`: Control current playlist (`~`=next, `0`=stop, `>0`=load).
    *   `T=<0|1|2>`: Toggle power (0=off, 1=on, 2=toggle).
    *   `NL=<0|1>`: Control nightlight. `NL=0`: off, `NL=1`: on (default duration). `NL=>1`: on with duration `(val)` minutes.
    *   `ND`: Activate nightlight with default duration.
    *   `NT=<0-255>`: Set nightlight target brightness.
    *   `NF=<0|1|2>`: Set nightlight mode (0=fade, 1=instant, 2=colorfade, 3=sunrise).
    *   `TT=<ms>`: Set transition time for next change only.
    *   `SV=<0|1|2>`: Select segment(s) (0=unselect, 1=select, 2=select only this). Used with `SS=`.
    *   `SS=<id>`: Target segment ID for subsequent segment commands.
    *   `SM=<id>`: Set main segment ID.
    *   `S=<start>`, `S2=<stop>`: Set segment start/stop LEDs.
    *   `GP=<val>`, `SP=<val>`, `OF=<val>`: Set segment grouping, spacing, offset.
    *   `RV=<0|1>`, `MI=<0|1>`: Set segment reverse/mirror.
    *   `SB=<0-255>`: Set segment brightness/opacity.
    *   `SW=<0|1|2>`: Set segment power (on/off/toggle).
    *   `LX=<val>`, `LY=<val>`: Loxone primary/secondary color commands.
    *   `M1=<0|1>`, `M2=<0|1>`, `M3=<0|1>`: Control segment checkmarks (custom effect options).
    *   `X1=<0-255>`, `X2=<0-255>`, `X3=<0-31>`: Control segment custom sliders.
-   **Response:** Basic XML status (`/win`) or HTML confirmation page. `/url` endpoint (deprecated) returns a clickable URL.

## 2. JSON API

-   **Endpoint:** `/json` or `/json/state`, `/json/info`, `/json/si`, `/json/eff`, `/json/pal`, `/json/nodes`, `/json/cfg`, `/json/live` (optional)
-   **Method:** GET (for reading), POST (for setting state/config)
-   **Function:** Preferred method for modern integrations. Allows reading detailed state/info and setting complex states in a single request.
-   **GET Payloads:**
    *   `/json` or `/json/si`: Returns combined state and info object.
    *   `/json/state`: Returns current state object (brightness, segments, nightlight, UDP sync, etc.).
    *   `/json/info`: Returns device information (version, LED setup, MAC, IP, memory, palettes, effects, etc.).
    *   `/json/eff`: Returns list of effect names.
    *   `/json/pal`: Returns list of palette names.
    *   `/json/nodes`: Returns list of discovered WLED nodes.
    *   `/json/cfg`: Returns device configuration (`cfg.json`). Requires PIN if set.
    *   `/json/live`: (If `WLED_ENABLE_JSONLIVE` enabled) Returns current LED RGB values as JSON array (slow).
-   **POST Payload (to `/json` or `/json/state`):** A JSON object representing the desired state changes. Mirrors the structure returned by GET `/json/state`. Can include segment arrays or single segment objects. Can also include HTTP API commands via the `"win"` key.
    *   `"on": true/false/"t"`: Set power state or toggle.
    *   `"bri": <0-255>`: Set brightness.
    *   `"transition": <ms>`: Set transition time for next change in 100ms increments.
    *   `"tt": <ms>`: Set transition time for *this* change only in 100ms increments.
    *   `"ps": <id>`: Apply preset ID.
    *   `"pl": <id>`: Control playlist.
    *   `"nl": {"on": bool, "dur": mins, "mode": 0-3, "tbri": 0-255}`: Control nightlight.
    *   `"udpn": {"send": bool, "recv": bool}`: Control UDP sync send/receive.
    *   `"lor": <0|1|2>`: Set live override mode.
    *   `"live": bool`: Enter/exit live (realtime) mode.
    *   `"seg": [{segment object(s)}]`: Array or single object to modify segment(s).
        *   `"id": <id>`: Segment ID to target.
        *   `"start": <led>`, `"stop": <led>`: Set bounds. `stop=0` deletes/disables.
        *   `"len": <leds>`: Set length based on start.
        *   `"grp": <val>`, `"spc": <val>`, `"of": <val>`: Grouping, spacing, offset.
        *   `"on": bool/"t"`, `"bri": <0-255>`: Segment power/brightness.
        *   `"col": [[r,g,b,w?], [r,g,b,w?], [r,g,b,w?]]` or `["#RRGGBBWW", ...]` or `[kelvin]` : Set colors.
        *   `"fx": <id>`, `"sx": <0-255>`, `"ix": <0-255>`, `"pal": <id>`: Effect, speed, intensity, palette.
        *   `"cct": <0-255>`: Set CCT value (if applicable).
        *   `"sel": bool`: Select/deselect segment.
        *   `"rev": bool`, `"mi": bool`: Reverse/mirror.
        *   `"o1/2/3": bool`: Set segment checkmarks.
        *   `"c1/2/3": <val>`: Set segment custom sliders (`c3` is 0-31).
        *   `"i": [<idx|start>, <stop>?, <hex|array>, ...]` Set individual LEDs within segment. `start`/`stop` define range.
    *   `"psave": <id>`: Save current state to preset ID. Can include `n`, `ib`, `sb`, `sc`.
    *   `"pdel": <id>`: Delete preset ID.
-   **POST Payload (to `/json/cfg`):** JSON object mirroring `cfg.json` to modify device configuration. Requires PIN if set. Changes require saving (`doSerializeConfig = true;`).
-   **Response:** `{"success":true}` or detailed state/info if `"v":true` included in request, or error object `{"error":code}`.

## 3. WebSockets API

-   **Endpoint:** `/ws`
-   **Function:** Provides bidirectional real-time communication.
    *   **Server -> Client:** Sends full JSON state (`state` + `info`) upon connection and whenever state changes significantly (rate-limited).
    *   **Client -> Server:** Accepts JSON state objects (same format as POST `/json/state`) to control WLED. Also accepts simple `"p"` string for ping, server responds with `"pong"`.
    *   **Live View:** Accepts `{"lv":true}` to start receiving binary LED data packets (see below). Accepts `{"lv":false}` to stop.
    *   **Binary Peek:** Sends current LED state in a compact binary format to the client identified by `wsLiveClientId`. Format: `L` (char), `1` (version byte), `R1`,`G1`,`B1`, `R2`,`G2`,`B2`, ... (RGB values). 2D format: `L`, `2`, `W`, `H`, `R1`,`G1`,`B1`, ... (row-major order).

## 4. MQTT API

-   **Function:** Allows control and status reporting via an MQTT broker.
-   **Topics (Default structure, configurable):**
    *   `<deviceTopic>/status`: `online`/`offline` (LWT). Retained.
    *   `<deviceTopic>/g`: Brightness (0-255). Retained (optional).
    *   `<deviceTopic>/c`: Primary color hex (`#RRGGBBWW`). Retained (optional).
    *   `<deviceTopic>/v`: XML State API response. Retained (optional).
    *   `<deviceTopic>`: Accepts brightness (0-255), `ON`, `OFF`, `T`.
    *   `<deviceTopic>/col`: Accepts hex color (`#RRGGBBWW` or `RRGGBBWW`).
    *   `<deviceTopic>/api`: Accepts HTTP API commands (e.g., `FX=1&SX=128`) or JSON API state objects.
    *   `<groupTopic>/...`: Same as `<deviceTopic>` but applies to all devices subscribed to the group topic.
    *   `<deviceTopic>/button/<button_id>`: (Optional) Publishes button events (`short`, `long`, `double`, `on`, `off`).

## 5. UDP Realtime Protocols

-   **Function:** Receive LED data in real-time from external sources. Enabled via `receiveDirect` setting. Priority over built-in effects.
-   **Protocols:**
    *   **WLED Notifier:** Port `udpPort` (def: 21324). Used for sync, but also accepts realtime formats 1-4.
    *   **DRGB / DNRGB:** Port `udpPort`. Format `[1|2|4]<timeout><leddata>` (1=WARLS, 2=DRGB, 4=DNRGB).
    *   **DRGBW / DNRGBW:** Port `udpPort`. Format `[3|5]<timeout><leddata>` (3=DRGBW, 5=DNRGBW).
    *   **Hyperion / Raw RGB:** Port `udpRgbPort` (def: 19446). Simple `[R1 G1 B1 R2 G2 B2 ...]` stream.
    *   **E1.31 (sACN):** Port `e131Port` (def: 5568). Listens on `e131Universe`. Supports multicast/unicast.
    *   **Art-Net:** Port 6454. Listens on `e131Universe`. Responds to ArtPoll.
    *   **DDP:** Port 4048. Distributed Display Protocol. Supports RGB/RGBW.
    *   **TPM2.NET:** Port 65506 (`udpPort2`).

## 6. Serial API

-   **Function:** Control via hardware serial port (if RX/TX pins not used for LEDs/other).
-   **Protocols:**
    *   **Adalight:** Standard protocol for ambilight-style control.
    *   **TPM2 Serial:** Similar to TPM2.NET but over serial.
    *   **JSON API:** Accepts JSON state objects.
    *   **HTTP API:** Accepts HTTP GET commands (without `win&`).
    *   **Improv Serial:** WiFi provisioning protocol.
    *   **Baud Rate Change:** Send single byte `0xB0` to `0xB7` to change baud rate.
    *   **LED Data Request:** `l` sends JSON array, `L` sends binary TPM2 packet.
    *   **Continuous Streaming:** `O` starts, `o` stops binary TPM2 streaming.

## 7. Other Integrations

-   **Alexa:** Emulates a Hue bridge via Espalexa library. Allows On/Off, Brightness, Color, Preset control via voice.
-   **Hue Sync:** Polls a Philips Hue bridge and mirrors the state of a selected Hue light.
-   **DMX Output:** Sends DMX data via MAX485 interface (requires `WLED_ENABLE_DMX`). Configurable fixture mapping.
-   **DMX Input:** Receives DMX data via MAX485 interface (requires `WLED_ENABLE_DMX_INPUT`).
-   **Infrared Remote:** Decodes signals from various IR remotes (`ir.cpp`, `ir_codes.h`).
-   **Loxone:** Parses specific Loxone commands (`lx_parser.cpp`).
-   **ESP-NOW Remote:** Receives commands from paired ESP-NOW remotes (e.g., Wizmote).
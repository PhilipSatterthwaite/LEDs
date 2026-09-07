"""Drive the Kasa lamp from the same MQTT topic as the LED strip.

The phone app publishes one command; the Mega paints the strip and this
paints the lamp. Neither knows about the other -- they just both listen.

Why this exists rather than the Mega talking to the bulb directly: the bulb's
firmware moved to KLAP, which needs SHA-256, AES-128-CBC and an HTTP session
with cookies. The ESP-01's AT firmware cannot do any of that.

One wrinkle worth recording: python-kasa maps the IOT.SMARTBULB family to the
v1 (MD5) KLAP transport, and login_version does not override it. This KL125 is
IOT-generation hardware running the newer v2 (SHA) auth, so the transport is
constructed explicitly. Letting the library infer it fails with
AuthenticationError even when the credentials are perfectly correct.

Credentials come from the environment, never the source:

    setx TPLINK_USER "you@example.com"
    setx TPLINK_PASS "your-password"

then open a new terminal and run.
"""

from __future__ import annotations

import asyncio
import os
import signal
import sys

import paho.mqtt.client as mqtt
from kasa import Credentials, DeviceConfig
from kasa.iot import IotBulb
from kasa.protocols.iotprotocol import IotProtocol
from kasa.transports.klaptransport import KlapTransportV2

BROKER = "broker.hivemq.com"
PORT = 1883
TOPIC = "lakeside/ps1639a/cmd"

BULB_HOST = os.environ.get("BULB_HOST", "10.9.47.3")
MAX_BRIGHTNESS = 50          # matches the sketch's ceiling

# Hue/saturation for each command the app sends. Values mirror the strip's
# colours so both land on the same shade.
COLOURS: dict[str, tuple[int, int]] = {
    "r": (0, 100), "g": (120, 100), "b": (240, 100),
    "w": (0, 0), "wm": (30, 78), "y": (55, 100),
    "p": (275, 100), "lg": (120, 76), "lb": (180, 100),
    "a": (195, 100), "c": (215, 100),
    # Multicoloured scenes have no single equivalent, so the lamp holds a warm
    # white rather than picking one arbitrary hue from the strip.
    "rb": (30, 78), "cy": (30, 78), "wo": (30, 78), "w4": (30, 78),
}


def hex_to_hs(payload: str) -> tuple[int, int] | None:
    """hRRGGBB from the colour wheel -> hue/saturation."""
    if len(payload) != 7 or payload[0] != "h":
        return None
    try:
        r, g, b = (int(payload[i:i + 2], 16) / 255 for i in (1, 3, 5))
    except ValueError:
        return None
    mx, mn = max(r, g, b), min(r, g, b)
    d = mx - mn
    if d == 0:
        h = 0.0
    elif mx == r:
        h = ((g - b) / d % 6) * 60
    elif mx == g:
        h = ((b - r) / d + 2) * 60
    else:
        h = ((r - g) / d + 4) * 60
    return int(round(h)) % 360, int(round((d / mx * 100) if mx else 0))


class Lamp:
    """Holds the connection and applies commands, reconnecting as needed."""

    def __init__(self, user: str, pw: str) -> None:
        self._user, self._pw = user, pw
        self._dev: IotBulb | None = None
        self.brightness = 20     # percent, tracks the app's slider
        self.on = True

    async def _connect(self) -> IotBulb:
        cfg = DeviceConfig(
            host=BULB_HOST,
            credentials=Credentials(self._user, self._pw),
            timeout=10,
        )
        dev = IotBulb(BULB_HOST, protocol=IotProtocol(transport=KlapTransportV2(config=cfg)))
        await dev.update()
        return dev

    async def device(self) -> IotBulb:
        if self._dev is None:
            self._dev = await self._connect()
            print(f"[lamp] connected to {self._dev.alias or BULB_HOST}", flush=True)
        return self._dev

    async def apply(self, cmd: str) -> None:
        try:
            dev = await self.device()

            if cmd == "off":
                self.on = False
                await dev.turn_off()
                return
            if cmd == "on":
                self.on = True
                await dev.turn_on()
                return

            if cmd.startswith("v") and cmd[1:].isdigit():
                # The strip caps at MAX_BRIGHTNESS; the bulb's scale is 1-100.
                self.brightness = max(1, min(100, round(int(cmd[1:]) / MAX_BRIGHTNESS * 100)))
                self.on = True
                await dev.set_brightness(self.brightness)
                return

            hs = hex_to_hs(cmd) or COLOURS.get(cmd)
            if hs is None:
                return
            self.on = True
            await dev.set_hsv(hs[0], hs[1], self.brightness)

        except Exception as e:  # noqa: BLE001
            # Most failures here are a dropped session; drop it and reconnect
            # on the next command rather than dying.
            print(f"[lamp] {type(e).__name__}: {e}", flush=True)
            self._dev = None


async def main() -> None:
    user = os.environ.get("TPLINK_USER")
    pw = os.environ.get("TPLINK_PASS")
    if not user or not pw:
        print("Set TPLINK_USER and TPLINK_PASS first:")
        print('  setx TPLINK_USER "you@example.com"')
        print('  setx TPLINK_PASS "your-password"')
        print("then open a NEW terminal and run again.")
        sys.exit(1)

    lamp = Lamp(user, pw)
    loop = asyncio.get_running_loop()
    queue: asyncio.Queue[str] = asyncio.Queue()

    def on_connect(client, _u, _f, rc, *_):
        print(f"[mqtt] connected (rc={rc}), subscribing to {TOPIC}", flush=True)
        client.subscribe(TOPIC)

    def on_message(_c, _u, msg):
        cmd = msg.payload.decode(errors="replace").strip()
        print(f"[mqtt] {cmd}", flush=True)
        loop.call_soon_threadsafe(queue.put_nowait, cmd)

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(BROKER, PORT, 60)
    client.loop_start()

    stop = asyncio.Event()
    try:
        loop.add_signal_handler(signal.SIGINT, stop.set)
    except NotImplementedError:
        pass    # Windows: Ctrl+C surfaces as KeyboardInterrupt instead

    print("[bridge] running -- Ctrl+C to stop", flush=True)
    try:
        while not stop.is_set():
            try:
                cmd = await asyncio.wait_for(queue.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue
            await lamp.apply(cmd)
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        if lamp._dev is not None:
            await lamp._dev.disconnect()
        print("\n[bridge] stopped", flush=True)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass

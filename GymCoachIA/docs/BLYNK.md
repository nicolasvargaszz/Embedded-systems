# XIAO bridge and Blynk setup

Create a template/device in your Blynk account, then copy its values into the
placeholder `firmware/XiaoBridge/secrets.h`. Use a 2.4 GHz network that does not
require a captive portal. Connect the XIAO antenna.

Create these virtual datastreams in the template, with device-write permission:

| Pin | Type | Meaning | Suggested display |
|---|---|---|---|
| V0 | String | curl / sentadilla / extension | Label |
| V1 | Integer, 0–9999 | Valid reps in the latest set | Value |
| V2 | Double, 0–250 | Manually entered weight, kg | Value/chart |
| V3 | Double, 0–864000 | Active set duration, seconds | Value |
| V4 | Integer, 0–9999 | Completed bad attempts | Value |
| V5 | Integer, 1–99 | Target valid reps | Value |
| V6 | Integer, 0–1 | Target reached | LED/value |
| V7 | String | Set ID for deduplication | Label |
| V8 | String | Complete JSON summary, same ID | Label/export integration |

Use available Blynk history/chart features for V1/V2/V4. Data retention, available
widgets and export features depend on your account. V8 is the canonical complete
record; the individual values update sequentially and are not an atomic snapshot.
This package does not add a separate database or a records/leaderboard service.

UART is newline-delimited JSON at 115200 baud, 8N1, 3.3 V logic. Wire Wio physical
pin 8/TX to XIAO D7/RX, Wio pin 10/RX to XIAO D6/TX, and connect grounds.
Wio USB telemetry and the inter-board UART are separate channels.

Every completed/manual-ended set contains protocol version, set ID, exercise,
valid reps, completed bad attempts, goal, weight, active duration and completion
flag. The Wio sends the head of an eight-entry RAM queue and retries every two
seconds. The XIAO validates it, saves it in a twelve-entry NVS queue, then replies
`{"ack":"<same-id>"}`. Duplicate queued/recent IDs are acknowledged without being
queued twice. A full queue sends a NACK; the Wio retains its entry and retries.

The XIAO connects with the Blynk SSL client, retries lost Wi-Fi and reconnects to
Blynk. It first obtains a clock using NTP for TLS certificate checks; the network
must allow NTP (normally UDP 123). While waiting, sets remain queued. TLS handshakes
inside the library can delay UART acknowledgments; Wio retries tolerate that delay.
It sends at most one virtual-pin write per 250 ms. No cloud request runs
on the Wio. Local counting therefore continues when Wi-Fi is unavailable until
the bounded queues fill.

## Persistence and delivery limits

- A UART ACK means **stored by the XIAO**, not delivered to Blynk.
- XIAO queued records and recent IDs survive reboot using a versioned NVS blob.
  An unreadable/incompatible store fails closed and is not silently erased.
- Wio RAM entries, the current set and its randomized per-boot ID disappear if
  the Wio loses power before XIAO acknowledgment. Keep it powered to drain them.
- Blynk `virtualWrite` has no application-level durable receipt in this design.
  A disconnect can replay part/all of a set; a connection that fails just after a
  write can lose the last delivery. Deduplicate downstream using V7/V8 IDs. This
  is a demo/history integration, not a guaranteed exactly-once ledger.
- Delayed uploads get Blynk's receipt time. No original UTC workout timestamp is
  invented; the Wio has only elapsed milliseconds. Queued records preserve order.
- No photographs, video or per-frame keypoints are sent to Blynk.

The USB serial log reports stored sets and queue length, without printing tokens
or passwords. If storage reports ERROR, preserve any required records before
using a board erase/reflash recovery; this code intentionally provides no automatic
erase command. Erasing flash deletes queued history and credentials.

To validate on hardware, end one manual set, confirm the UART ACK reduces `Q` on
the Wio, confirm V7/V8 have that set ID, then repeat with Wi-Fi disconnected and
reconnect. Reboot only the XIAO while queued to test persistence. Observe physical
behavior and real cloud receipt before relying on the demo.

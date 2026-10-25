"""Exercise the real recorder through a simulated serial port, never real hardware."""
import csv
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]

@unittest.skipUnless(os.name == "posix" and importlib.util.find_spec("serial"), "POSIX pseudo-terminal and pyserial required")
class RecorderIntegrationTests(unittest.TestCase):
    def test_serial_to_raw_metadata_and_ei_csv(self):
        import pty
        master, slave = pty.openpty()
        port = os.ttyname(slave)
        stop = threading.Event()
        received = bytearray()

        def emulate_wio():
            streaming = False
            deadline = time.monotonic()
            timestamp = 1000
            while not stop.is_set():
                try:
                    ready, _, _ = select.select([master], [], [], 0.02)
                    if ready:
                        received.extend(os.read(master, 4096))
                        streaming = b"STREAM 1\n" in received and b"STREAM 0\n" not in received
                    now = time.monotonic()
                    if streaming and now >= deadline:
                        os.write(master, f"F,1,{timestamp},0,150,165,170,12\n".encode())
                        timestamp += 100
                        deadline = now + 0.1
                except OSError:
                    break

        worker = threading.Thread(target=emulate_wio, daemon=True)
        worker.start()
        try:
            with tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary) / "data"
                result = subprocess.run([sys.executable, str(ROOT / "tools/grabar.py"),
                    "--port", port, "--label", "curl", "--persona", "synthetic_test",
                    "--lado", "izq", "--seconds", "2.6", "--output", str(output)],
                    capture_output=True, text=True, timeout=12)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                meta = json.loads(next((output / "raw").glob("*.json")).read_text())
                self.assertGreaterEqual(meta["valid_frames"], 20)
                self.assertEqual(meta["participant"], "synthetic_test")
                ei = next((output / "ei").glob("*.csv"))
                with ei.open(newline="") as f:
                    rows = list(csv.reader(f))
                self.assertEqual(rows[0], ["timestamp", "codo", "rodilla", "cadera", "torso"])
                self.assertEqual([int(r[0]) for r in rows[1:]], list(range(0, (len(rows) - 1) * 100, 100)))
                self.assertIn(b"SIDE LEFT\n", received)
        finally:
            stop.set(); worker.join(timeout=1)
            os.close(master); os.close(slave)

if __name__ == "__main__":
    unittest.main()

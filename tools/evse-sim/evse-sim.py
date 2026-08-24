"""
Pretend to be a CHAdeMO charger, so the box can be driven through the whole sequence on a bench.

    pip install python-can
    python evse-sim.py COM5

Sends 0x108 (charger limits) and 0x109 (charger status) every 100ms and prints what the box answers.
Both frames have to run at once, which is why a send window with a single slot is not enough: 0x109 is
the keepalive, and a gap over one second makes the box abort and open the contactors.

Values are bench values, not a real charger: 200V available, 20A, 100V present, 10A flowing.
Keep the contactor coils disconnected while this runs.
"""
import sys
import time

import can

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM5"

PARAMS = can.Message(arbitration_id=0x108, is_extended_id=False,
                     data=[0x01, 0xC8, 0x00, 0x14, 0xAA, 0x00, 0x00, 0x00])
STATUS = can.Message(arbitration_id=0x109, is_extended_id=False,
                     data=[0x01, 0x64, 0x00, 0x0A, 0x00, 0x05, 0xFF, 0x3C])

bus = can.Bus(interface="slcan", channel=PORT, bitrate=500000)
tasks = [bus.send_periodic(PARAMS, 0.1), bus.send_periodic(STATUS, 0.1)]
print("0x108 und 0x109 laufen alle 100 ms auf %s, Abbruch mit Strg+C" % PORT)

last = {}
try:
    while True:
        msg = bus.recv(1.0)
        if msg is None or msg.arbitration_id not in (0x100, 0x101, 0x102):
            continue
        # One line per ID per second is enough to see what the box wants.
        if time.time() - last.get(msg.arbitration_id, 0) < 1.0:
            continue
        last[msg.arbitration_id] = time.time()
        d = msg.data
        if msg.arbitration_id == 0x102:
            print("Box will %d V, %d A, Ladestand %d %%" % (d[1] + d[2] * 256, d[3], d[6]))
        elif msg.arbitration_id == 0x100:
            print("Box meldet max %d V, Kapazitaet %d" % (d[4] + d[5] * 256, d[6]))
        else:
            print("Box meldet Ladezeit, Frame 0x101")
except KeyboardInterrupt:
    pass
finally:
    for task in tasks:
        task.stop()
    bus.shutdown()

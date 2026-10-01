"""Reset the X4 and print its serial log for N seconds: x4log.py N"""
import glob, sys, time
import serial

port = glob.glob("/dev/cu.usbmodem*")[0]
s = serial.Serial(port, 115200, timeout=1)
s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); s.close()
time.sleep(0.5)
for _ in range(50):
    try:
        s = serial.Serial(port, 115200, timeout=1)
        break
    except serial.SerialException:
        time.sleep(0.1)
start = time.time()
while time.time() - start < float(sys.argv[1]):
    line = s.readline().decode(errors="replace").strip()
    if line:
        print("%5.1f %s" % (time.time() - start, line), flush=True)

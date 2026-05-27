import time
import sys
import os

# Ensure we can import from local lib
sys.path.append(os.getcwd())

try:
    from lib import serial_com as scm
    from lib import binary_protocol as bp
except ImportError as e:
    print(f"Error importing libraries: {e}")
    sys.exit(1)

def test_pen():
    print("--- PEN CONTROL TEST ---")
    
    # Initialize Serial
    if not scm.ser_init():
        print("ERROR: Could not connect to serial port.")
        return

    print("\nConnected to Robot.")
    print("Sending PEN UP (True)...")
    
    # Packet: q0, q1, dq0, dq1, ddq0, ddq1, pen_up
    # We send 0s for position/velocity to keep it stationary
    pkt_up = bp.encode_trajectory_point(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, True)
    scm.write_data(pkt_up)
    
    print("Waiting 2 seconds...")
    time.sleep(2)
    
    print("Sending PEN DOWN (False)...")
    pkt_down = bp.encode_trajectory_point(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, False)
    scm.write_data(pkt_down)
    
    print("Waiting 2 seconds...")
    time.sleep(2)
    
    print("Sending PEN UP (True)...")
    scm.write_data(pkt_up)
    
    print("Test Complete.")
    scm.serial_close()

if __name__ == "__main__":
    test_pen()

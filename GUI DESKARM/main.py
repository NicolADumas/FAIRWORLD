
from gevent import monkey
monkey.patch_all()

import eel
import signal
from config import SETTINGS, WEB_OPTIONS, SERIAL_PORT
from lib import serial_com as scm
from serial_manager import serial_manager
import gui_interface # Imports exposed functions
import sys
import threading

def handle_closure(sig, frame):
    print("Closing Serial and Exiting...")
    serial_manager.stop_monitor()
    if SETTINGS['ser_started']:
        scm.serial_close()
        SETTINGS['ser_started'] = False
    sys.exit(0)

import os

if __name__ == "__main__":
    signal.signal(signal.SIGINT, handle_closure)

    # Initialize Serial (Try to connect)
    SETTINGS['ser_started'] = scm.ser_init(SERIAL_PORT)
    if not SETTINGS['ser_started']:   
        print("No serial could be found, continuing anyway for GUI debug.")

    # Start Serial Monitor
    serial_manager.start_monitor()

    # GUI Setup
    # Use absolute path for layout directory
    current_dir = os.path.dirname(os.path.abspath(__file__))
    layout_dir = os.path.join(current_dir, "layout")
    eel.init(layout_dir) 
    
    try:
        eel.start(
            "index.html", 
            host=WEB_OPTIONS['host'], 
            port=WEB_OPTIONS['port'],
            mode=None,
            block=True
        )
    except (SystemExit, KeyboardInterrupt):
        pass
    finally:
        handle_closure(None, None)
        
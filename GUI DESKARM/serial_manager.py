import threading
from time import sleep, time
import eel

from lib import serial_com as scm
from lib import binary_protocol as bp
from lib.executor import TrajectoryExecutor
from state import state
from config import SETTINGS
import plotting 

class SerialManager:
    def __init__(self):
        self.stop_event = threading.Event()
        self.monitor_thread = None
        self.executor = TrajectoryExecutor(scm)

    def start_monitor(self):
        print("Starting Serial Monitor Thread...")
        self.stop_event.clear()
        self.monitor_thread = threading.Thread(target=self._monitor_loop, daemon=True)
        self.monitor_thread.start()

    def _monitor_loop(self):
        last_gui_update = 0
        GUI_UPDATE_INTERVAL = 0.05 # Limit updates to ~20Hz

        while not self.stop_event.is_set():
            if SETTINGS['ser_started']:
                try:
                    # Check for feedback (Robust reading)
                    # Process ALL available packets to avoid lag
                    while scm.get_waiting_in_buffer() >= 3:
                        b1 = scm.read_data(1)
                        if b1 and b1[0] == bp.START_BYTE_1:
                            b2 = scm.read_data(1)
                            if b2 and b2[0] == bp.START_BYTE_2:
                                # Header found
                                b_type = scm.read_data(1)
                                if b_type:
                                    if b_type[0] == bp.RESP_POS:
                                        payload = scm.read_data(12)
                                        if payload and len(payload) == 12:
                                            full_packet = b1 + b2 + b_type + payload
                                            feedback = bp.decode_feedback(full_packet)
                                            if feedback and feedback['type'] == bp.RESP_POS:
                                                state.firmware.q0 = feedback['q0']
                                                state.firmware.q1 = feedback['q1']
                                                state.firmware.last_update = time()
                                                
                                                if state.recording_active:
                                                    state.rec_data['q0'].append(feedback['q0'])
                                                    state.rec_data['q1'].append(feedback['q1'])
                                                    state.rec_data['t'].append(time())
                                    elif b_type[0] == bp.RESP_STATUS:
                                        payload = scm.read_data(5)
                                        if payload and len(payload) == 5:
                                            full_packet = b1 + b2 + b_type + payload
                                            feedback = bp.decode_feedback(full_packet)
                                            if feedback and 'buffer_level' in feedback:
                                                state.firmware.buffer_level = feedback['buffer_level']
                        else:
                            # If not a start byte, consume it to realign
                            pass
                    
                except Exception as e:
                    print(f"Serial Monitor Error: {e}")

            # Update GUI with current position (Always, even if offline)
            if time() - last_gui_update > GUI_UPDATE_INTERVAL:
                try:
                    eel.js_draw_pose([state.firmware.q0, state.firmware.q1, state.firmware.penup])
                except:
                    pass
                last_gui_update = time()
            
            sleep(0.005) # Fast polling

    def stop_monitor(self):
        self.stop_event.set()
        if self.monitor_thread:
            self.monitor_thread.join()

    def send_data(self, msg_type: str, **data):
        match msg_type:
            case 'trj':
                self.executor.execute(data)
            case 'cmd':
                if 'cmd_type' in data:
                    cmd = data['cmd_type']
                    if cmd == 'stop':
                        scm.send_data(bp.encode_stop_command())
                    elif cmd == 'homing':
                        scm.send_data(bp.encode_homing_command())
                    elif cmd == 'melody':
                        scm.send_data(bp.encode_melody_command(data.get('val', 1)))
                    elif cmd == 'set_tc':
                        scm.send_data(bp.encode_set_tc_command(data.get('val', 10)))

# Global Instance
serial_manager = SerialManager()

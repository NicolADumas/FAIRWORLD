from time import sleep, time
import eel
from lib import binary_protocol as bp
from config import SETTINGS
import plotting
from state import state

class TrajectoryExecutor:
    def __init__(self, serial_com):
        self.scm = serial_com

    def execute(self, data):
        """
        Executes a trajectory defined by data either via Serial (if online) or Simulation.
        data must contain: q (tuple of lists), dq, ddq.
        """
        if ('q' not in data) or ('dq' not in data) or ('ddq' not in data):
            print("Not enough data to define the trajectory")
            return 

        # Total number of points
        num_points = len(data['q'][0])
        print(f"Total Trajectory Points: {num_points}")
        
        if SETTINGS['ser_started']:
            self._execute_online(data, num_points)
        else:
            self._execute_simulation(data, num_points)

    def _execute_online(self, data, num_points):
        # Configuration for Flow Control
        FIRMWARE_BUFFER_SIZE = 50 # Assumed Firmware Buffer Size
        BATCH_SIZE = 5            # Send 5 points at a time
        
        # --- EXECUTION ENGINE ---
        
        # 1. Fill the buffer initially (Pre-roll)
        state.reset_recording()
        start_time = time()
        
        sent_count = 0
        initial_fill = min(num_points, FIRMWARE_BUFFER_SIZE - 5)
        
        print(f"Pre-rolling {initial_fill} points...")
        for i in range(initial_fill):
            packet = bp.encode_trajectory_point(
                data['q'][0][i], data['q'][1][i],
                data['dq'][0][i], data['dq'][1][i],
                data['ddq'][0][i], data['ddq'][1][i],
                int(data['q'][2][i])
            )
            self.scm.write_data(packet)
            sent_count += 1
            
        # 2. Main Execution Loop
        next_wake = time()
        tc_period = BATCH_SIZE * SETTINGS['Tc'] # Match rate to consumption
        
        while sent_count < num_points:
            if state.stop_requested:
                print("!!! TRAJECTORY ABORTED BY USER (ONLINE) !!!")
                break

            # Drift-compensating sleep
            now = time()
            sleep_time = next_wake - now
            if sleep_time > 0:
                sleep(sleep_time)
            
            # Update for next loop
            next_wake += tc_period

            # Send a batch of points
            limit = min(sent_count + BATCH_SIZE, num_points)
            for i in range(sent_count, limit):
                    packet = bp.encode_trajectory_point(
                    data['q'][0][i], data['q'][1][i],
                    data['dq'][0][i], data['dq'][1][i],
                    data['ddq'][0][i], data['ddq'][1][i],
                    int(data['q'][2][i])
                )
                    self.scm.write_data(packet)
            
            sent_count = limit
            
            # Update State for UI Visualization (Commanded Position)
            current_idx = min(limit - 1, num_points - 1)
            # REMOVED to prevent race condition with Serial Monitor feedback
            # state.firmware.q0 = data['q'][0][current_idx]
            # state.firmware.q1 = data['q'][1][current_idx]
            state.firmware.penup = data['q'][2][current_idx] # RESTORED: No race condition here, and needed for UI
            
            if sent_count % 100 == 0:
                print(f"Progress: {sent_count}/{num_points}")

        if state.stop_requested:
             print("Execution stopped.")
        else:
            print(f"TRJ SENT COMPLETE: {num_points} points")
            # Ensure final position is set
            # REMOVED: Rely on Feedback
            # state.firmware.q0 = data['q'][0][-1]
            # state.firmware.q1 = data['q'][1][-1]
            
            # Wait for the trajectory to finish physically
            total_duration = num_points * SETTINGS['Tc']
            elapsed = time() - start_time
            remaining = total_duration - elapsed
            
            if remaining > 0:
                print(f"Waiting for trajectory to finish: {remaining:.2f}s")
                sleep(remaining + 0.5) # Add 0.5s margin
        state.stop_recording()

    def _execute_simulation(self, data, num_points):
        # --- SIMULATION ENGINE ---
        print("SIMULATION MODE: Playing trajectory locally...")
        state.reset_recording()
        
        start_time = time()
        target_period = SETTINGS['Tc']

        for i in range(num_points):
            if state.stop_requested:
                print("!!! TRAJECTORY ABORTED BY USER (SIMULATION) !!!")
                break

            # Drift-compensating sleep
            now = time()
            # Determine when this specific point SHOULD be processed relative to start
            target_time = start_time + (i * target_period)
            
            sleep_time = target_time - now
            if sleep_time > 0:
                try:
                    sleep(sleep_time)
                except ValueError:
                    pass 
            
            # Update State
            state.firmware.q0 = data['q'][0][i]
            state.firmware.q1 = data['q'][1][i]
            state.firmware.penup = data['q'][2][i]
            state.firmware.last_update = time()
            
            # Notify UI (Animation)
            try:
                eel.js_draw_pose([state.firmware.q0, state.firmware.q1, state.firmware.penup])
            except:
                pass 

            if state.recording_active:
                state.rec_data['q0'].append(state.firmware.q0)
                state.rec_data['q1'].append(state.firmware.q1)
                state.rec_data['t'].append(time())
            
            if i % 100 == 0:
                print(f"Sim Progress: {i}/{num_points}")
        
        state.stop_recording()
        
        # Pass desired trajectory data to plotter
        plotting.plot_recorded_data(data['q'][0], data['q'][1], SETTINGS['Tc'], state.rec_data)

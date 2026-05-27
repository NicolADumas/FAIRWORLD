import eel
import numpy as np
import traceback
from time import sleep
import platform
try:
    import winsound
except ImportError:
    winsound = None

from lib import trajpy as tpy
from config import SETTINGS, SIZES, MAX_ACC_TOLERANCE_FACTOR, SERIAL_PORT, DEBUG_MODE


from state import state
from serial_manager import serial_manager
from lib import serial_com as scm
from lib import binary_protocol as bp
from lib import char_gen
from lib import transform
import plotting
import math
from lib import image_processor




def read_position_cartesian() -> list[float]:
    q_actual = state.last_known_q[:]
    if SETTINGS['ser_started']:
        scm.ser.reset_input_buffer()
        packet = bp.encode_pos_command()
        scm.write_data(packet)
        
        # Wait for latency
        sleep(0.1)
        
        # Read from global state (updated by serial manager)
        q_actual = [state.firmware.q0, state.firmware.q1]
        print(f"DEBUG: READ POS (from state): {q_actual}")
     
    # Convert to Cartesian
    points = tpy.dk(np.array(q_actual), SIZES)
    print(f"DEBUG: Calculated Cartesian Position: {points[0,0]:.4f}, {points[1,0]:.4f}")
    return [points[0,0], points[1,0]]

def validate_trajectory(q, dq, ddq):
    """
    Validate trajectory against speed/acceleration limits.
    Returns: (is_valid, scale_factor)
    - is_valid: True if within limits (possibly after scaling)
    - scale_factor: Factor to multiply time intervals by (1.0 if already valid, >1.0 if needs slowing)
    """
    print("\n--- TRAJECTORY VALIDATION ---")
    MAX_ACC_RAD = SETTINGS['max_acc'] * MAX_ACC_TOLERANCE_FACTOR
    
    # Find maximum velocity and acceleration
    max_v = 0.0
    max_a = 0.0
    
    for i in range(len(dq[0])):
        v0 = abs(dq[0][i])
        v1 = abs(dq[1][i])
        a0 = abs(ddq[0][i])
        a1 = abs(ddq[1][i])
        
        max_v = max(max_v, v0, v1)
        max_a = max(max_a, a0, a1)
    
    print(f"Stats: Max Vel={max_v:.2f} rad/s (limit: {SETTINGS['max_speed']}), Max Acc={max_a:.2f} rad/s^2 (limit: {MAX_ACC_RAD:.2f})")
    
    # Calculate required scale factor
    # For velocity: v' = v / scale -> need scale >= v / v_max
    # For acceleration: a' = a / scale^2 -> need scale >= sqrt(a / a_max)
    scale_v = max_v / SETTINGS['max_speed'] if max_v > SETTINGS['max_speed'] else 1.0
    scale_a = (max_a / MAX_ACC_RAD) ** 0.5 if max_a > MAX_ACC_RAD else 1.0
    
    scale_factor = max(scale_v, scale_a)
    
    if scale_factor > 1.0:
        print(f"[!] Trajectory exceeds limits. Auto-scaling by factor {scale_factor:.2f}x (slower)")
        print(f"    New max vel: {max_v/scale_factor:.2f} rad/s, New max acc: {max_a/(scale_factor**2):.2f} rad/s^2")
        return (True, scale_factor)
    else:
        print("Trajectory Dynamics: OK")
        return (True, 1.0)

def trace_trajectory(q:tuple[list,list]):
    q1 = q[0][:]
    q2 = q[1][:]
    
    # Guard against empty trajectories
    if not q1 or not q2:
        print("Warning: Empty trajectory, nothing to trace")
        return
        
    eel.js_draw_traces([q1, q2])
    eel.js_draw_pose([q1[-1], q2[-1]])

    # DEBUG
    if DEBUG_MODE:
        x = [] 
        for i in range(len(q1)):
            x.append(tpy.dk(np.array([q1[i], q2[i]]).T))
        plotting.debug_plotXY([xt[0] for xt in x], [yt[1] for yt in x], "xy")


def merge_to_polylines(data):
    if not data: return []
    
    optimized = []
    buffer = []
    
    def flush_buffer():
        if not buffer: return
        if len(buffer) == 1:
            optimized.append(buffer[0])
        else:
            # Create Polyline
            poly_points = [buffer[0]['points'][0]]
            for i in range(len(buffer)):
                poly_points.append(buffer[i]['points'][1])
                
            optimized.append({
                'type': 'polyline',
                'points': poly_points,
                'data': buffer[0]['data'] 
            })
        buffer.clear()

    for patch in data:
        # Merge criteria: type is line, NOT penup (drawing), connected to previous
        is_drawing_line = (patch['type'] == 'line') and (not patch['data'].get('penup'))
        
        if is_drawing_line:
            if not buffer:
                buffer.append(patch)
            else:
                # 1. Check connectivity
                prev = buffer[-1]
                p_end = prev['points'][1]
                p_start = patch['points'][0]
                dist = ((p_end[0]-p_start[0])**2 + (p_end[1]-p_start[1])**2)**0.5
                
                if dist < 0.001:
                    # 2. Check Collinearity (Avoid merging sharp corners)
                    # Vector 1: prev start to end
                    # Vector 2: current start to end
                    p_prev_start = prev['points'][0]
                    p_curr_end = patch['points'][1]
                    
                    v1 = (p_end[0] - p_prev_start[0], p_end[1] - p_prev_start[1])
                    v2 = (p_curr_end[0] - p_start[0], p_curr_end[1] - p_start[1])
                    
                    mag1 = (v1[0]**2 + v1[1]**2)**0.5
                    mag2 = (v2[0]**2 + v2[1]**2)**0.5
                    
                    is_collinear = True
                    if mag1 > 0.001 and mag2 > 0.001:
                        # Normalize dot product (cosine of angle)
                        dot = (v1[0]*v2[0] + v1[1]*v2[1]) / (mag1 * mag2)
                        # Clamp for safety
                        dot = max(-1.0, min(1.0, dot))
                        # HIGH PRECISION: Only merge if angle < 1 degree (cos(1) ~ 0.9998)
                        if dot < 0.9998:
                            is_collinear = False
                    
                    if is_collinear:
                        buffer.append(patch)
                    else:
                        flush_buffer()
                        buffer.append(patch)
                else:
                    flush_buffer()
                    buffer.append(patch)
        else:
            flush_buffer()
            optimized.append(patch)
            
    flush_buffer()
    flush_buffer()
    return optimized

def play_pc_melody(melody_id):
    """Simulates the robot melody on PC speakers"""
    print(f"[DEBUG] play_pc_melody called with ID: {melody_id}")
    if winsound:
        try:
            print(f"[DEBUG] Playing sound with winsound...")
            # Simple simulation of melodies
            if melody_id == 5: # Trajectory End (Ta-Da!)
                winsound.Beep(523, 100) # C5
                winsound.Beep(659, 100) # E5
                winsound.Beep(784, 100) # G5
                winsound.Beep(1046, 300) # C6
            elif melody_id == 1: # Startup
                winsound.Beep(440, 100)
                winsound.Beep(880, 100)
            elif melody_id == 2: # Text Mode Reset (Typewriter-ish)
                winsound.Beep(1000, 50)
                winsound.Beep(800, 50)
            elif melody_id == 3: # Drawing Mode Reset (Eraser-ish)
                winsound.Beep(600, 50)
                winsound.Beep(400, 100)
            elif melody_id == 4: # Image Mode Reset (Shutter-ish)
                winsound.Beep(1200, 50)
                winsound.Beep(500, 150)
        except Exception as e:
            print(f"[ERROR] Sound Error: {e}")
    else:
         print(f"[WARNING] winsound module not available.")

@eel.expose
def py_trigger_sound(sound_id=1):
    """Triggers a sound on the robot (if connected) or PC"""
    print(f"Triggering Sound ID: {sound_id}")
    if SETTINGS['ser_started']:
        try:
            # Use melody command from binary protocol
            pkt = bp.encode_melody_command(int(sound_id))
            scm.write_data(pkt)
            return True
        except Exception as e:
            print(f"Sound Trigger Error: {e}")
            return False
            
    # Fallback to PC beep if robot not connected (optional given user preference)
    # But user specifically asked "fai suonare il robot". 
    # If not connected, we can't make the robot sound. 
    return False

# --- EEL EXPOSED FUNCTIONS ---

@eel.expose
def py_log(msg):
    print(msg)

import json

def export_to_fairworld(patches):
    try:
        # Moltiplichiamo i metri di DESKARM per avere blocchi (es. 0.1m -> 10 blocchi nel gioco)
        fairworld_points = []
        for patch in patches:
            for pt in patch.get('points', []):
                x = int(pt[0] * 100)
                z = int(pt[1] * 100)
                fairworld_points.append({"x": x, "z": z})
        
        # Scrive nel file esportazione per FAIRWORLD
        export_path = r"d:\FILE OF FAIRWORLD\FAIRWORLD\assets\deskarm_export.json"
        with open(export_path, 'w') as f:
            json.dump(fairworld_points, f)
        print(f"[FAIRWORLD] Coordinate esportate per il gioco!")
    except Exception as e:
        print(f"[FAIRWORLD] Errore esportazione: {e}")

@eel.expose
def py_get_data():
    try:
        data: list = eel.js_get_data()()
        
        # ESPORTA A FAIRWORLD (C++)
        export_to_fairworld(data)
        
        # DEBUG: Show incoming data from frontend
        print(f"DEBUG py_get_data: Received {len(data)} patches")
        if data and len(data) > 0:
            p = data[0]
            print(f"DEBUG py_get_data: patch[0] = type:{p.get('type')}, points:{p.get('points')}")
        
        if len(data) < 1: 
            raise Exception("Not Enough Points to build a Trajectory")
            
        current_q = read_position_cartesian()
        print(f"Start Point: {current_q}")
        
        # Add initial path from current position
        data = [{'type':'line', 'points':[current_q, data[0]['points'][0]], 'data':{'penup':True}}] + data[::]
        
        # --- MICRO-GAP FIX (Segment Continuity) ---
        # Snap start of current segment to end of previous if close (<1mm)
        for i in range(1, len(data)):
            prev = data[i-1]
            curr = data[i]
            
            # If current is continuous (not penup)
            if not curr['data'].get('penup', False):
                 p_prev_end = prev['points'][1]
                 p_curr_start = curr['points'][0]
                 
                 dist = ((p_prev_end[0] - p_curr_start[0])**2 + (p_prev_end[1] - p_curr_start[1])**2)**0.5
                 
                 # HIGH PRECISION: Reduced gap tolerance from 1mm to 0.1mm
                 if dist < 0.0001: 
                     # Force snap
                     curr['points'][0] = p_prev_end
        # ------------------------------------------

        # OPTIMIZATION: Merge consecutive lines into Polylines for fluid execution
        data = merge_to_polylines(data)
        print(f"DEBUG: Optimized patches count: {len(data)}")
        
        # Stitch patches with Look-Ahead Blending
        q0s = []
        q1s = []
        penups = []
        ts = []
        
        # PROPAGATION: Maintain current joint position as seed for next patch
        initial_q = state.last_known_q[:]
        
        for i, patch in enumerate(data): 
            # Look-Ahead Logic for Velocity Blending
            v0 = 0.0
            v1 = 0.0
            
            # Helper: calculate angle between two vectors
            def check_collinear(p1_start, p1_end, p2_start, p2_end):
                v1_vec = (p1_end[0] - p1_start[0], p1_end[1] - p1_start[1])
                v2_vec = (p2_end[0] - p2_start[0], p2_end[1] - p2_start[1])
                mag1 = (v1_vec[0]**2 + v1_vec[1]**2)**0.5
                mag2 = (v2_vec[0]**2 + v2_vec[1]**2)**0.5
                if mag1 < 0.001 or mag2 < 0.001: return False
                dot = (v1_vec[0]*v2_vec[0] + v1_vec[1]*v2_vec[1]) / (mag1 * mag2)
                return dot > 0.95 # ~18 degrees tolerance for blending
            
            # If not the first patch, and we didn't just pen-up, and previous wasn't a pen-up
            if i > 0 and not patch['data'].get('penup', False) and not data[i-1]['data'].get('penup', False):
                prev_patch = data[i-1]
                if patch['type'] in ['line', 'polyline'] and prev_patch['type'] in ['line', 'polyline']:
                    # Extract last vector of prev
                    p1_s = prev_patch['points'][-2] if len(prev_patch['points']) > 1 else prev_patch['points'][0]
                    p1_e = prev_patch['points'][-1]
                    # Extract first vector of curr
                    p2_s = patch['points'][0]
                    p2_e = patch['points'][1] if len(patch['points']) > 1 else patch['points'][-1]
                    
                    if check_collinear(p1_s, p1_e, p2_s, p2_e):
                        v0 = min(SETTINGS['max_speed'] * 0.4, SETTINGS['max_speed'])
                
            # If there's a next patch, and it doesn't jump, and we don't jump now
            if i < len(data) - 1 and not data[i+1]['data'].get('penup', False) and not patch['data'].get('penup', False):
                next_patch = data[i+1]
                if patch['type'] in ['line', 'polyline'] and next_patch['type'] in ['line', 'polyline']:
                    p1_s = patch['points'][-2] if len(patch['points']) > 1 else patch['points'][0]
                    p1_e = patch['points'][-1]
                    p2_s = next_patch['points'][0]
                    p2_e = next_patch['points'][1] if len(next_patch['points']) > 1 else next_patch['points'][-1]
                    
                    if check_collinear(p1_s, p1_e, p2_s, p2_e):
                        v1 = min(SETTINGS['max_speed'] * 0.4, SETTINGS['max_speed'])

            # Override with Trapezoidal base for safety but blending is enabled
            # Note: The custom profile generator handles quintic/trapezoidal blending
            if v0 > 0 or v1 > 0:
                print(f"Blending Patch {i}: v0={v0:.2f}, v1={v1:.2f}")

            # Fallback safe velocities 
            if patch['type'] == 'circle' or patch['data'].get('penup', False):
                v0 = 0.0
                v1 = 0.0

            (q0s_p, q1s_p, penups_p, ts_p) = tpy.slice_trj(
                patch, 
                Tc=SETTINGS['Tc'],
                max_acc=SETTINGS['max_acc'],
                max_speed=SETTINGS['max_speed'],
                profile=SETTINGS['motion_profile'],
                sizes=SIZES,
                initial_q=initial_q,
                v0=v0,
                v1=v1
            )
            # Update seed for next segment/patch
            if q0s_p:
                initial_q = [q0s_p[-1], q1s_p[-1]]
                
            # Stitching logic
            q0s += q0s_p if len(q0s) == 0 else q0s_p[1:] 
            q1s += q1s_p if len(q1s) == 0 else q1s_p[1:]
            penups += penups_p if len(penups) == 0 else penups_p[1:]
            ts += [(t + ts[-1] if len(ts) > 0  else t) for t in (ts_p if len(ts) == 0 else ts_p[1:])]

        # --- PEN UP LOGIC (Start/End) ---
        if len(q0s) > 0:
            wait_time = 0.5
            wait_points = int(wait_time / SETTINGS['Tc'])
            
            # 1. Prepend Wait (Pen Up)
            q0s = [q0s[0]] * wait_points + q0s
            q1s = [q1s[0]] * wait_points + q1s
            penups = [1] * wait_points + penups
            
            # Shift existing timestamps
            ts = [t + wait_time for t in ts]
            # Create prefix timestamps (0 to wait_time)
            ts_prefix = [i * SETTINGS['Tc'] for i in range(wait_points)]
            ts = ts_prefix + ts

            # 2. Append Wait (Pen Up)
            q0s += [q0s[-1]] * wait_points
            q1s += [q1s[-1]] * wait_points
            penups += [1] * wait_points
            
            # Create suffix timestamps
            last_t = ts[-1]
            ts_suffix = [last_t + (i+1) * SETTINGS['Tc'] for i in range(wait_points)]
            ts += ts_suffix
            
            print(f"Added Pen Up Waits: {wait_time}s start/end. Total points: {len(q0s)}")

        q = (q0s, q1s, penups)
        dq = (tpy.find_velocities(q[0], ts), tpy.find_velocities(q[1], ts))
        ddq = (tpy.find_accelerations(dq[0], ts), tpy.find_accelerations(dq[1], ts))
        
        # Validate and get scale factor
        (is_valid, scale_factor) = validate_trajectory(q, dq, ddq)
        
        # Apply scaling if needed
        if scale_factor > 1.0:
            print(f"Applying time scaling factor: {scale_factor:.2f}x")
            # Scale time intervals
            ts_scaled = [t * scale_factor for t in ts]
            # Recalculate velocities and accelerations with scaled time
            dq = (tpy.find_velocities(q[0], ts_scaled), tpy.find_velocities(q[1], ts_scaled))
            ddq = (tpy.find_accelerations(dq[0], ts_scaled), tpy.find_accelerations(dq[1], ts_scaled))
            ts = ts_scaled
            print(f"Trajectory scaled. New duration: {ts[-1]:.2f}s")
        # Stop command is not needed because executor handles queue
        # But we might want to sound a "Completion" melody?
        
        # We send the melody command as a raw packet through serial manager 
        # BUT serial manager expects tuple (type, q, dq, ddq). 
        # We need a way to send raw bytes or a special 'cmd' type.
        # executor.py handles 'trj', 'cmd', 'stop'. 
        
        # Simplified: We just encode and send via scm directly if queue is empty?
        # No, we should append it to the executor? 
        # Executor doesn't support generic commands easily in its queue.
        # Let's send it directly via scm, but it might arrive before trajectory finishes if we aren't careful.
        # The firmware buffer handles execution. If we send generic command, it's processed immediately?
        # Firmware `process_data` handles commands. 
        # If we want it at the END, we should rely on the firmware to play it after buffer empty?
        # Or simplified: The user asked to send it "After generating trajectory".
        # Since we blast the whole trajectory to the firmware buffer, we can just append the melody command 
        # at the end of the transmission. The firmware will likely execute it when received 
        # OR if it's a specific command type, it might interrupt? 
        # Re-reading: "quando finisce la traiettoria dopo il termine del comando GENerate trajecotry"
        # This implies sending the command from Python after sending all points.
        
        state.stop_requested = False # Reset flag before start

        # Store trajectory for optional post-run plotting
        state.last_trajectory = {'q': q, 'dq': dq, 'ddq': ddq, 'ts': ts, 'mode': 'unknown'}

        serial_manager.send_data('trj', q=q, dq=dq, ddq=ddq)
        
        # Send Trajectory End Melody (ID 5)
        # 1. Physical Robot
        if SETTINGS['ser_started']:
            try:
                melody_pkt = bp.encode_melody_command(5)
                scm.write_data(melody_pkt)
                print("Sent Trajectory End Melody (ID 5)")
            except Exception as e:
                print(f"Failed to send end melody: {e}")
        
        # 2. PC Simulation (User Feedback)
        # play_pc_melody(5)
        
        if len(q0s) > 0:
             state.last_known_q = [q0s[-1], q1s[-1]]
        
        trace_trajectory(q)
        
        # DEBUG PLOTS
        if DEBUG_MODE:
            plotting.debug_plot(q[0], 'q1')
            plotting.debug_plot(dq[0], 'dq1')
            plotting.debug_plot(ddq[0], 'ddq1')
            plotting.debug_plot(q[1], 'q2')
            plotting.debug_plot(dq[1], 'dq2')
            plotting.debug_plot(ddq[1], 'ddq2')

    except Exception as e:
        print(f"Error in py_get_data: {e}")
        print(traceback.format_exc())

@eel.expose
def py_save_plots(mode_name="unknown"):
    """Called from frontend when the PLOT toggle is ON after Generate Trajectory."""
    try:
        trj = state.last_trajectory
        if trj['q'] is None:
            print("[PLOT] No trajectory stored yet.")
            return False
        trj['mode'] = mode_name
        plotting.plot_full_trajectory(
            trj['q'], trj['dq'], trj['ddq'], trj['ts'], mode_name
        )
        return True
    except Exception as e:
        print(f"[PLOT] Error saving plots: {e}")
        print(traceback.format_exc())
        return False

@eel.expose
def py_stop_trajectory():
    print("Received STOP request from UI")
    state.stop_requested = True
    
    if SETTINGS['ser_started']:
        try:
            packet = bp.encode_stop_command()
            scm.write_data(packet)
            print("Physical STOP command sent to Firmware.")
        except Exception as e:
            print(f"Failed to send STOP command: {e}")



@eel.expose
def py_homing_cmd():
    if SETTINGS['ser_started']:
        # Real Robot Homing
        packet = bp.encode_homing_command()
        print(f"Homing packet sent: {packet}")
        scm.write_data(packet)
        # We assume the robot resets. 
        # Ideally we should wait for feedback, but for now we reset state locally too.
        state.last_known_q = [0.0, 0.0]
        state.firmware.q0 = 0.0
        state.firmware.q1 = 0.0
    else:
        # Simulated Homing
        print("Homing: SIMULATION MODE")
        
        # Get start position from FIRMWARE (actual visual position)
        # Not last_known_q which might be stale
        q_start = [state.firmware.q0, state.firmware.q1]
        q_end = [0.0, 0.0]
        
        print(f"Homing from {q_start} to {q_end}")
        
        # If already at home, do nothing
        if abs(q_start[0]) < 0.01 and abs(q_start[1]) < 0.01:
            print("Already at home.")
            return

        # Generate smooth trajectory (Cycloidal)
        # Using max_acc/5 for gentle homing
        acc = SETTINGS['max_acc'] * 0.2 
        
        # Trajpy cycloidal returns tuple (functions, duration)
        # We need to compose for both joints.
        # cycloidal([start, end], acc)
        
        (f0, tf0) = tpy.cycloidal([q_start[0], q_end[0]], acc)
        (f1, tf1) = tpy.cycloidal([q_start[1], q_end[1]], acc)
        
        tf = max(tf0, tf1)
        
        # Sample points
        ts = tpy.rangef(0, SETTINGS['Tc'], tf, True)
        
        q0s = [f0[0](t) for t in ts]
        q1s = [f1[0](t) for t in ts]
        
        # Velocity/Acc (Optional for sim but good for plot)
        dq0s = [f0[1](t) for t in ts]
        dq1s = [f1[1](t) for t in ts]
        
        ddq0s = [f0[2](t) for t in ts]
        ddq1s = [f1[2](t) for t in ts] # Corrected from f0 to f1
        
        # Package for serial_manager (Sim Engine)
        # It expects tuple lists: q=(q0s, q1s, penups)
        # penups = 1 (Up) usually for homing to be safe? Or 0?
        # Let's say 1 (Up).
        penups = [1] * len(q0s)
        
        q = (q0s, q1s, penups)
        dq = (dq0s, dq1s)
        ddq = (ddq0s, ddq1s) # We don't really use this in sim, but consisteny
        
        # DEBUG: Log trajectory endpoints
        print(f"Homing Trajectory: {len(q0s)} points")
        print(f"  Start: q0={q0s[0]:.4f}, q1={q1s[0]:.4f}")
        print(f"  End:   q0={q0s[-1]:.4f}, q1={q1s[-1]:.4f}")
        
        # Send to manager
        serial_manager.send_data('trj', q=q, dq=dq, ddq=ddq)
        
        # Update last known
        state.last_known_q = [0.0, 0.0]

@eel.expose
def py_serial_online():
    return SETTINGS['ser_started']

@eel.expose
def py_serial_startup():
    print(f"Calling scm.ser_init({SERIAL_PORT})...")
    SETTINGS['ser_started'] = scm.ser_init(SERIAL_PORT)
    print(f"Serial Started? {SETTINGS['ser_started']}")
    if SETTINGS['ser_started']:
        # play_pc_melody(1) # Connection Sound
        pass

@eel.expose
def py_get_position():
    # Return current robot state for Polling (Backup for Push)
    q0, q1, pen_up = state.firmware.get_position()
    # print(f"DEBUG: py_get_position returning: {q0:.3f}, {q1:.3f}") # Spam warning
    return [q0, q1, pen_up]

@eel.expose
def py_clear_state():
    print("Clearing Backend State...")
    # Reset State
    state.recording_active = False
    state.reset_recording()
    
    # If serial is connected, maybe stop any current motion?
    # Sending empty trajectory or stop?
    # For now, just reset internal trackers.
    state.last_known_q = [state.firmware.q0, state.firmware.q1]
    
    if SETTINGS['ser_started']:
        # Optional: Send a specific invalidation command if protocol supports it
        pass
        
    return True


@eel.expose
def py_set_motion_params(max_acc, max_speed):
    print(f"Requesting Motion Params: Acc={max_acc}, Speed={max_speed}")
    try:
        acc = float(max_acc)
        speed = float(max_speed)
        
        if acc > 0:
            SETTINGS['max_acc'] = acc
        if speed > 0:
            SETTINGS['max_speed'] = speed
            
        print(f"Motion Params Updated: Acc={SETTINGS['max_acc']}, Speed={SETTINGS['max_speed']}")
        return True
    except ValueError:
        print("Invalid motion parameters")
        return False

@eel.expose
def py_set_motion_profile(profile_name):
    print(f"Requesting Motion Profile: {profile_name}")
    # Valid profiles: 'trapezoidal', 's-curve', 'quintic'
    valid_profiles = ['trapezoidal', 's-curve', 'quintic']
    
    if profile_name in valid_profiles:
        SETTINGS['motion_profile'] = profile_name
        print(f"Motion Profile switched to: {profile_name}")
        return True
    
    print(f"Invalid profile name: {profile_name}")
    return False

@eel.expose
def py_set_tc(val):
    try:
        val = float(val)
        if val < 0.001: val = 0.001
        if val > 0.1: val = 0.1
        
        SETTINGS['Tc'] = val
        tc_ms = int(val * 1000)
        print(f"Setting Tc: {val}s ({tc_ms}ms)")
        
        # Send to Firmware
        serial_manager.send_data('cmd', cmd_type='set_tc', val=tc_ms)
        return True
    except Exception as e:
        print(f"Error setting Tc: {e}")
        return False

def _apply_linear_transform(patches, x_offset, y_offset, angle_deg):
    angle_rad = math.radians(angle_deg)
    cos_a = math.cos(angle_rad)
    sin_a = math.sin(angle_rad)
    
    transformed = []
    for patch in patches:
        new_points = []
        for p in patch['points']:
            # Rotation
            x_rot = p[0] * cos_a - p[1] * sin_a
            y_rot = p[0] * sin_a + p[1] * cos_a
            # Translation
            new_points.append([x_rot + x_offset, y_rot + y_offset])
            
        transformed.append({
            'type': patch['type'],
            'points': new_points,
            'data': patch['data']
        })
    return transformed

def _apply_curved_transform(patches, radius, offset_angle):
    # Convert patches to the format expected by transform.py (if needed)
    # OR better: just implement the loop here using transform.apply_curved_transform logic
    # But since we wrote apply_curved_transform to take a list of dicts {x,y,z}, 
    # we can adapt.
    
    transformed = []
    
    for patch in patches:
        # Create a temporary trajectory list for this patch's points
        temp_traj = []
        for p in patch['points']:
            temp_traj.append({'x': p[0], 'y': p[1], 'z': 0}) # Z doesn't matter much here
            
        # Transform
        # Note: char_gen outputs X as horizontal, Y as vertical.
        # transform.apply_curved_transform maps X to Angle, Y to Radius.
        # We need to ensure scale is correct. 
        # But stick with unit units?
        
        # Call the library function
        res = transform.apply_curved_transform(temp_traj, radius, start_angle_deg=offset_angle)
        
        new_points = [[pt['x'], pt['y']] for pt in res]
        
        transformed.append({
            'type': patch['type'],
            'points': new_points,
            'data': patch['data']
        })
        
    return transformed

@eel.expose
def py_generate_text(text, options):
    print(f"Generating Text: '{text}' with options: {options}")
    try:
        # Input Validation
        if not text or not isinstance(text, str):
            print("Invalid text input: empty or not a string")
            return []
        
        if len(text) > 500:
            print(f"Text too long: {len(text)} chars (max 500)")
            return []
        
        # Validate mode
        mode = options.get('mode', 'linear')
        if mode not in ['linear', 'curved']:
            print(f"Invalid mode: {mode}")
            return []
        
        # Validate numeric parameters
        try:
            font_size = float(options.get('fontSize', 0.05))
            if not (0.01 <= font_size <= 0.2):
                print(f"Font size out of range: {font_size} (valid: 0.01-0.2)")
                return []
        except (ValueError, TypeError) as e:
            print(f"Invalid fontSize: {e}")
            return []
        
        # 1. Generate Base Text (Linear, at origin)
        # We pass start_pos=(0,0) and handle placement via transform
        patches = char_gen.text_to_traj(text, (0,0), font_size, char_spacing=font_size*0.2)
        
        # DEBUG: Show raw patches
        if patches:
            p0 = patches[0]['points'][0]
            print(f"DEBUG: Raw text patch[0] point[0]: ({p0[0]:.4f}, {p0[1]:.4f})")
        
        # 2. Apply Transform
        final_patches = []
        
        if mode == 'linear':
            try:
                x = float(options.get('x', 0.05))
                y = float(options.get('y', 0.0))
                angle = float(options.get('angle', 0.0))
                
                # Removed strict range validation - frontend handles geometry validation
                # Robot reach is ~0.328m, so values up to 0.35 are reasonable
                    
                final_patches = _apply_linear_transform(patches, x, y, angle)
            except (ValueError, TypeError) as e:
                print(f"Invalid linear parameters: {e}")
                return []
            
        elif mode == 'curved':
            try:
                radius = float(options.get('radius', 0.2))
                offset = float(options.get('offset', 90))
                
                print(f"DEBUG: Curved mode - radius={radius}, offset={offset}")
                
                # Removed strict range validation - frontend handles geometry validation
                # Just ensure radius is positive
                if radius <= 0:
                    print(f"Radius must be positive: {radius}")
                    return []
                    
                final_patches = _apply_curved_transform(patches, radius, offset)
                
                # DEBUG: Show transformed patches
                if final_patches:
                    tp0 = final_patches[0]['points'][0]
                    print(f"DEBUG: Transformed patch[0] point[0]: ({tp0[0]:.4f}, {tp0[1]:.4f})")
                    
            except (ValueError, TypeError) as e:
                print(f"Invalid curved parameters: {e}")
                return []
            
        else:
            final_patches = patches

        return final_patches
        
    except Exception as e:
        print(f"Error generating text: {e}")
        traceback.print_exc()
        return []

@eel.expose
def py_validate_text(text, options):
    # Generate the trajectory first
    patches = py_generate_text(text, options)
    
    if not patches:
        return {'valid': True, 'message': 'Empty'}

    valid = True
    msg = "OK"
    
    # Check every point against IK or Workspace limits
    # We can use tpy.ik to check if a solution exists
    for patch in patches:
        for p in patch['points']:
            try:
                # Check if point is reachable
                # tpy.ik returns numpy array of q1, q2
                # If it raises error or returns NaNs (depends on implementation), it's invalid.
                # Looking at tpy.ik (dk is imported), let's assume it might throw or we check bounds.
                # Actually commonly verification is checking if point is within reach.
                # R_min < dist < R_max
                
                x, y = p
                dist = (x**2 + y**2)**0.5
                
                # Check simple radius bounds
                l1 = SIZES['l1']
                l2 = SIZES['l2']
                max_reach = l1 + l2
                min_reach = abs(l1 - l2)
                
                if dist > max_reach * 0.99 or dist < min_reach * 1.01:
                    valid = False
                    msg = "Point out of reach"
                    break
                    
            except Exception as e:
                valid = False
                msg = f"IK Error: {e}"
                break
        if not valid: break
        
    return {'valid': valid, 'message': msg}

import os
import json

TEMPLATE_DIR = "saved_trajectories"

@eel.expose
def py_save_template(filename, data):
    print(f"Saving Template: {filename}")
    try:
        if not filename:
            raise ValueError("Filename cannot be empty")
        
        # Add .json extension if missing
        if not filename.endswith('.json'):
            filename += ".json"
            
        # Ensure dir exists (redundant check)
        if not os.path.exists(TEMPLATE_DIR):
            os.makedirs(TEMPLATE_DIR)
            
        filepath = os.path.join(TEMPLATE_DIR, filename)
        
        with open(filepath, 'w') as f:
            json.dump(data, f, indent=4)
            
        print(f"Saved to {filepath}")
        return {'success': True, 'message': f"Saved {filename}"}
        
    except Exception as e:
        print(f"Save Error: {e}")
        return {'success': False, 'message': str(e)}

@eel.expose
def py_load_template(filename):
    print(f"Loading Template: {filename}")
    try:
        filepath = os.path.join(TEMPLATE_DIR, filename)
        if not os.path.exists(filepath):
             raise FileNotFoundError(f"File {filename} not found")
             
        with open(filepath, 'r') as f:
            data = json.load(f)
            
        return {'success': True, 'data': data}
        
    except Exception as e:
        print(f"Load Error: {e}")
        return {'success': False, 'message': str(e)}

@eel.expose
def py_list_templates():
    try:
        if not os.path.exists(TEMPLATE_DIR):
            return []
            
        files = [f for f in os.listdir(TEMPLATE_DIR) if f.endswith('.json')]
        return files
        
    except Exception as e:
        print(f"List Error: {e}")
        return []

@eel.expose
def py_delete_template(filename):
    print(f"Deleting Template: {filename}")
    try:
        if not filename:
            raise ValueError("Filename cannot be empty")
            
        filepath = os.path.join(TEMPLATE_DIR, filename)
        
        if not os.path.exists(filepath):
            raise FileNotFoundError(f"File {filename} not found")
            
        os.remove(filepath)
        print(f"Deleted {filepath}")
        return {'success': True, 'message': f"Deleted {filename}"}
        
    except Exception as e:
        print(f"Delete Error: {e}")
        return {'success': False, 'message': str(e)}

@eel.expose
def py_optimize_trajectory(patches, tolerance=0.001):
    try:
        print(f"Optimizing {len(patches)} patches with tolerance {tolerance}...")
        # Ensure tolerance is float
        tolerance = float(tolerance)
        min_tol = 0.0001
        if tolerance < min_tol: tolerance = min_tol
        
        optimized = image_processor.optimize_patches(patches, tolerance)
        
        # Count total points for debug
        orig_pts = sum(len(p['points']) for p in patches)
        opt_pts = sum(len(p['points']) for p in optimized)
        
        print(f"Optimization complete. Points reduced from {orig_pts} to {opt_pts}.")
        return optimized
    except Exception as e:
        print(f"Optimization Error: {e}")
        import traceback
        traceback.print_exc()
        return patches # Return original on error

@eel.expose
def py_process_image(file_data, options):
    try:
        # Increment Generation to invalidate old processes
        state.process_generation += 1
        current_gen = state.process_generation
        
        # Reset Stop Flag
        state.STOP_PROCESSING = False
        
        print(f"Processing Image Request (Gen {current_gen}): {options.get('width')}m width, mode={options.get('mode')}")
        
        # Define Logger Callback that streams to Frontend
        def frontend_logger(msg):
            print(msg) # Keep server log
            try:
                eel.js_log_image_processing(str(msg))
                eel.sleep(0.001) # Yield to allow Websocket flush
            except Exception as e:
                print(f"Frontend Log Error: {e}")

        # Inject logger into options
        options['logger'] = frontend_logger
        
        # Define check_stop callback with generation check
        def check_stop():
            if state.STOP_PROCESSING: return True
            if state.process_generation > current_gen: return True
            return False
        
        options['check_stop'] = check_stop
        
        # Rotation is handled smartly in image_processor.py
        
        frontend_logger("Starting Backend Processing...")
        result = image_processor.process_image(file_data, options)
        frontend_logger(f"Backend Processing Finished. Returning {len(result)} patches.")
        return result
    except Exception as e:
        import traceback
        traceback.print_exc()
        print(f"CRITICAL BACKEND ERROR: {e}")
        return []

import winsound
import threading

# Note Frequencies (A=432Hz)
NOTE_D4   = 288
NOTE_F4   = 343
NOTE_Fsp4 = 363 # F#4
NOTE_Gs4  = 408 # G#4
NOTE_A4   = 432
NOTE_B4   = 485
NOTE_C5   = 514
NOTE_Cs5  = 544 # C#5
NOTE_D5   = 576
NOTE_E5   = 647
NOTE_Fsp5 = 726 # F#5
NOTE_G5   = 770
NOTE_Gs5  = 816 # G#5
NOTE_A5   = 864
NOTE_B5   = 970
NOTE_C6   = 1027
NOTE_D6   = 1153

def play_pc_sound(melody_id):
    """Plays the melody on the PC speakers using winsound (Blocking, so run in thread)."""
    return # Disable PC Sound globally for debug silence
    try:
        if melody_id == 1: # USB: D F# A F5 (Correction)
            winsound.Beep(NOTE_D4, 100)
            winsound.Beep(NOTE_Fsp4, 100)
            winsound.Beep(NOTE_A4, 100)
            winsound.Beep(NOTE_F5, 200)
        elif melody_id == 2: # Drawing: A C# E G#
            winsound.Beep(NOTE_A4, 100)
            winsound.Beep(NOTE_Cs5, 100)
            winsound.Beep(NOTE_E5, 100)
            winsound.Beep(NOTE_Gs5, 200)
        elif melody_id == 3: # Text: G# B D F#
            winsound.Beep(NOTE_Gs4, 100)
            winsound.Beep(NOTE_B4, 100)
            winsound.Beep(NOTE_D5, 100)
            winsound.Beep(NOTE_Fsp5, 200)
        elif melody_id == 4: # Image: E G# B D
            winsound.Beep(NOTE_E5, 100)
            winsound.Beep(NOTE_Gs5, 100)
            winsound.Beep(NOTE_B5, 100)
            winsound.Beep(NOTE_D6, 200)
        elif melody_id == 5: # Trajectory End: C# E G# B
            winsound.Beep(NOTE_Cs5, 100)
            winsound.Beep(NOTE_E5, 100)
            winsound.Beep(NOTE_Gs5, 100)
            winsound.Beep(NOTE_B5, 200)
        elif melody_id == 6: # Motion Change
            winsound.Beep(NOTE_C6, 50)
        elif melody_id == 7: # Ghost Toggle
            winsound.Beep(NOTE_A4, 50)
    except Exception as e:
        print(f"Error playing PC sound: {e}")

@eel.expose
def py_play_melody(melody_id):
    print(f"Requested Melody ID: {melody_id}")
    
    # ALWAYS play sound on PC for simulation/feedback
    # play_pc_sound commented out as per user request to remove debug sounds
    # threading.Thread(target=play_pc_sound, args=(int(melody_id),), daemon=True).start()

    if SETTINGS['ser_started']:
        try:
            packet = bp.encode_melody_command(int(melody_id))
            scm.write_data(packet)
            print(f"Melody command {melody_id} sent to robot.")
            return True
        except Exception as e:
            print(f"Error sending melody command: {e}")
            return False
    else:
        # print("Simulation mode: cannot play melody (on robot).")
        return False


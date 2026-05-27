import matplotlib.pyplot as plt
import numpy as np
from lib import trajpy as tpy
from config import SETTINGS
import os
from datetime import datetime

# Ensure images directory exists
os.makedirs('images', exist_ok=True)

def debug_plot(q, name="image"):
    plt.figure()
    t = [i*SETTINGS['Tc'] for i in range(len(q))]
    plt.plot(t, q)
    plt.grid(visible=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f'images/{name}_{timestamp}.png'
    plt.savefig(fname)
    plt.close()

def debug_plotXY(x, y, name="image"):
    plt.figure()
    plt.plot(x, y)
    plt.grid(visible=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f'images/{name}_{timestamp}.png'
    plt.savefig(fname)
    plt.close()

def plot_recorded_data(des_q0, des_q1, Tc, rec_data):
    if not rec_data['t']:
        print("No data recorded to plot.")
        return

    plt.close('all') # FORCE CLOSE ALL PREVIOUS FIGURES
    plt.figure()
    
    # 1. Actual Time Axis
    t0 = rec_data['t'][0]
    t_act = np.array([ti - t0 for ti in rec_data['t']])
    q0_act = np.array(rec_data['q0'])
    
    # 2. Desired Time Axis
    t_des = np.array([i * Tc for i in range(len(des_q0))])
    q0_des = np.array(des_q0)
    
    # 3. Alignment Logic (Start/End Scaling)
    try:
        # Helper to find active duration based on velocity
        def get_active_bounds(t, q, threshold=0.05):
            # Calculate velocity (simple difference)
            vel = np.gradient(q) 
            # Normalize velocity to find significant movement
            max_vel = np.max(np.abs(vel))
            if max_vel == 0: return 0, len(t)-1
            
            is_moving = np.abs(vel) > max_vel * threshold
            indices = np.where(is_moving)[0]
            
            if len(indices) < 2:
                return 0, len(t)-1
            
            return indices[0], indices[-1]

        # Find bounds in indices
        idx_s_des, idx_e_des = get_active_bounds(t_des, q0_des)
        idx_s_act, idx_e_act = get_active_bounds(t_act, q0_act)
        
        # Get times
        t_s_des, t_e_des = t_des[idx_s_des], t_des[idx_e_des]
        t_s_act, t_e_act = t_act[idx_s_act], t_act[idx_e_act]
        
        dur_des = t_e_des - t_s_des
        dur_act = t_e_act - t_s_act
        
        if dur_act > 0.1: # Avoid div by zero
            scale = dur_des / dur_act
            shift = t_s_des - (t_s_act * scale)
            
            print(f"Aligning: Scale={scale:.4f}, Shift={shift:.4f}")
            
            # Apply transformation
            t_act = t_act * scale + shift
            
    except Exception as e:
        print(f"Alignment failed: {e}")

    # 4. Plotting
    plt.plot(t_des, des_q0, '--', label='q0_des', alpha=0.7)
    plt.plot(t_des, des_q1, '--', label='q1_des', alpha=0.7)
    plt.plot(t_act, rec_data['q0'], label='q0_act', linewidth=1.5)
    plt.plot(t_act, rec_data['q1'], label='q1_act', linewidth=1.5)
    
    plt.xlabel('Time (s)')
    plt.ylabel('Joint Position (rad)')
    plt.title('Trajectory Tracking: Desired vs Actual')
    plt.legend()
    plt.grid(visible=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname1 = f'images/recorded_trajectory_{timestamp}.png'
    plt.savefig(fname1)
    plt.close('all') # CLEANUP
    print(f"Recorded trajectory plot saved at {fname1}")

    # 5. XY Plotting
    plt.figure()
    
    # Compute Desired XY
    x_des = []
    y_des = []
    for q0, q1 in zip(des_q0, des_q1):
        pos = tpy.dk(np.array([q0, q1]))
        x_des.append(pos[0][0])
        y_des.append(pos[1][0])
        
    # Compute Actual XY
    x_act = []
    y_act = []
    for q0, q1 in zip(rec_data['q0'], rec_data['q1']):
        pos = tpy.dk(np.array([q0, q1]))
        x_act.append(pos[0][0])
        y_act.append(pos[1][0])
        
    plt.plot(x_des, y_des, '--', label='Desired Path', alpha=0.7)
    plt.plot(x_act, y_act, label='Actual Path', linewidth=1.5)
    
    plt.xlabel('X Position (m)')
    plt.ylabel('Y Position (m)')
    plt.title('Path Tracking: Desired vs Actual')
    plt.legend()
    plt.grid(visible=True)
    plt.axis('equal') # Ensure aspect ratio is correct for spatial path
    fname2 = f'images/recorded_xy_{timestamp}.png'
    plt.savefig(fname2)
    plt.close('all')
    print(f"Recorded XY path plot saved at {fname2}")


def plot_full_trajectory(q, dq, ddq, ts, mode_name="unknown"):
    """
    Saves a single synchronized figure with 3 stacked subplots:
      1. Joint Position Tracking  — q0, q1 vs time
      2. Velocity Profile         — dq0, dq1 vs time
      3. Control Effort           — ddq0, ddq1 vs time (acceleration = torque proxy)

    All subplots share the same time axis for perfect visual synchronization.
    Saved in: images/<mode_name>_trajectory_analysis.png
    """
    if q is None or dq is None or ddq is None or ts is None:
        print("[PLOT] No trajectory data available to plot.")
        return

    t = np.array(ts)
    q0  = np.array(q[0])
    q1  = np.array(q[1])
    dq0 = np.array(dq[0])
    dq1 = np.array(dq[1])
    ddq0 = np.array(ddq[0])
    ddq1 = np.array(ddq[1])

    # --- Figure setup ---
    fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
    fig.suptitle(
        f'Trajectory Analysis — {mode_name.upper()} Mode',
        fontsize=14, fontweight='bold', y=0.98
    )

    # Colours
    C0, C1 = '#2196F3', '#FF5722'  # blue / orange

    # ── Subplot 1: Joint Position Tracking ──────────────────────────────────
    ax0 = axes[0]
    ax0.plot(t, np.degrees(q0), color=C0, linewidth=1.5, label='q0 (Joint 1)')
    ax0.plot(t, np.degrees(q1), color=C1, linewidth=1.5, label='q1 (Joint 2)')
    ax0.set_ylabel('Position (deg)', fontsize=10)
    ax0.set_title('Joint Position Tracking', fontsize=10, loc='left', pad=3)
    ax0.legend(loc='upper right', fontsize=8)
    ax0.grid(True, alpha=0.3)
    ax0.tick_params(labelbottom=False)

    # ── Subplot 2: Velocity Profile Adherence ───────────────────────────────
    ax1 = axes[1]
    ax1.plot(t, np.degrees(dq0), color=C0, linewidth=1.5, label='dq0 (Joint 1)')
    ax1.plot(t, np.degrees(dq1), color=C1, linewidth=1.5, label='dq1 (Joint 2)')
    ax1.axhline(0, color='white', linewidth=0.5, linestyle='--', alpha=0.4)
    ax1.set_ylabel('Velocity (deg/s)', fontsize=10)
    ax1.set_title('Velocity Profile Adherence', fontsize=10, loc='left', pad=3)
    ax1.legend(loc='upper right', fontsize=8)
    ax1.grid(True, alpha=0.3)
    ax1.tick_params(labelbottom=False)

    # ── Subplot 3: Control Effort (Acceleration proxy) ───────────────────────
    ax2 = axes[2]
    ax2.plot(t, np.degrees(ddq0), color=C0, linewidth=1.2, label='ddq0 (Joint 1)')
    ax2.plot(t, np.degrees(ddq1), color=C1, linewidth=1.2, label='ddq1 (Joint 2)')
    ax2.axhline(0, color='white', linewidth=0.5, linestyle='--', alpha=0.4)
    ax2.set_xlabel('Time (s)', fontsize=10)
    ax2.set_ylabel('Accel. (deg/s²)', fontsize=10)
    ax2.set_title('Control Effort Analysis', fontsize=10, loc='left', pad=3)
    ax2.legend(loc='upper right', fontsize=8)
    ax2.grid(True, alpha=0.3)

    # --- Shared layout ---
    plt.tight_layout(rect=[0, 0, 1, 0.97])

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    fname = f'images/{mode_name.lower()}_trajectory_analysis_{timestamp}.png'
    plt.savefig(fname, dpi=150, bbox_inches='tight')
    plt.close(fig)
    print(f"[PLOT] Saved: {fname}")

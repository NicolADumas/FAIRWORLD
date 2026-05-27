from dataclasses import dataclass, field
from typing import List, Dict, Optional
import threading

@dataclass
class FirmwareState:
    q0: float = 0.0
    q1: float = 0.0
    pen_up: bool = True
    buffer_level: int = 0
    last_update: float = 0
    _lock: threading.Lock = field(default_factory=threading.Lock, repr=False)
    
    def update_position(self, q0: float, q1: float, pen_up: bool = None):
        """Thread-safe position update"""
        with self._lock:
            self.q0 = q0
            self.q1 = q1
            if pen_up is not None:
                self.pen_up = pen_up
    
    def get_position(self) -> tuple:
        """Thread-safe position read"""
        with self._lock:
            return (self.q0, self.q1, self.pen_up)
    
    def update_buffer(self, level: int):
        """Thread-safe buffer level update"""
        with self._lock:
            self.buffer_level = level
    
@dataclass
class RobotState:
    firmware: FirmwareState = field(default_factory=FirmwareState)
    recording_active: bool = False
    stop_requested: bool = False
    rec_data: Dict[str, List[float]] = field(default_factory=lambda: {'q0': [], 'q1': [], 't': []})
    last_known_q: List[float] = field(default_factory=lambda: [0.0, 0.0])
    
    # Process Generation for Cancellation
    process_generation: int = 0
    STOP_PROCESSING: bool = False

    # Last generated trajectory (for post-run plotting)
    last_trajectory: Dict = field(default_factory=lambda: {
        'q': None, 'dq': None, 'ddq': None, 'ts': None, 'mode': 'unknown'
    })
    
    # Logging data
    log_data: Dict[str, List] = field(default_factory=lambda: {
        'time': [], 'q0': [], 'q1': [], 'dq0': [], 'dq1': [],
        'ddq0': [], 'ddq1': [], 'q0_actual': [], 'q1_actual': [],
        'dq0_actual': [], 'dq1_actual': [], 'ddq0_actual': [], 'ddq1_actual': [],
        'x': [], 'y': [], 'x_actual': [], 'y_actual': []
    })

    def reset_recording(self):
        self.rec_data = {'q0': [], 'q1': [], 't': []}
        self.recording_active = True

    def stop_recording(self):
        self.recording_active = False

# Global state instance
state = RobotState()

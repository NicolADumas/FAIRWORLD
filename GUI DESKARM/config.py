from lib import trajpy as tpy

# General Settings
# General Settings
SETTINGS = {
    'Tc': 0.01,  # s
    'data_rate': 1 * 10**-6,  # rate at which msgs are sent
    'max_acc': 0.1,  # rad/s**2
    'max_speed': 10.0, # rad/s
    'ser_started': False,
    'motion_profile': 'trapezoidal' # Default profile
}

# Serial Configuration
SERIAL_PORT = 'COM9' # Auto-detect

# Robot Physical Dimensions
SIZES = {
    'l1': 0.128,
    'l2': 0.144
}

# Web Server Options
WEB_OPTIONS = {
    'host': 'localhost',
    'port': 8000 # Fissato a 8000 per permettere l'integrazione con WebView2
}

# Trajectory Validation Limits
MAX_ACC_TOLERANCE_FACTOR = 15.0

# Debug Mode (set to True for development, False for production)
DEBUG_MODE = False

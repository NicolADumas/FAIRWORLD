import sys
import os

# Add parent directory to path
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))

def test_imports():
    print("Testing imports...")
    try:
        import config
        import state
        import plotting
        import serial_manager
        import gui_interface
        import main
        print("Imports Successful")
    except ImportError as e:
        print(f"Import Failed: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"Unexpected Error during import: {e}")
        sys.exit(1)

def test_config():
    print("Testing config...")
    from config import SETTINGS
    if 'Tc' not in SETTINGS:
        print("Config missing 'Tc'")
        sys.exit(1)
    print("Config OK")

if __name__ == "__main__":
    test_imports()
    test_config()
    print("ALL CHECKS PASSED")

import serial
import time
import serial.tools.list_ports

ser = None # serial object

def list_serial_ports() -> list[dict]:
    """Returns a list of available serial ports."""
    ports = serial.tools.list_ports.comports()
    result = []
    for port in ports:
        result.append({
            'device': port.device,
            'description': port.description
        })
    return result


def ser_init(serial_path:str) -> bool:
    global ser 
    print("Starting Serial Connection:\n")
    found = False
    
    if serial_path is None:
        # Auto-discovery of STM32 Virtual COM Port
        # STM32 VCP usually has specific VID/PID, but we'll scan generally for now
        ports = serial.tools.list_ports.comports()
        for port in ports:
            # You might want to filter by VID:PID here if known
            # e.g., if "STM32" in port.description:
            try:
                print(f"Trying {port.device}...")
                ser = serial.Serial(port.device, 115200, timeout=1) 
                found = True
                print(f"Connected to {port.device}")
                break
            except Exception as e:
                print(f"Failed to connect to {port.device}: {e}")
                
        if not found:
             # Fallback to hardcoded list if auto-discovery fails or for specific OS
             # On Linux this is usually /dev/ttyACM* or /dev/ttyUSB*
             candidate_ports = ['/dev/ttyACM0', '/dev/ttyACM1', '/dev/ttyUSB0', 'COM3', 'COM4']
             for p in candidate_ports:
                 try:
                     ser = serial.Serial(p, 115200, timeout=1)
                     found = True
                     print(f"Connected to {p}")
                     break
                 except:
                     pass
    else:
        try:
            ser = serial.Serial(serial_path, 115200, timeout=1)  # open serial port
            found = True
        except Exception as e:
            print(f"{serial_path} failed: {e}\n")
            found = False
            
    if found:
        # Auto-Reset the board via DTR
        ser.dtr = False
        time.sleep(0.1)
        ser.dtr = True
        time.sleep(2.0) # Wait for board reboot

        ser.reset_input_buffer()
        ser.reset_output_buffer()
        
    return found

def write_serial(msg:str) -> bool:
    """Legacy string write"""
    global ser
    if ser is None: return False
    if len(msg) == 0:
        msg = "EMPTY\n"
    ser.write(bytes(msg,'utf-8'))
    return True

def write_data(data: bytes) -> bool:
    """Write raw binary data"""
    global ser
    if ser is None: return False
    try:
        ser.write(data)
        return True
    except Exception as e:
        print(f"Serial Write Error: {e}")
        return False

def read_serial() -> bytes:
    """Legacy string read"""
    global ser
    if ser is None: return None
    try:
        line = ser.readline()
        return line
    except Exception as e:
        print(f"Serial Read Error: {e}")
        return None

def read_data(size: int) -> bytes:
    """Read specific number of bytes"""
    global ser
    if ser is None: return None
    try:
        data = ser.read(size)
        return data
    except Exception as e:
        print(f"Serial Data Read Error: {e}")
        return None
        
def get_waiting_in_buffer() -> int:
    global ser
    if ser is None: return 0
    return ser.in_waiting

def serial_close():
    global ser
    if ser is not None:
        try:
            ser.flush()
            ser.close() # close port
        except:
            pass
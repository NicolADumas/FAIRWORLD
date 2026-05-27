import serial.tools.list_ports
import serial
import time
from lib import binary_protocol as bp

def check_ports():
    print("Listing all available serial ports:")
    ports = serial.tools.list_ports.comports()
    
    if not ports:
        print("No serial ports found!")
        return

    for p in ports:
        print(f"--------------------------------------------------")
        print(f"Device: {p.device}")
        print(f"Name: {p.name}")
        print(f"Description: {p.description}")
        print(f"HWID: {p.hwid}")
        
        # Try handshake
        try:
            print(f"Attempting handshake on {p.device}...")
            s = serial.Serial(p.device, 115200, timeout=0.5, write_timeout=0.5)
            
            # Reset DTR (Arduino style)
            s.dtr = False
            time.sleep(0.1)
            s.dtr = True
            time.sleep(1.5) # Wait for potential bootloader
            
            s.reset_input_buffer()
            s.reset_output_buffer()
            
            # Send Handshake (Pos Request)
            packet = bp.encode_pos_command()
            s.write(packet)
            
            # Read response
            start_t = time.time()
            buffer = b''
            found = False
            while time.time() - start_t < 1.0:
                if s.in_waiting:
                    buffer += s.read(s.in_waiting)
                    if len(buffer) >= 3:
                        if buffer[0] == bp.START_BYTE_1 and buffer[1] == bp.START_BYTE_2:
                            print(f"[SUCCESS] Robot found on {p.device}!")
                            
                            # Send Melody Confirmation (ID 1 = USB Success)
                            confirm_packet = bp.encode_melody_command(1)
                            s.write(confirm_packet)
                            
                            found = True
                            break

                time.sleep(0.05)
                
            if not found:
                print(f"[FAILED] No valid response from {p.device}")
                
            s.close()
            
        except Exception as e:
            print(f"[ERROR] Could not open {p.device}: {e}")
            
    print("--------------------------------------------------")
    print("Scan complete.")

if __name__ == "__main__":
    check_ports()

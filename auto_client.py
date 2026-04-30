import subprocess
import time
import sys
import threading

if len(sys.argv) < 2:
    print("Usage: python3 auto_client.py <client_id>")
    sys.exit(1)

client_id = sys.argv[1]

# Registration, Login, Place Order, Logout, Quit
inputs = [
    "3", f"demo{client_id}", "pass123", # Register
    "2", f"demo{client_id}", "pass123", # Login
    "2", "1", "1", "1",                 # Place Order: Product 1, Qty 1, UPI
    "3",                                # View Orders
    "0", "0"                            # Logout, Quit
]

p = subprocess.Popen(["./retail_client"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)

def read_output():
    for line in iter(p.stdout.readline, ''):
        sys.stdout.write(line)
        sys.stdout.flush()

t = threading.Thread(target=read_output)
t.daemon = True
t.start()

time.sleep(1) # wait for connection
for cmd in inputs:
    print(f"\n\033[1;33m>>> [Auto-Input]: {cmd}\033[0m")
    try:
        p.stdin.write(cmd + "\n")
        p.stdin.flush()
    except:
        break
    time.sleep(1) # Give time to read

p.wait()

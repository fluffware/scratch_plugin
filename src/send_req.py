import sys
import struct

for l in sys.stdin:
    sys.stdout.buffer.write(struct.pack("I", len(l)) + bytes(l,"UTF-8"))
    sys.stdout.flush()
    

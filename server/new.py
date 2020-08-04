import socket
import struct

PORT = 4444

class Caps:
    count = 0
    times = []

    def __init__(self, filename=None):
        self.filename = filename

    def push(self, time):
        self.times.append(time)
        self.count += 1
    
    def save(self):
        with open(self.filename, 'a') as f:
            i = len(self.times)-1
            for time in self.times[::-1]:
                f.write(f'{time},{self.count - i}\n')
                i -= 1
            self.times.clear()
    
    def load(self):
        with open(self.filename, 'r') as f:
            lines = f.read().splitlines()
            if (len(lines) == 0):
                self.count = 0
                return
            self.count = int(lines[-1].split(',')[1])
        print(f"Caps: {self.count}")

            

if __name__ == '__main__':
    caps = Caps(filename='caps.csv')
    caps.load()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', PORT))

    try:
        while True:
            data, src = sock.recvfrom(1024)
            # print(f'{src[0]}:{src[1]} - {data}')

            if (len(data) < 1):
                continue

            # GET_COUNT message received
            if data[0] == 1:
                print('GET_COUNT msg received')
                out = caps.count.to_bytes(4, byteorder='little')
                sock.sendto(out, src)

            # Capdata received
            if len(data) > 1:
                count = len(data)//4
                print(f"Received {count-1} caps")
                remote_capdata = struct.unpack('<'+'L'*count, data)
                if (remote_capdata[0] > caps.count):
                    for cap in remote_capdata[1:]:
                        caps.push(cap)
                    caps.save()

    except KeyboardInterrupt:
        caps.save()
        print('Closing')
        pass
from influxdb import InfluxDBClient
import socket
import struct
import pprint

PORT = 4444

class Caps:
    count = 0
    times = []  # Time in milliseconds since epoch

    def __init__(self, filename=None):
        if type(filename) == str:
            print(f'Using CSV: {filename}')
            self.filename = filename
            self.save = self._save_csv
            self.load = self._load_csv
        elif type(filename) == InfluxDBClient:
            print('Using influxDB')
            self.filename = filename
            self.save = self._save_influx
            self.load = self._load_influx

    def push(self, time_tup):
        self.times.append(time_tup[0]*1000 + time_tup[1])
        # self.count += 1

    def print(self):
        for time in self.times:
            print(time)
    
    def _save_csv(self):
        with open(self.filename, 'a') as f:
            i = len(self.times)-1
            for time in self.times[::-1]:
                f.write(f'{time},{self.count - i}\n')
                i -= 1
            self.times.clear()
    
    def _load_csv(self):
        with open(self.filename, 'r') as f:
            lines = f.read().splitlines()
            if (len(lines) == 0):
                self.count = 0
                return
            self.count = int(lines[-1].split(',')[1])

    def _save_influx(self):
        client = self.filename
        i = len(self.times)-1
        points = []
        for time in self.times[::-1]:
            points += [{
                'measurement': 'capcounter',
                'time': time,
                'fields': {
                    'count': float(self.count - i)
                }
            }]
            i -= 1
        #pprint.pprint(points)
        client.write_points(points, time_precision='ms')
        self.times.clear()

    def _load_influx(self):
        client = self.filename
        res = client.query('SELECT last(count) FROM capcounter')
        self.count = next(res.get_points())['last']

            

if __name__ == '__main__':
    client = InfluxDBClient('localhost', 8086, database='mydb')
    caps = Caps(filename=client)

    # caps = Caps(filename='caps.csv')
    caps.load()
    print(f"Caps: {caps.count}")

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
                count = (len(data)-4)//6
                print(f"Received {count} caps")
                remote_count = struct.unpack_from('<L', data)[0]
                if remote_count > caps.count:
                    caps.count = remote_count
                    remote_capdata = struct.iter_unpack('<LH', data[4:])
                    for cap in remote_capdata:
                        print(f"{cap}")
                        caps.push(cap)
                    caps.save()
                else:
                    print(f"Error: Remote {remote_count}, local {caps.count}")

    except KeyboardInterrupt:
        caps.save()
        print(f'Saved {len(caps.times)} entries this session')
        print('Closing')
        pass

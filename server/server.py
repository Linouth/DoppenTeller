import socket
import time

LOG_DIR = 'logs'

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('0.0.0.0', 4444))

filename = LOG_DIR + '/' + time.strftime('%s') + '.log'
print('Starting server. Logfile: ' + filename)
with open(filename, 'w') as f:
    try:
        while True:
            data = sock.recv(128)
            t = time.time()
            data = data.decode()
            print(t, data)

            f.write(f'{t}, {data}\n')
    except KeyboardInterrupt:
        print('Closing...')
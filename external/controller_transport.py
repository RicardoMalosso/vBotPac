"""Persistent framed controller connection. Failure closes it; mutations never replay."""
import struct
import os
import subprocess
import threading
import queue
from collections import deque

class ControllerTransport:
    def __init__(self,controller,pid,dll,timeout=15):
        self.timeout=timeout
        isolation={'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP} if os.name=='nt' else {'start_new_session':True}
        self.process=subprocess.Popen([controller,str(pid),dll,'--stdio'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**isolation)
        self.stderr_tail=deque(maxlen=4096)
        self.stderr_reader=threading.Thread(target=self._read_stderr,daemon=True)
        self.stderr_reader.start()
        self.responses=queue.Queue(maxsize=1)
        self.closed=False
        self.reader=threading.Thread(target=self._read,daemon=True)
        self.reader.start()

    def _read_stderr(self):
        try:
            while True:
                part=self.process.stderr.read(1)
                if not part:return
                self.stderr_tail.append(part)
        except (OSError,ValueError):pass

    def diagnostic(self):
        return b''.join(list(self.stderr_tail)).decode('utf-8',errors='replace').strip()

    def _exact(self,count):
        data=bytearray()
        while len(data)<count:
            part=self.process.stdout.read(count-len(data))
            if not part:raise EOFError('Controller disconnected')
            data.extend(part)
        return bytes(data)

    def _read(self):
        try:
            while not self.closed:
                size=struct.unpack('<I',self._exact(4))[0]
                if not 0<size<=1048576:raise ValueError('Invalid response frame')
                self.responses.put(self._exact(size).decode('utf-8'))
        except Exception as error:
            if not self.closed:self.responses.put(error)

    def request(self,command):
        if self.closed:raise OSError('Controller connection closed')
        data=command.encode('utf-8')
        if not 0<len(data)<=65541:raise ValueError('Invalid command size')
        try:
            self.process.stdin.write(struct.pack('<I',len(data))+data)
            self.process.stdin.flush()
            response=self.responses.get(timeout=self.timeout)
            if self.closed:raise OSError('Controller connection closed')
            if isinstance(response,Exception):raise response
            return response
        except Exception as error:
            self.close()
            detail=self.diagnostic()
            if 'Connected. status | exec' in detail:
                raise OSError('Outdated pinj.exe: text console detected instead of framed --stdio mode. Rebuild with run.cmd or external/native.py.') from error
            if isinstance(error,queue.Empty):
                raise TimeoutError('Timed out waiting for controller response'+(': '+detail if detail else ' (no controller diagnostic)')) from error
            raise OSError(str(error)+(': '+detail if detail else '')) from error

    def close(self):
        if self.closed:return
        self.closed=True
        try:self.responses.put_nowait(OSError('Controller connection closed'))
        except queue.Full:pass
        self.process.kill() # Only our controller child; never the client.
        self.process.wait(timeout=2)
        self.stderr_reader.join(timeout=0.5)
        self.process.stdin.close();self.process.stdout.close();self.process.stderr.close()

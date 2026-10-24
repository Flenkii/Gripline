"""Minimal Windows-x64 emulation (unicorn) for the Extra Options test DLL.

Runs the real compiled code: XAML generation, menu property getters/setters, INI saving,
traffic tunables and the wheel bridge's live settings. Kernel32 is replaced by small
Python implementations backed by a temporary folder."""
import os, struct, tempfile, pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64, UC_HOOK_CODE, UcError
from unicorn.x86_const import *

def align(x, a=0x1000):
    return (x + a - 1) & ~(a - 1)

STUBS, HEAP, STACK = 0x10000000, 0x20000000, 0x30000000
GAME, GAME_SIZE = 0x140000000, 0x3a18000
STOP = STUBS + 0xff000

class Emu:
    def __init__(self, dll_path, root):
        self.root = root  # host folder that stands for C:\test
        self.mu = mu = Uc(UC_ARCH_X86, UC_MODE_64)
        self.pe = pe = pefile.PE(dll_path)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        mu.mem_map(self.base, align(pe.OPTIONAL_HEADER.SizeOfImage))
        mu.mem_write(self.base, pe.header)
        for s in pe.sections:
            data = s.get_data()[: max(s.Misc_VirtualSize, 0) or len(s.get_data())]
            mu.mem_write(self.base + s.VirtualAddress, data)
        mu.mem_map(STUBS, 0x100000)
        mu.mem_map(HEAP, 0x4000000)
        mu.mem_map(STACK, 0x200000)
        mu.mem_map(GAME, GAME_SIZE)
        mu.mem_write(STOP, b'\xf4')
        self.heap = HEAP
        self.handlers = {}
        self.next_stub = STUBS
        self.files = {}  # handle -> [path, pos, mode]
        self.next_handle = 0x100
        self.last_error = 0
        self.tick = 1000
        self.threads = []
        self.calls = []
        self.mtime = {}
        for entry in pe.DIRECTORY_ENTRY_IMPORT:
            for imp in entry.imports:
                name = imp.name.decode()
                fn = getattr(self, 'k_' + name, None)
                addr = self.stub(name, fn or self.missing(name))
                mu.mem_write(imp.address, struct.pack('<Q', addr))
        self.exports = {e.name.decode(): self.base + e.address
                        for e in pe.DIRECTORY_ENTRY_EXPORT.symbols if e.name}
        mu.hook_add(UC_HOOK_CODE, self.on_code, begin=STUBS, end=STUBS + 0xfffff)
        mu.hook_add(UC_HOOK_CODE, self.on_code, begin=GAME, end=GAME + GAME_SIZE)

    # ------------------------------------------------------------- plumbing
    def stub(self, name, fn, at=None):
        a = at if at is not None else self.next_stub
        if at is None:
            self.next_stub += 16
        self.mu.mem_write(a, b'\xc3')
        self.handlers[a] = (name, fn)
        return a

    def missing(self, name):
        def f():
            raise RuntimeError('unexpected call ' + name)
        return f

    def on_code(self, mu, address, size, user):
        h = self.handlers.get(address)
        if h:
            r = h[1]()
            mu.reg_write(UC_X86_REG_RAX, (r or 0) & 0xffffffffffffffff)

    def alloc(self, n, align_to=16):
        p = align(self.heap, align_to)
        self.heap = p + max(n, 16)
        return p

    def arg(self, i):
        regs = [UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9]
        if i < 4:
            return self.mu.reg_read(regs[i])
        return self.rq(self.mu.reg_read(UC_X86_REG_RSP) + 8 * (i + 1))

    def rq(self, a): return struct.unpack('<Q', self.mu.mem_read(a, 8))[0]
    def rd(self, a): return struct.unpack('<I', self.mu.mem_read(a, 4))[0]
    def rf(self, a): return struct.unpack('<f', self.mu.mem_read(a, 4))[0]
    def wq(self, a, v): self.mu.mem_write(a, struct.pack('<Q', v & 0xffffffffffffffff))
    def wd(self, a, v): self.mu.mem_write(a, struct.pack('<I', v & 0xffffffff))
    def wf(self, a, v): self.mu.mem_write(a, struct.pack('<f', v))

    def wstr(self, a):
        out = []
        while True:
            c = struct.unpack('<H', self.mu.mem_read(a + 2 * len(out), 2))[0]
            if c == 0:
                return ''.join(out)
            out.append(chr(c))

    def put_wstr(self, s):
        b = s.encode('utf-16le') + b'\0\0'
        p = self.alloc(len(b))
        self.mu.mem_write(p, b)
        return p

    def put_bytes(self, b):
        p = self.alloc(len(b) + 1)
        self.mu.mem_write(p, b + b'\0')
        return p

    def call(self, name, *args):
        target = self.exports[name] if isinstance(name, str) else name
        rsp = STACK + 0x100000
        rsp -= 0x100
        rsp &= ~0xf
        for i, v in enumerate(args[4:]):
            self.wq(rsp + 0x28 + 8 * i, v)
        self.wq(rsp, STOP)
        regs = [UC_X86_REG_RCX, UC_X86_REG_RDX, UC_X86_REG_R8, UC_X86_REG_R9]
        for i, v in enumerate(args[:4]):
            self.mu.reg_write(regs[i], v & 0xffffffffffffffff)
        self.mu.reg_write(UC_X86_REG_RSP, rsp)
        try:
            self.mu.emu_start(target, STOP, count=200_000_000)
        except UcError as e:
            rip = self.mu.reg_read(UC_X86_REG_RIP)
            raise RuntimeError(f'emulation error {e} at {rip:#x} (dll+{rip - self.base:#x})')
        return self.mu.reg_read(UC_X86_REG_RAX)

    # ------------------------------------------------------------- files
    def host(self, wpath):
        p = wpath.replace('/', '\\')
        if p.lower().rstrip('\\') in ('c:\\test', 'c:'):
            return self.root
        assert p.lower().startswith('c:\\test\\'), p
        return os.path.join(self.root, *p[8:].split('\\'))

    def k_CreateFileW(self):
        path = self.host(self.wstr(self.arg(0)))
        access, disposition = self.arg(1) & 0xffffffff, self.arg(4) & 0xffffffff  # DWORDs
        exists = os.path.exists(path)
        if disposition == 3 and not exists:  # OPEN_EXISTING
            self.last_error = 2
            return 0xffffffffffffffff
        if disposition == 1 and exists:  # CREATE_NEW
            self.last_error = 80
            return 0xffffffffffffffff
        if disposition == 2 or (disposition in (1, 4) and not exists):
            open(path, 'wb').close()
            self.touch(path)
        h = self.next_handle
        self.next_handle += 4
        append = access == 4  # FILE_APPEND_DATA
        self.files[h] = [path, os.path.getsize(path) if append else 0, append]
        self.last_error = 183 if (disposition == 4 and exists) else 0
        return h

    def touch(self, path):
        self.tick += 1
        self.mtime[os.path.abspath(path)] = self.tick

    def k_ReadFile(self):
        h, buf, n, got = self.arg(0), self.arg(1), self.arg(2), self.arg(3)
        f = self.files[h]
        with open(f[0], 'rb') as fp:
            fp.seek(f[1])
            data = fp.read(n)
        self.mu.mem_write(buf, data)
        f[1] += len(data)
        if got:
            self.wd(got, len(data))
        return 1

    def k_WriteFile(self):
        h, buf, n, written = self.arg(0), self.arg(1), self.arg(2), self.arg(3)
        f = self.files[h]
        data = bytes(self.mu.mem_read(buf, n))
        with open(f[0], 'r+b') as fp:
            fp.seek(os.path.getsize(f[0]) if f[2] else f[1])
            fp.write(data)
        f[1] += n
        self.touch(f[0])
        if written:
            self.wd(written, n)
        return 1

    def k_CloseHandle(self):
        self.files.pop(self.arg(0), None)
        return 1

    def k_GetFileSizeEx(self):
        self.wq(self.arg(1), os.path.getsize(self.files[self.arg(0)][0]))
        return 1

    def k_FlushFileBuffers(self): return 1
    def k_GetLastError(self): return self.last_error
    def k_SetLastError(self): self.last_error = self.arg(0)

    def k_GetFileAttributesW(self):
        p = self.host(self.wstr(self.arg(0)))
        return 0x80 if os.path.exists(p) else 0xffffffff

    def k_GetFileAttributesExW(self):
        p = self.host(self.wstr(self.arg(0)))
        if not os.path.exists(p):
            return 0
        out = self.arg(2)
        t = self.mtime.get(os.path.abspath(p), 1)
        self.mu.mem_write(out, bytes(36))
        self.wd(out, 0x80)
        self.wd(out + 20, t & 0xffffffff)  # ftLastWriteTime
        self.wd(out + 24, t >> 32)
        self.wd(out + 32, os.path.getsize(p))
        return 1

    def k_MoveFileExW(self):
        a, b = self.host(self.wstr(self.arg(0))), self.host(self.wstr(self.arg(1)))
        if not os.path.exists(a):
            return 0  # like Windows: ERROR_FILE_NOT_FOUND
        os.replace(a, b)
        self.touch(b)
        return 1

    def k_CreateDirectoryW(self):
        p = self.host(self.wstr(self.arg(0)))
        if os.path.exists(p):
            return 0
        os.makedirs(p)
        return 1

    def k_CopyFileW(self):
        a, b = self.host(self.wstr(self.arg(0))), self.host(self.wstr(self.arg(1)))
        if not os.path.exists(a) or (self.arg(2) and os.path.exists(b)):
            return 0
        import shutil
        shutil.copyfile(a, b)
        self.touch(b)
        return 1

    def k_DeleteFileW(self):
        p = self.host(self.wstr(self.arg(0)))
        if os.path.exists(p):
            os.remove(p)
        return 1

    def k_GetPrivateProfileStringW(self):
        section, key, default, buf, size, path = (self.wstr(self.arg(0)), self.wstr(self.arg(1)),
                                                  self.wstr(self.arg(2)), self.arg(3), self.arg(4),
                                                  self.host(self.wstr(self.arg(5))))
        value = default
        if os.path.exists(path):
            cur, first_done, in_first = None, False, False
            for line in open(path, encoding='latin1').read().replace('\r', '').split('\n'):
                t = line.strip()
                if t.startswith('[') and ']' in t:
                    name = t[1:t.index(']')].strip()
                    in_first = name.lower() == section.lower() and not first_done
                    if in_first:
                        first_done = True
                    continue
                if in_first and '=' in t and not t.startswith(';'):
                    k, v = t.split('=', 1)
                    if k.strip().lower() == key.lower():
                        value = v.strip()
                        if len(value) >= 2 and value[0] == value[-1] and value[0] in '"\'':
                            value = value[1:-1]
                        break
        value = value[: size - 1]
        self.mu.mem_write(buf, value.encode('utf-16le') + b'\0\0')
        return len(value)

    # ------------------------------------------------------------- misc kernel32
    def k_MultiByteToWideChar(self):
        src, n, dst, cap = self.arg(2), self.arg(3) & 0xffffffff, self.arg(4), self.arg(5) & 0xffffffff
        if n == 0xffffffff:
            raw = b''
            while self.mu.mem_read(src + len(raw), 1) != b'\0':
                raw += bytes(self.mu.mem_read(src + len(raw), 1))
            raw += b'\0'
        else:
            raw = bytes(self.mu.mem_read(src, n))
        w = raw.decode('utf-8', 'replace').encode('utf-16le')
        if dst and cap:
            w = w[: cap * 2]
            self.mu.mem_write(dst, w)
        return len(w) // 2

    def k_GetModuleHandleW(self):
        # other mods' modules (e.g. FnFNitro.dll): name -> base, not loaded = 0
        name = self.wstr(self.arg(0)).lower() if self.arg(0) else ''
        return getattr(self, 'modules', {}).get(name, 0)

    def k_GetTickCount64(self): return self.tick
    def k_Sleep(self): self.tick += self.arg(0)

    def k_CreateThread(self):
        self.threads.append(self.arg(2))
        return 0x9999

    # the Azimuth Mapper's named event (set e.mapper_running = True)
    def k_OpenEventW(self):
        return 0x778 if getattr(self, 'mapper_running', False) else 0

    def k_AcquireSRWLockExclusive(self): pass
    def k_ReleaseSRWLockExclusive(self): pass
    def k_AcquireSRWLockShared(self): pass
    def k_ReleaseSRWLockShared(self): pass

    def k_VirtualQuery(self):
        addr, info = self.arg(0), self.arg(1)
        self.mu.mem_write(info, bytes(48))
        base = addr & ~0xfff
        self.wq(info, base)
        self.wq(info + 8, base)
        self.wq(info + 24, 0x1000)       # RegionSize
        self.wd(info + 32, 0x1000)       # MEM_COMMIT
        self.wd(info + 36, 0x04)         # PAGE_READWRITE
        return 48

    def k_GetProcessHeap(self): return 0x5555
    def k_HeapAlloc(self):
        p = self.alloc(self.arg(2))
        if self.arg(1) & 8:
            self.mu.mem_write(p, bytes(self.arg(2)))
        return p
    def k_HeapFree(self): return 1

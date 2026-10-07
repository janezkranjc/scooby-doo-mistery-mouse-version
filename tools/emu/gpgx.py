"""Headless reference-emulator driver (Genesis Plus GX libretro core + tracer).

Used only as ground truth during reverse engineering: scripted input, PNG
screenshots, memory dumps, execution/read coverage, breakpoints, DMA log.
"""
import ctypes as C
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CORE = os.path.join(ROOT, "work", "gpgx", "genesis_plus_gx_libretro.dylib")
TRACER = os.path.join(ROOT, "work", "libtracer.dylib")
DEFAULT_ROM = os.path.join(ROOT, "rom", "Scooby-Doo Mystery (USA).md")

# libretro joypad ids -> Genesis buttons (per the core's 3-button mapping)
RETRO = {"B": 0, "A": 1, "C": 8, "START": 3, "UP": 4, "DOWN": 5, "LEFT": 6, "RIGHT": 7}
# Genesis A = retro Y(1), B = retro B(0), C = retro A(8)

ENV_FN = C.CFUNCTYPE(C.c_bool, C.c_uint, C.c_void_p)
VIDEO_FN = C.CFUNCTYPE(None, C.c_void_p, C.c_uint, C.c_uint, C.c_size_t)
AUDIO_FN = C.CFUNCTYPE(None, C.c_int16, C.c_int16)
AUDIOB_FN = C.CFUNCTYPE(C.c_size_t, C.c_void_p, C.c_size_t)
POLL_FN = C.CFUNCTYPE(None)
STATE_FN = C.CFUNCTYPE(C.c_int16, C.c_uint, C.c_uint, C.c_uint, C.c_uint)
HOOK_FN = C.CFUNCTYPE(None, C.c_int, C.c_int, C.c_uint, C.c_uint)


class GameInfo(C.Structure):
    _fields_ = [("path", C.c_char_p), ("data", C.c_void_p), ("size", C.c_size_t), ("meta", C.c_char_p)]


class BpRec(C.Structure):
    _fields_ = [("frame", C.c_uint32), ("pc", C.c_uint32), ("d", C.c_uint32 * 8), ("a", C.c_uint32 * 8), ("sr", C.c_uint32)]


class MemRec(C.Structure):
    _fields_ = [("frame", C.c_uint32), ("pc", C.c_uint32), ("addr", C.c_uint32), ("val", C.c_uint32), ("width_w", C.c_uint32)]


class DmaRec(C.Structure):
    _fields_ = [("frame", C.c_uint32), ("pc", C.c_uint32), ("src", C.c_uint32), ("len", C.c_uint32), ("dest", C.c_uint32), ("code", C.c_uint32)]


def _swap16(b):
    out = bytearray(len(b))
    out[0::2] = b[1::2]
    out[1::2] = b[0::2]
    return bytes(out)


def write_png(path, w, h, rgb):
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Emu:
    def __init__(self, rom=DEFAULT_ROM, trace=True):
        self.core = C.CDLL(CORE)
        self.tr = C.CDLL(TRACER)
        self.pad = 0
        self.frame = 0
        self.fb = None
        self._sysdir = C.create_string_buffer(os.path.join(ROOT, "work").encode())
        self._keep = []

        def env(cmd, data):
            cmd &= 0xFFFF
            if cmd in (9, 31):  # system / save directory
                C.cast(data, C.POINTER(C.c_char_p))[0] = C.cast(self._sysdir, C.c_char_p)
                return True
            if cmd == 10:  # pixel format
                self.pixfmt = C.cast(data, C.POINTER(C.c_int))[0]
                return True
            if cmd == 17:  # variable update
                C.cast(data, C.POINTER(C.c_bool))[0] = False
                return True
            if cmd == 51:  # input bitmasks
                return True
            return False

        def video(data, w, h, pitch):
            if data:
                self.fb = (C.string_at(data, pitch * h), w, h, pitch)

        def audio_batch(data, frames):
            return frames

        def state(port, device, index, ident):
            if port != 0:
                return 0
            if ident == 256:
                return self.pad
            return 1 if (self.pad >> ident) & 1 else 0

        self._cbs = [ENV_FN(env), VIDEO_FN(video), AUDIO_FN(lambda l, r: None), AUDIOB_FN(audio_batch),
                     POLL_FN(lambda: None), STATE_FN(state)]
        c = self.core
        c.retro_set_environment(self._cbs[0])
        c.retro_set_video_refresh(self._cbs[1])
        c.retro_set_audio_sample(self._cbs[2])
        c.retro_set_audio_sample_batch(self._cbs[3])
        c.retro_set_input_poll(self._cbs[4])
        c.retro_set_input_state(self._cbs[5])
        c.retro_init()
        gi = GameInfo(rom.encode(), None, 0, None)
        c.retro_load_game.argtypes = [C.POINTER(GameInfo)]
        c.retro_load_game.restype = C.c_bool
        if not c.retro_load_game(C.byref(gi)):
            raise RuntimeError("retro_load_game failed")
        c.retro_serialize_size.restype = C.c_size_t
        c.retro_serialize.argtypes = [C.c_void_p, C.c_size_t]
        c.retro_serialize.restype = C.c_bool
        c.retro_unserialize.argtypes = [C.c_void_p, C.c_size_t]
        c.retro_unserialize.restype = C.c_bool
        c.m68k_get_reg.argtypes = [C.c_int]
        c.m68k_get_reg.restype = C.c_uint
        t = self.tr
        t.tr_init(C.cast(c.m68k_get_reg, C.c_void_p))
        for n, rt in (("tr_exec_cov", C.POINTER(C.c_uint8)), ("tr_read_cov", C.POINTER(C.c_uint8)),
                      ("tr_read_pc", C.POINTER(C.c_uint32)), ("tr_vram_wpc", C.POINTER(C.c_uint32)),
                      ("tr_bp_log", C.POINTER(BpRec)), ("tr_mem_log", C.POINTER(MemRec)),
                      ("tr_dma_log", C.POINTER(DmaRec))):
            getattr(t, n).restype = rt
        for n in ("tr_bp_count", "tr_mem_count", "tr_dma_count"):
            getattr(t, n).restype = C.c_uint32
        if trace:
            c.set_cpu_hook(C.cast(t.tr_hook, C.c_void_p))

    # ---- running -------------------------------------------------------
    def run(self, frames=1, buttons=()):
        self.pad = 0
        for b in buttons:
            self.pad |= 1 << RETRO[b]
        for _ in range(frames):
            self.tr.tr_set_frame(self.frame)
            self.core.retro_run()
            self.frame += 1

    def press(self, *buttons, hold=4, after=10):
        self.run(hold, buttons)
        self.run(after)

    # ---- state ---------------------------------------------------------
    def save(self, path=None):
        n = self.core.retro_serialize_size()
        buf = C.create_string_buffer(n)
        assert self.core.retro_serialize(buf, n)
        if path:
            open(path, "wb").write(struct.pack("<I", self.frame) + buf.raw)
        return buf.raw

    def load(self, src):
        raw = open(src, "rb").read() if isinstance(src, str) else src
        if isinstance(src, str):
            self.frame = struct.unpack("<I", raw[:4])[0]
            raw = raw[4:]
        buf = C.create_string_buffer(raw, len(raw))
        assert self.core.retro_unserialize(buf, len(raw))

    # ---- memory views (big-endian, as the 68000 sees them) -------------
    def _glob(self, name, n):
        return C.string_at(C.addressof(C.c_uint8.in_dll(self.core, name)), n)

    def ram(self):
        return _swap16(self._glob("work_ram", 0x10000))

    def vram(self):
        return _swap16(self._glob("vram", 0x10000))

    def cram(self):
        """64 colours as (r,g,b) 0..7"""
        raw = self._glob("cram", 0x80)
        out = []
        for i in range(64):
            v = raw[2 * i] | (raw[2 * i + 1] << 8)
            out.append((v & 7, (v >> 3) & 7, (v >> 6) & 7))
        return out

    def vsram(self):
        raw = self._glob("vsram", 0x80)
        return [raw[2 * i] | (raw[2 * i + 1] << 8) for i in range(40)]

    def vdpreg(self):
        return self._glob("reg", 0x20)

    def reg(self, i):
        return self.core.m68k_get_reg(i)

    def rw(self, addr):
        r = self.ram(); a = addr & 0xFFFF
        return (r[a] << 8) | r[a + 1]

    def rl(self, addr):
        return (self.rw(addr) << 16) | self.rw(addr + 2)

    # ---- output --------------------------------------------------------
    def shot(self, path):
        data, w, h, pitch = self.fb
        rgb = bytearray(w * h * 3)
        for y in range(h):
            row = struct.unpack_from("<%dH" % w, data, y * pitch)
            o = y * w * 3
            for x, p in enumerate(row):
                rgb[o + 3 * x] = (p >> 11) * 255 // 31
                rgb[o + 3 * x + 1] = ((p >> 5) & 63) * 255 // 63
                rgb[o + 3 * x + 2] = (p & 31) * 255 // 31
        write_png(path, w, h, bytes(rgb))
        return w, h

    # ---- tracer --------------------------------------------------------
    def exec_cov(self):
        return C.string_at(self.tr.tr_exec_cov(), 0x200000)

    def read_cov(self):
        return C.string_at(self.tr.tr_read_cov(), 0x200000)

    def read_pc(self):
        return struct.unpack("<%dI" % 0x200000, C.string_at(self.tr.tr_read_pc(), 0x800000))

    def vram_wpc(self):
        return struct.unpack("<%dI" % 0x10000, C.string_at(self.tr.tr_vram_wpc(), 0x40000))

    def bp(self, *pcs):
        for p in pcs:
            self.tr.tr_bp_add(p)

    def bp_hits(self, reset=True):
        n = self.tr.tr_bp_count(); log = self.tr.tr_bp_log()
        out = [dict(frame=log[i].frame, pc=log[i].pc, d=list(log[i].d), a=list(log[i].a), sr=log[i].sr) for i in range(n)]
        if reset:
            self.tr.tr_bp_reset_log()
        return out

    def watch(self, lo, hi, r=False, w=True):
        self.tr.tr_watch_add(lo, hi, int(r), int(w))

    def mem_hits(self, reset=True):
        n = self.tr.tr_mem_count(); log = self.tr.tr_mem_log()
        out = [(log[i].frame, log[i].pc, log[i].addr, log[i].val, log[i].width_w & 0xFF, bool(log[i].width_w & 0x100)) for i in range(n)]
        if reset:
            self.tr.tr_mem_reset_log()
        return out

    def dma_hits(self, reset=True):
        n = self.tr.tr_dma_count(); log = self.tr.tr_dma_log()
        out = [dict(frame=log[i].frame, pc=log[i].pc, src=log[i].src, len=log[i].len, dest=log[i].dest, code=log[i].code) for i in range(n)]
        if reset:
            self.tr.tr_dma_reset_log()
        return out

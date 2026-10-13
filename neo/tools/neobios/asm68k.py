#!/usr/bin/env python3
"""asm68k: un ensamblador pequeño del 68000 (sintaxis de Motorola) para la BIOS propia de NEORecomp. Solo
biblioteca estándar de Python; hecho aquí.

    python3 tools/neobios/asm68k.py fuente.s salida.bin [--base $C00000] [--size $20000] [--fill $FF]
                                   [--list listado.txt] [--swap]

- Dos pasadas; etiquetas globales y locales (las que empiezan con «.» se cuelgan de la última global).
- Expresiones: $hex, %binario, decimal, 'c', etiquetas, * (la dirección actual), + - * / % << >> & | ^ ~ y
  paréntesis.
- Directivas: org, dc.b/dc.w/dc.l (con cadenas entre comillas), ds.b/ds.w/ds.l, equ (o =), even, align N.
- Instrucciones: las del 68000 que usa la BIOS (move, movea, moveq, movem, lea, pea, clr, tst, neg, not, ext,
  swap, add/sub/and/or/eor/cmp y sus formas a/i/q, mulu/muls/divu/divs, desplazamientos y rotaciones, btst,
  bset, bclr, bchg, bcc/bra/bsr, dbcc, scc, jmp, jsr, rts, rte, rtr, nop, stop, trap, exg, link, unlk, tas,
  chk, illegal, reset, trapv, y move a/desde sr, ccr y usp). Los saltos Bcc sin tamaño son de 16 bits; con
  .s, de 8 (sin elegir solo para que las dos pasadas den lo mismo).
- La salida es la imagen de --size bytes desde --base (lo que no se escribió queda en --fill); con --swap, en
  el orden de bytes de los chips del Neo Geo (cada palabra al revés, como neo-epo.bin o un P1).
"""
import argparse, re, sys

REGS = {f"d{i}": ("d", i) for i in range(8)}
REGS.update({f"a{i}": ("a", i) for i in range(8)})
REGS["sp"] = ("a", 7)
CC = {"t": 0, "f": 1, "hi": 2, "ls": 3, "cc": 4, "hs": 4, "cs": 5, "lo": 5, "ne": 6, "eq": 7, "vc": 8, "vs": 9,
      "pl": 10, "mi": 11, "ge": 12, "lt": 13, "gt": 14, "le": 15}
SIZE = {"b": 0, "w": 1, "l": 2}


class Error(Exception):
    pass


# --------------------------------------------------------------------------- expresiones

TOKEN = re.compile(r"\s*(?:(\$[0-9a-fA-F]+)|(%[01]+)|(\d+)|('(?:[^'\\]|\\.)')|([A-Za-z_.][\w.]*)|(<<|>>|[-+*/%&|^~()]))")


class Expr:
    def __init__(self, asm, text):
        self.asm, self.toks, self.i = asm, [], 0
        pos = 0
        text = text.strip()
        while pos < len(text):
            m = TOKEN.match(text, pos)
            if not m or m.end() == pos:
                raise Error(f"expresión inválida: {text}")
            self.toks.append(m)
            pos = m.end()
        if not self.toks:
            raise Error("falta una expresión")

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def op(self, *ops):
        t = self.peek()
        if t and t.group(6) in ops:
            self.i += 1
            return t.group(6)
        return None

    def value(self):
        v = self.binary(0)
        if self.i != len(self.toks):
            raise Error("sobra algo en la expresión")
        return v

    PREC = [("|",), ("^",), ("&",), ("<<", ">>"), ("+", "-"), ("*", "/", "%")]

    def binary(self, lvl):
        if lvl == len(self.PREC):
            return self.unary()
        v = self.binary(lvl + 1)
        while True:
            o = self.op(*self.PREC[lvl])
            if not o:
                return v
            w = self.binary(lvl + 1)
            v = {"|": lambda: v | w, "^": lambda: v ^ w, "&": lambda: v & w, "<<": lambda: v << w, ">>": lambda: v >> w,
                 "+": lambda: v + w, "-": lambda: v - w, "*": lambda: v * w,
                 "/": lambda: int(v / w) if w else 0, "%": lambda: v % w if w else 0}[o]()

    def unary(self):
        if self.op("-"):
            return -self.unary()
        if self.op("+"):
            return self.unary()
        if self.op("~"):
            return ~self.unary()
        if self.op("("):
            v = self.binary(0)
            if not self.op(")"):
                raise Error("falta «)»")
            return v
        t = self.peek()
        if not t:
            raise Error("expresión incompleta")
        self.i += 1
        if t.group(1):
            return int(t.group(1)[1:], 16)
        if t.group(2):
            return int(t.group(2)[1:], 2)
        if t.group(3):
            return int(t.group(3))
        if t.group(4):
            return ord(t.group(4)[1:-1].encode().decode("unicode_escape"))
        if t.group(5):
            return self.asm.symbol(t.group(5))
        if t.group(6) == "*":
            return self.asm.pc
        raise Error(f"no se esperaba «{t.group(0).strip()}»")


# --------------------------------------------------------------------------- operandos

class Op:
    """Un operando: kind = dn an ind postinc predec disp index absw absl pcdisp pcindex imm reglist sr ccr usp"""
    def __init__(self, kind, reg=0, val=0, xreg=None, xlong=False):
        self.kind, self.reg, self.val, self.xreg, self.xlong = kind, reg, val, xreg, xlong


def split_operands(s):
    out, depth, cur, q = [], 0, "", False
    for ch in s:
        if ch == "'" and not q:
            q = True
        elif ch == "'" and q:
            q = False
        if not q and ch == "(":
            depth += 1
        elif not q and ch == ")":
            depth -= 1
        if ch == "," and depth == 0 and not q:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def reglist(s):
    """d0-d3/a0/a6 -> máscara (bit 0 = d0 ... bit 15 = a7), o None"""
    mask = 0
    for part in s.lower().split("/"):
        m = re.fullmatch(r"([da][0-7]|sp)(?:-([da][0-7]|sp))?", part.strip())
        if not m:
            return None
        a = REGS[m.group(1)]
        b = REGS[m.group(2)] if m.group(2) else a
        ia, ib = (8 if a[0] == "a" else 0) + a[1], (8 if b[0] == "a" else 0) + b[1]
        if ib < ia:
            return None
        for i in range(ia, ib + 1):
            mask |= 1 << i
    return mask


class Asm:
    def __init__(self, base, size, fill):
        self.base, self.size, self.fill = base, size, fill
        self.syms, self.pass_ = {}, 1
        self.last_global = ""
        self.listing = []

    def symbol(self, name):
        if name.startswith("."):
            name = self.last_global + name
        if name in self.syms:
            return self.syms[name]
        if self.pass_ == 1:
            return 0
        raise Error(f"etiqueta sin definir: {name}")

    def expr(self, s):
        return Expr(self, s).value()

    def operand(self, s):
        t = s.strip()
        tl = t.lower()
        if tl in REGS:
            k, r = REGS[tl]
            return Op("dn" if k == "d" else "an", r)
        if tl in ("sr", "ccr", "usp"):
            return Op(tl)
        if t.startswith("#"):
            return Op("imm", val=self.expr(t[1:]))
        m = re.fullmatch(r"\(\s*([ad][0-7]|sp)\s*\)\+", tl)
        if m:
            return Op("postinc", REGS[m.group(1)][1]) if REGS[m.group(1)][0] == "a" else None
        m = re.fullmatch(r"-\(\s*([ad][0-7]|sp)\s*\)", tl)
        if m:
            return Op("predec", REGS[m.group(1)][1])
        m = re.fullmatch(r"\(\s*(a[0-7]|sp)\s*\)", tl)
        if m:
            return Op("ind", REGS[m.group(1)][1])
        # d(An), d(An,Xn[.w|.l]), d(pc), d(pc,Xn), (d,An...)
        m = re.fullmatch(r"(.*?)\(\s*(?:([^,()]+)\s*,\s*)?(a[0-7]|sp|pc)\s*(?:,\s*([ad][0-7]|sp)(\.[wl])?)?\s*\)", t, re.I)
        if m and (m.group(1).strip() or m.group(2) or m.group(4)):
            dtxt = (m.group(1) or "").strip() or (m.group(2) or "").strip() or "0"
            base = m.group(3).lower()
            d = self.expr(dtxt)
            x = None
            if m.group(4):
                xr = REGS[m.group(4).lower()]
                x = ((1 if xr[0] == "a" else 0) << 3) | xr[1]
            xl = (m.group(5) or ".w").lower() == ".l"
            if base == "pc":
                if x is None:
                    return Op("pcdisp", val=d)
                return Op("pcindex", val=d, xreg=x, xlong=xl)
            r = REGS[base][1]
            return Op("disp", r, d) if x is None else Op("index", r, d, x, xl)
        m = re.fullmatch(r"\(?(.+?)\)?\.([wl])", t, re.I)       # ($1234).w; una etiqueta local como .w no
        if m and not t.startswith("#"):
            v = self.expr(m.group(1))
            return Op("absw" if m.group(2).lower() == "w" else "absl", val=v)
        rl = reglist(t)
        if rl is not None and ("-" in t or "/" in t):
            return Op("reglist", val=rl)
        return Op("absl", val=self.expr(t.strip("()") if t.startswith("(") and t.endswith(")") else t))

    # ----------------------------------------------------------------- codificación de operandos

    def ea(self, o, size, pc_after_op):
        """(mode<<3|reg, [palabras de extensión]); pc_after_op = dirección de la primera extensión"""
        k = o.kind
        if k == "dn":
            return (0 << 3) | o.reg, []
        if k == "an":
            return (1 << 3) | o.reg, []
        if k == "ind":
            return (2 << 3) | o.reg, []
        if k == "postinc":
            return (3 << 3) | o.reg, []
        if k == "predec":
            return (4 << 3) | o.reg, []
        if k == "disp":
            self.check(o.val, -0x8000, 0x7FFF, "desplazamiento")
            return (5 << 3) | o.reg, [o.val & 0xFFFF]
        if k == "index":
            self.check(o.val, -0x80, 0x7F, "desplazamiento indexado")
            return (6 << 3) | o.reg, [(o.xreg << 12) | (0x800 if o.xlong else 0) | (o.val & 0xFF)]
        if k == "absw":
            self.check(o.val if o.val < 0x8000 else o.val - (0x1000000 if o.val > 0xFFFF else 0x10000), -0x8000, 0x7FFF,
                       "absoluto corto")
            return (7 << 3) | 0, [o.val & 0xFFFF]
        if k == "absl":
            return (7 << 3) | 1, [(o.val >> 16) & 0xFFFF, o.val & 0xFFFF]
        if k == "pcdisp":
            d = o.val - pc_after_op
            self.check(d, -0x8000, 0x7FFF, "desplazamiento relativo al PC")
            return (7 << 3) | 2, [d & 0xFFFF]
        if k == "pcindex":
            d = o.val - pc_after_op
            self.check(d, -0x80, 0x7F, "desplazamiento relativo al PC indexado")
            return (7 << 3) | 3, [(o.xreg << 12) | (0x800 if o.xlong else 0) | (d & 0xFF)]
        if k == "imm":
            v = o.val
            if size == 2:
                return (7 << 3) | 4, [(v >> 16) & 0xFFFF, v & 0xFFFF]
            if size == 0:
                return (7 << 3) | 4, [v & 0xFF]
            return (7 << 3) | 4, [v & 0xFFFF]
        raise Error("operando no válido aquí")

    def check(self, v, lo, hi, what):
        if self.pass_ == 2 and not lo <= v <= hi:
            raise Error(f"{what} fuera de alcance: {v}")

    # ----------------------------------------------------------------- instrucciones

    def instr(self, mnem, args, pc):
        """Las palabras de una instrucción en pc"""
        m = mnem.lower()
        sz = None
        if "." in m:
            m, s = m.split(".", 1)
            if s not in ("b", "w", "l", "s"):
                raise Error(f"tamaño desconocido: .{s}")
            sz = s
        ops = [self.operand(a) for a in split_operands(args)] if args.strip() else []
        size = SIZE.get(sz, 1) if sz != "s" else 0

        def need(n):
            if len(ops) != n:
                raise Error(f"{m} lleva {n} operando(s)")

        def emit(op, *eas):
            words, at = [op], pc + 2
            for o, s in eas:
                e, ext = self.ea(o, s, at)
                words += ext
                at += 2 * len(ext)
            return words

        def ea_of(o, s, at):
            return self.ea(o, s, at)

        simple = {"rts": 0x4E75, "rte": 0x4E73, "rtr": 0x4E77, "nop": 0x4E71, "reset": 0x4E70, "trapv": 0x4E76,
                  "illegal": 0x4AFC}
        if m in simple:
            need(0)
            return [simple[m]]
        if m == "stop":
            need(1)
            return [0x4E72, ops[0].val & 0xFFFF]
        if m == "trap":
            need(1)
            return [0x4E40 | (ops[0].val & 15)]
        if m in ("bra", "bsr") or (m.startswith("b") and m[1:] in CC and m[1:] not in ("t", "f")):
            need(1)
            cc = 0 if m == "bra" else 1 if m == "bsr" else CC[m[1:]]
            d = ops[0].val - (pc + 2)
            if sz == "s":
                self.check(d, -128, 127, "salto corto")
                if self.pass_ == 2 and d == 0:
                    raise Error("bcc.s a la instrucción siguiente no existe")
                return [0x6000 | cc << 8 | (d & 0xFF)]
            self.check(d, -0x8000, 0x7FFF, "salto")
            return [0x6000 | cc << 8, d & 0xFFFF]
        if m.startswith("db") and (m[2:] in CC or m == "dbra"):
            need(2)
            cc = 1 if m == "dbra" else CC[m[2:]]
            d = ops[1].val - (pc + 2)
            self.check(d, -0x8000, 0x7FFF, "dbcc")
            return [0x50C8 | cc << 8 | ops[0].reg, d & 0xFFFF]
        if m.startswith("s") and m[1:] in CC:
            need(1)
            return emit(0x50C0 | CC[m[1:]] << 8 | ea_of(ops[0], 0, pc + 2)[0], (ops[0], 0))
        if m in ("jmp", "jsr", "pea", "tas"):
            need(1)
            base = {"jmp": 0x4EC0, "jsr": 0x4E80, "pea": 0x4840, "tas": 0x4AC0}[m]
            return emit(base | ea_of(ops[0], 2, pc + 2)[0], (ops[0], 2))
        if m == "lea":
            need(2)
            return emit(0x41C0 | ops[1].reg << 9 | ea_of(ops[0], 2, pc + 2)[0], (ops[0], 2))
        if m in ("clr", "neg", "negx", "not", "tst"):
            need(1)
            base = {"clr": 0x4200, "neg": 0x4400, "negx": 0x4000, "not": 0x4600, "tst": 0x4A00}[m]
            return emit(base | size << 6 | ea_of(ops[0], size, pc + 2)[0], (ops[0], size))
        if m == "ext":
            need(1)
            return [(0x48C0 if size == 2 else 0x4880) | ops[0].reg]
        if m == "swap":
            need(1)
            return [0x4840 | ops[0].reg]
        if m == "link":
            need(2)
            return [0x4E50 | ops[0].reg, ops[1].val & 0xFFFF]
        if m == "unlk":
            need(1)
            return [0x4E58 | ops[0].reg]
        if m == "exg":
            need(2)
            a, b = ops
            if a.kind == "dn" and b.kind == "dn":
                return [0xC140 | a.reg << 9 | b.reg]
            if a.kind == "an" and b.kind == "an":
                return [0xC148 | a.reg << 9 | b.reg]
            if a.kind == "an":
                a, b = b, a
            return [0xC188 | a.reg << 9 | b.reg]
        if m == "moveq":
            need(2)
            self.check(ops[0].val, -128, 255, "moveq")
            return [0x7000 | ops[1].reg << 9 | (ops[0].val & 0xFF)]
        if m in ("mulu", "muls", "divu", "divs", "chk"):
            need(2)
            base = {"mulu": 0xC0C0, "muls": 0xC1C0, "divu": 0x80C0, "divs": 0x81C0, "chk": 0x4180}[m]
            return emit(base | ops[1].reg << 9 | ea_of(ops[0], 1, pc + 2)[0], (ops[0], 1))
        if m == "movem":
            need(2)
            if ops[0].kind in ("reglist", "dn", "an"):            # registros -> memoria
                mask = ops[0].val if ops[0].kind == "reglist" else 1 << ((8 if ops[0].kind == "an" else 0) + ops[0].reg)
                dst = ops[1]
                if dst.kind == "predec":
                    mask = int(f"{mask:016b}"[::-1], 2)
                e, ext = self.ea(dst, size, pc + 4)
                return [0x4880 | (1 if size == 2 else 0) << 6 | e, mask] + ext
            mask = ops[1].val if ops[1].kind == "reglist" else 1 << ((8 if ops[1].kind == "an" else 0) + ops[1].reg)
            e, ext = self.ea(ops[0], size, pc + 4)
            return [0x4C80 | (1 if size == 2 else 0) << 6 | e, mask] + ext
        if m in ("move", "movea"):
            need(2)
            s, d = ops
            if d.kind == "sr":
                return emit(0x46C0 | ea_of(s, 1, pc + 2)[0], (s, 1))
            if d.kind == "ccr":
                return emit(0x44C0 | ea_of(s, 1, pc + 2)[0], (s, 1))
            if s.kind == "sr":
                return emit(0x40C0 | ea_of(d, 1, pc + 2)[0], (d, 1))
            if d.kind == "usp":
                return [0x4E60 | s.reg]
            if s.kind == "usp":
                return [0x4E68 | d.reg]
            code = {0: 1, 1: 3, 2: 2}[size]
            se, sext = self.ea(s, size, pc + 2)
            de, dext = self.ea(d, size, pc + 2 + 2 * len(sext))
            dst = ((de & 7) << 3 | (de >> 3)) << 6
            return [code << 12 | dst | se] + sext + dext
        if m in ("add", "sub", "and", "or", "cmp", "eor", "adda", "suba", "cmpa"):
            need(2)
            s, d = ops
            base = {"add": 0xD000, "sub": 0x9000, "and": 0xC000, "or": 0x8000, "cmp": 0xB000, "eor": 0xB000,
                    "adda": 0xD000, "suba": 0x9000, "cmpa": 0xB000}[m]
            if d.kind == "an":                                    # adda/suba/cmpa
                return emit(base | d.reg << 9 | (7 if size == 2 else 3) << 6 | ea_of(s, size, pc + 2)[0], (s, size))
            if s.kind == "imm" and m in ("add", "sub", "and", "or", "cmp", "eor"):
                ib = {"add": 0x0600, "sub": 0x0400, "and": 0x0200, "or": 0x0000, "cmp": 0x0C00, "eor": 0x0A00}[m]
                return self.instr(f"{['ori', 'andi', 'subi', 'addi', None, 'eori', 'cmpi'][ib >> 9]}.{'bwl'[size]}", args, pc)
            if m == "eor" or (d.kind != "dn" and m != "cmp"):     # Dn,<ea>
                return emit(base | s.reg << 9 | (4 + size) << 6 | ea_of(d, size, pc + 2)[0], (d, size))
            return emit(base | d.reg << 9 | size << 6 | ea_of(s, size, pc + 2)[0], (s, size))
        if m in ("addi", "subi", "andi", "ori", "eori", "cmpi"):
            need(2)
            s, d = ops
            base = {"addi": 0x0600, "subi": 0x0400, "andi": 0x0200, "ori": 0x0000, "eori": 0x0A00, "cmpi": 0x0C00}[m]
            if d.kind in ("ccr", "sr"):
                return [base | (0x3C if d.kind == "ccr" else 0x7C), s.val & (0xFF if d.kind == "ccr" else 0xFFFF)]
            imm = self.ea(s, size, pc + 2)[1]
            de, dext = self.ea(d, size, pc + 2 + 2 * len(imm))
            return [base | size << 6 | de] + imm + dext
        if m in ("addq", "subq"):
            need(2)
            v = ops[0].val
            self.check(v, 1, 8, m)
            return emit((0x5000 if m == "addq" else 0x5100) | (v & 7) << 9 | size << 6 | ea_of(ops[1], size, pc + 2)[0],
                        (ops[1], size))
        if m in ("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr"):
            typ = {"as": 0, "ls": 1, "ro": 3, "rox": 2}[m[:-1]]
            left = 1 if m.endswith("l") else 0
            if len(ops) == 1:                                     # memoria, una vez, palabra
                return emit(0xE0C0 | typ << 9 | left << 8 | ea_of(ops[0], 1, pc + 2)[0], (ops[0], 1))
            need(2)
            c, d = ops
            if c.kind == "imm":
                self.check(c.val, 1, 8, "cuenta del desplazamiento")
                return [0xE000 | (c.val & 7) << 9 | left << 8 | size << 6 | typ << 3 | d.reg]
            return [0xE020 | c.reg << 9 | left << 8 | size << 6 | typ << 3 | d.reg]
        if m in ("btst", "bchg", "bclr", "bset"):
            need(2)
            typ = {"btst": 0, "bchg": 1, "bclr": 2, "bset": 3}[m]
            b, d = ops
            if b.kind == "dn":
                return emit(0x0100 | b.reg << 9 | typ << 6 | ea_of(d, 0, pc + 2)[0], (d, 0))
            de, dext = self.ea(d, 0, pc + 4)
            return [0x0800 | typ << 6 | de, b.val & 0xFF] + dext
        raise Error(f"instrucción desconocida: {mnem}")

    # ----------------------------------------------------------------- fuente

    def assemble(self, lines):
        for self.pass_ in (1, 2):
            self.pc = self.base
            self.out = bytearray([self.fill]) * self.size
            self.last_global = ""
            self.listing = []
            for num, (fname, raw) in enumerate(lines, 1):
                try:
                    self.line(raw)
                except Error as e:
                    raise Error(f"{fname}:{num}: {e}\n    {raw.strip()}")

    def put(self, words_or_bytes, as_bytes=False):
        if as_bytes:
            data = bytes(words_or_bytes)
        else:
            data = b"".join(bytes(((w >> 8) & 0xFF, w & 0xFF)) for w in words_or_bytes)
        off = self.pc - self.base
        if off < 0 or off + len(data) > self.size:
            raise Error(f"fuera de la imagen: ${self.pc:06X}")
        self.out[off:off + len(data)] = data
        self.pc += len(data)

    def line(self, raw):
        s = raw.split(";", 1)[0] if "'" not in raw.split(";", 1)[0] else self.strip_comment(raw)
        s = s.rstrip()
        if not s.strip():
            return
        m = re.match(r"^([A-Za-z_.][\w.]*):?(?=\s|$)", s) if not s[0].isspace() else None
        if m:
            label = m.group(1)
            rest = s[m.end():].strip()
            r2 = re.match(r"(?i)^(equ|=)\s+(.*)$", rest) if rest else None
            if not r2 and rest.startswith("="):
                r2 = re.match(r"^(=)\s*(.*)$", rest)
            if r2:
                self.define(label, self.expr(r2.group(2)))
                return
            if not label.startswith("."):
                self.last_global = label
            self.define(label, self.pc)
            s = rest
            if not s:
                return
        s = s.strip()
        mm = re.match(r"^(\S+)\s*(.*)$", s)
        mnem, args = mm.group(1), mm.group(2)
        ml = mnem.lower()
        start = self.pc
        if ml == "org":
            self.pc = self.expr(args)
            return
        if ml in ("even",):
            if self.pc & 1:
                self.put([self.fill], True)
            return
        if ml == "align":
            n = self.expr(args)
            while self.pc % n:
                self.put([self.fill], True)
            return
        if ml in ("dc.b", "dc.w", "dc.l", "dc"):
            sz = {"dc.b": 1, "dc.w": 2, "dc.l": 4, "dc": 2}[ml]
            data = bytearray()
            for a in split_operands(args):
                if len(a) >= 2 and a[0] == a[-1] == '"':
                    raw_s = a[1:-1].encode("latin-1").decode("unicode_escape").encode("latin-1")
                    if sz != 1:
                        raise Error("cadenas solo en dc.b")
                    data += raw_s
                    continue
                v = self.expr(a)
                data += (v & ((1 << (8 * sz)) - 1)).to_bytes(sz, "big")
            if sz > 1 and self.pc & 1:
                raise Error("dc.w/dc.l en dirección impar")
            self.put(data, True)
            self.listing.append((start, raw))
            return
        if ml in ("ds.b", "ds.w", "ds.l"):
            n = self.expr(args) * {"ds.b": 1, "ds.w": 2, "ds.l": 4}[ml]
            self.put([self.fill] * n, True)
            return
        if self.pc & 1:
            raise Error("instrucción en dirección impar")
        words = self.instr(mnem, args, self.pc)
        self.put(words)
        self.listing.append((start, raw))

    @staticmethod
    def strip_comment(raw):
        q, out = None, ""
        for ch in raw:
            if q:
                if ch == q:
                    q = None
            elif ch in "'\"":
                q = ch
            elif ch == ";":
                break
            out += ch
        return out

    def define(self, name, value):
        if name.startswith("."):
            name = self.last_global + name
        if self.pass_ == 1 and name in self.syms:
            raise Error(f"etiqueta repetida: {name}")
        self.syms[name] = value


def main():
    ap = argparse.ArgumentParser(description="ensamblador del 68000 para la BIOS propia")
    ap.add_argument("fuente")
    ap.add_argument("salida")
    ap.add_argument("--base", default="$C00000")
    ap.add_argument("--size", default="$20000")
    ap.add_argument("--fill", default="$FF")
    ap.add_argument("--list")
    ap.add_argument("--swap", action="store_true", help="cada palabra al revés (el orden de los chips)")
    a = ap.parse_args()
    num = lambda t: int(t[1:], 16) if t.startswith("$") else int(t, 0)
    asm = Asm(num(a.base), num(a.size), num(a.fill))
    with open(a.fuente, encoding="utf-8") as f:
        lines = [(a.fuente, l) for l in f.read().splitlines()]
    try:
        asm.assemble(lines)
    except Error as e:
        sys.exit(f"asm68k: {e}")
    out = bytearray(asm.out)
    if a.swap:
        out[0::2], out[1::2] = asm.out[1::2], asm.out[0::2]
    with open(a.salida, "wb") as f:
        f.write(out)
    if a.list:
        with open(a.list, "w", encoding="utf-8") as f:
            for addr, raw in asm.listing:
                f.write(f"{addr:06X}  {raw.strip()}\n")
    print(f"{a.salida}: {asm.pc - asm.base} bytes usados de {asm.size}")


if __name__ == "__main__":
    main()

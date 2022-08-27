import os
from num2words import num2words
import subprocess
import math
import re
import shutil

OUTPUT_DIR = 'data'


def write_lines_to_file(ls, fn):
    with open(fn, "w", encoding='utf-8') as f:
        for l in ls:
            f.write(l + "\n")


def float_to_fix(f, s=0x1000):
    a = round(f * s)
    return '0x{:05X}'.format(a) if a >= 0 else '-0x{:04X}'.format(-a)


def from_LPC(fn):
    A = []
    G = []
    dx = None
    samplingPeriod = None
    nx = None
    mc = None

    with open(fn, "r", encoding='utf-8') as f:
        a = []
        while True:
            line = f.readline()
            if line:
                line = line.strip()
                if line.startswith("dx = "):
                    dx = float(line.split(" ")[-1])
                if line.startswith("samplingPeriod = "):
                    samplingPeriod = float(line.split(" ")[-1])
                if line.startswith("nx = "):
                    nx = int(line.split(" ")[-1])
                if line.startswith("maxnCoefficients = "):
                    mc = int(line.split(" ")[-1])

                if not line.startswith("a []:"):
                    if line.startswith("a ["):
                        a.append(float(line.split(" ")[-1]))
                        if len(a) == mc:
                            A.append(a)
                            a = []
                if line.startswith("gain = "):
                    G.append(float(line.split(" ")[-1]))
            else:
                break

    assert(dx)
    assert(nx)
    assert(samplingPeriod)
    assert(len(G) == len(A) == nx)

    AA = []

    for g, a in zip(G, A):
        if g < 0.0000001:
            AA.append([0.0] * mc)
        else:
            AA.append(a)

    zeros_at_end = 0
    for g in reversed(G):
        if g >= 0.0000001:
            break
        zeros_at_end += 1

    if zeros_at_end > 1:
        to_remove = zeros_at_end - 1
        G = G[:-to_remove]
        AA = AA[:-to_remove]

    nc = len(AA)
    fl = round(dx / samplingPeriod)
    sr = round(1.0 / samplingPeriod)
    return nc, mc, fl, sr, G, AA


def from_Pitch(fn):
    intensity = 0
    P = []
    pitch = []
    strength = []
    dx = None
    nx = None
    candidates_n = 1
    fmt_re = re.compile(r"candidates \[(\d+)\]:")

    with open(fn, "r", encoding='utf-8') as f:
        while True:
            line = f.readline()
            if line:
                line = line.strip()
                if line.startswith("dx = "):
                    dx = float(line.split(" ")[-1])
                if line.startswith("nx = "):
                    nx = int(line.split(" ")[-1])

                res = fmt_re.match(line)
                if res:
                    # keep track of last formant index
                    candidates_n = int(res.group(1))

                if line.startswith("frequency = "):
                    pitch.append(float(line.split(" ")[-1]))

                if line.startswith("strength = "):
                    strength.append(float(line.split(" ")[-1]))

                if line.startswith("intensity = "):
                    intensity = float(line.split(" ")[-1])

                if line.startswith("candidates []:"):
                    if intensity > 0.1:
                        assert(candidates_n == len(pitch))
                        assert(candidates_n == len(strength))
                        mp, ms = sorted(zip(pitch, strength),
                                        key=lambda x: x[1], reverse=True)[0]
                        if ms > 0.5:
                            P.append(mp)
                        else:
                            P.append(0.0)
                    else:
                        P.append(0.0)
                    pitch = []
                    strength = []
            else:
                break

    if intensity > 0.1:
        assert(candidates_n == len(pitch))
        assert(candidates_n == len(strength))
        mp, ms = sorted(zip(pitch, strength),
                        key=lambda x: x[1], reverse=True)[0]
        if ms > 0.5:
            P.append(mp)
        else:
            P.append(0.0)
    else:
        P.append(0.0)

    assert(dx)
    assert(nx)

    return dx, nx, P


def rep_pl(s):
    d = {'ą': 'a', 'ę': 'e', 'ć': 'c', 'ś': 's',
         'ń': 'n', 'ó': 'o', 'ż': 'z', 'ź': 'z', 'ł': 'l'}
    ret = ""
    for l in s:
        if l in d.keys():
            ret += d[l]
        else:
            ret += l

    return ret


def gen_C(nc, mc, fl, sr, data):
    alpha = math.exp(-2.0 * math.pi * 50.0 / sr)
    ls = ["#ifndef __LPC_DATA__",
          "#define __LPC_DATA__",
          "",
          "#include <stddef.h>",
          "",
          "#include \"fix.h\"",
          "",
          "#define LPC_ORDER ({})".format(mc),
          "#define LPC_FRAME_LEN ({})".format(fl),
          "#define LPC_SAMPLE_RATE ({})".format(sr),
          "#define LPC_DEEMPHASIS_FACTOR ({})".format(
              float_to_fix(alpha, 0x00100000)),
          "",
          "typedef struct",
          "{",
          "    int16_t g;",
          "    uint8_t ps;",
          "    int16_t a[LPC_ORDER];",
          "} lpc_frame_t;",
          "",
          "typedef struct",
          "{",
          "    const size_t len;",
          "    lpc_frame_t frames[];",
          "} lpc_seq_t;",
          "",
          "typedef enum {"]

    for name, i, nc, frame in data:
        if i == 0:
            ls.append("    LPC_{} = 0,".format(name))
        else:
            ls.append("    LPC_{},".format(name))

    ls.append('    LPC_MAX_SEQ')
    ls.append('} lpc_seq_e;')
    ls.append('')
    ls.append('const lpc_seq_t * const lpc_get_seq(lpc_seq_e id);')
    ls.append('')
    ls.append("#endif")
    write_lines_to_file(ls, "lpc_data.h")

    ls = ['#include "lpc_data.h"', '']

    for name, i, nc, frame in data:
        est_size = ((2 * mc) + 2 + 1) * nc
        ls.append("// {} - {} bytes - {}".format(i, est_size, name))
        ls.append('const lpc_seq_t LPC_{}_SEQ = {{'.format(i))
        ls.append('    .len = {},'.format(nc))
        ls.append('    .frames = {')

        for a, g, p in frame:
            if p > 0:
                ps = round(sr / p)
                if ps > 255:
                    ps = 255
            else:
                ps = 0
            a = ', '.join(list(map(lambda x: float_to_fix(-x), a)))
            ls.append("        {{.g = {}, .ps = 0x{:02X}, .a = {{{}}}}},".format(
                float_to_fix(math.sqrt(g)), ps, a))
        ls.extend(["    }", "};", ""])

    ls.append(
        "static const lpc_seq_t * const lpc_sequences[{}] = {{".format(len(data)))
    for name, i, nc, frame in data:
        ls.append('    &LPC_{}_SEQ,'.format(i))
    ls.append("};")

    ls.append('')
    ls.append('const lpc_seq_t * const lpc_get_seq(lpc_seq_e id) {')
    ls.append('    if(id < LPC_MAX_SEQ) {')
    ls.append('        return lpc_sequences[id];')
    ls.append('    } else {')
    ls.append('        return NULL;')
    ls.append('    }')
    ls.append('};')
    write_lines_to_file(ls, "lpc_data.c")


# data = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
#         30, 40, 50, 60, 70, 80, "dziewięćdziesiąt", 100, 200, 300, 400, 500, 600, 700, 800, 900,
#         1000, 1000000, "minus", "plus", "przecinek", "tysięcy", "tysiące",
#         "stopnie", "stopni", "Celsjusza", "temperatura", "wilgotność", "ciśnienie",
#         "hektopaskale", "hektopaskali"]

data = ['zerowa', 'pierwsza', 'druga', 'trzecia', 'czwarta', 'piąta', 'szósta', 'siódma', 'ósma',
        'dziewiąta', 'dziesiąta', 'jedenasta', 'dwunasta', 'trzynasta', 'czternasta', 'piętnasta',
        'szesnasta', 'siedemnasta', 'osiemnasta', 'dziewiętnasta', 'dwudziesta', 'zero', 'jeden',
        'dwie', 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 30, 40, 50, 'jest godzina']

data.extend(['jest godzina dwudziesta pierwsza trzydzieści sześć',
             'W Szczebrzeszynie chrząszcz brzmi w trzcinie i Szczebrzeszyn z tego słynie'])

try:
    os.mkdir(OUTPUT_DIR)
except:
    FileExistsError
    pass

ret = subprocess.run(
    ["rm {}".format(os.path.join(OUTPUT_DIR, "*.LPC"))], shell=True)

ret = subprocess.run(
    ["rm {}".format(os.path.join(OUTPUT_DIR, "*.Pitch"))], shell=True)

ret = subprocess.run(
    ["rm {}".format(os.path.join(OUTPUT_DIR, "*.wav"))], shell=True)


cfg = {i: d if type(d) == str else num2words(
    d, lang='pl').split(" ")[-1] for i, d in enumerate(data)}

for i, d in cfg.items():
    fn = str(i)

    if d.endswith(".wav"):
        if os.path.exists(d):
            ret = shutil.copy(d, os.path.join(OUTPUT_DIR, fn + '.wav'))
            assert(ret)
        else:
            print("Warning! The file {} doesn't exist".format(d))
            continue

    else:
        ret = subprocess.run(
            ["praat", "--run", "to_wav.praat", fn, "\"{},\"".format(d)])
        assert(ret.returncode == 0)

    ret = subprocess.run(
        ["praat", "--run", "to_lpc.praat", fn])
    assert(ret.returncode == 0)

    ret = subprocess.run(
        ["praat", "--run", "to_pitch.praat", fn])
    assert(ret.returncode == 0)


size_all = 0
data = []

for i, d in cfg.items():
    fn = str(i)
    lfn = os.path.join(OUTPUT_DIR, fn)
    nc, mc, fl, sr, G, A = from_LPC(lfn + ".LPC")
    dx, nx, pitch = from_Pitch(lfn + ".Pitch")
    assert((nx - len(G)) < 10)
    name = "_".join(rep_pl(d).upper().split(" ")).\
        replace("/", "_").replace(".", "_")
    size_all += ((2 * mc) + 2 + 1) * nc
    data.append((name, i, nc, list(zip(A, G, pitch))))

gen_C(nc, mc, fl, sr, data)

print("Overall size: {} kB".format(size_all / 1024))

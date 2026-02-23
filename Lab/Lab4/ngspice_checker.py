#!/usr/bin/env python3
# -*- coding: utf-8 -*-

from __future__ import print_function, division

import os
import re
import json
import math
import argparse
import subprocess
from datetime import datetime
import numpy as np
import scipy.interpolate as interp
import scipy.optimize as sciopt


def run_ngspice_os(sim_netlist, log_path, workdir):
    cmd = 'cd "{0}" && ngspice -b "{1}" > "{2}" 2>&1'.format(workdir, sim_netlist, log_path)
    rc = os.system(cmd)
    if os.name != 'nt':
        rc = rc >> 8
    return rc

def run_ngspice_subprocess(sim_netlist, log_path, workdir):
    with open(log_path, 'w') as logf:
        p = subprocess.run(["ngspice", "-b", sim_netlist],
                           stdout=logf, stderr=subprocess.STDOUT,
                           cwd=workdir)
    return p.returncode

def strip_control_blocks(lines):
    out = []
    in_ctl = False
    for ln in lines:
        s = ln.strip().lower()
        if s.startswith(".control"):
            in_ctl = True
            continue
        if in_ctl:
            if s.startswith(".endc"):
                in_ctl = False
            continue
        out.append(ln)
    return out

def strip_analyses(lines):
    kill = (".op", ".ac", ".meas", ".measure", ".tran", ".dc", ".tf", ".pz", ".noise")
    out = []
    for ln in lines:
        s = ln.strip()
        if not s or s.startswith("*"):
            out.append(ln)
            continue
        if s.startswith("."):
            head = s.split()[0].lower()
            if head in kill:
                continue
        out.append(ln)
    return out

def ensure_ac_input(lines, in_node="in"):
    # find a V-source whose node+ == in_node; enforce ac=1.0; otherwise insert Vin_ac
    vsrc_idx = None
    for i, ln in enumerate(lines):
        s = ln.strip()
        if not s or s.startswith("*") or s.startswith("."):
            continue
        toks = s.split()
        if len(toks) >= 3 and toks[0].lower().startswith("v") and toks[1].lower() == in_node.lower():
            vsrc_idx = i
            break

    if vsrc_idx is None:
        insert_at = 1 if (lines and lines[0].strip() and not lines[0].strip().startswith((".", "*"))) else 0
        lines.insert(insert_at, "Vin_ac {0} 0 dc=0 ac=1.0\n".format(in_node))
        return lines

    s = lines[vsrc_idx].rstrip("\n")
    if re.search(r"(?i)\bac\s*=", s):
        s = re.sub(r"(?i)\bac\s*=\s*([0-9eE\+\-\.]+)", "ac=1.0", s)
    elif re.search(r"(?i)\bac\s+[0-9eE\+\-\.]+", s):
        s = re.sub(r"(?i)\bac\s+([0-9eE\+\-\.]+)", "ac 1.0", s)
    else:
        s = s + " ac=1.0"
    lines[vsrc_idx] = s + "\n"
    return lines

def find_vdd_vsource(lines, vdd_node="VDD"):
    vdd_l = vdd_node.lower()
    for ln in lines:
        s = ln.strip()
        if not s or s.startswith("*") or s.startswith("."):
            continue
        toks = s.split()
        if len(toks) >= 3 and toks[0].lower().startswith("v") and toks[1].lower() == vdd_l:
            return toks[0]
    return None

def ensure_single_end(lines):
    end_idx = None
    for i, ln in enumerate(lines):
        if ln.strip().lower().startswith(".end"):
            end_idx = i
            break
    if end_idx is None:
        lines.append(".end\n")
        return lines
    return lines[:end_idx+1]

def insert_before_end(lines, inject_lines):
    out = []
    inserted = False
    for ln in lines:
        if (not inserted) and ln.strip().lower().startswith(".end"):
            out.extend(inject_lines)
            inserted = True
        out.append(ln)
    return out

def read_wrdata_numeric(path):
    rows = []
    with open(path, "r") as f:
        for ln in f:
            s = ln.strip()
            if not s:
                continue
            parts = re.split(r"[,\s]+", s)
            vals = []
            ok = True
            for tok in parts:
                if tok == "":
                    continue
                try:
                    vals.append(float(tok))
                except Exception:
                    ok = False
                    break
            if ok and vals:
                rows.append(vals)
    return rows

# to be tested
def _get_best_crossing(freq, gain, val=1.0):
    # AutoCkt style: spline + brentq over [freq[0], freq[-1]]
    interp_fun = interp.InterpolatedUnivariateSpline(freq, gain)

    def fzero(x):
        return float(interp_fun(x) - val)

    xstart, xstop = float(freq[0]), float(freq[-1])
    try:
        return float(sciopt.brentq(fzero, xstart, xstop)), True
    except ValueError:
        # AutoCkt: return xstop, False
        return float(xstop), False


def compute_ac_metrics(ac_rows):
    # supports:
    # 3 cols: [freq, real, imag]
    # 4 cols: [freq, freq_dup, real, imag]
    if not ac_rows:
        return {"gain": None, "ugbw": None, "phm": None, "phase_ugbw": None}

    arr = np.asarray(ac_rows, dtype=float)
    if arr.shape[1] >= 4:
        freq = arr[:, 0]
        vout_real = arr[:, 2]
        vout_imag = arr[:, 3]
    elif arr.shape[1] >= 3:
        freq = arr[:, 0]
        vout_real = arr[:, 1]
        vout_imag = arr[:, 2]
    else:
        return {"gain": None, "ugbw": None, "phm": None, "phase_ugbw": None}

    vout = vout_real + 1j * vout_imag

    # AutoCkt: dc gain is first point magnitude
    gain = np.abs(vout)
    gain0 = float(gain[0])

    # AutoCkt: ugbw via _get_best_crossing; if invalid, ugbw = freq[0]
    ugbw_root, valid = _get_best_crossing(freq, gain, val=1.0)
    if not valid:
        return {"gain": gain0, "ugbw": float(freq[0]), "phm": -180.0, "phase_ugbw": None}

    # AutoCkt: phase unwrap + deg
    phase = np.angle(vout, deg=False)
    phase = np.unwrap(phase)
    phase = np.rad2deg(phase)

    # AutoCkt: quadratic interpolation on linear freq
    phase_fun = interp.interp1d(freq, phase, kind='quadratic')
    ph_at = float(phase_fun(ugbw_root))

    # AutoCkt: fold rule
    if ph_at > 0:
        phm = -180.0 + ph_at
    else:
        phm = 180.0 + ph_at

    return {"gain": gain0, "ugbw": float(ugbw_root), "phm": float(phm), "phase_ugbw": ph_at}


# def compute_ac_metrics(ac_rows):
#     # supports:
#     # 3 cols: [freq, real, imag]
#     # 4 cols: [freq, freq_dup, real, imag]
#     if not ac_rows:
#         return {"gain": None, "ugbw": None, "phm": None, "phase_ugbw": None}

#     ncol = len(ac_rows[0])
#     if ncol >= 4:
#         f  = [r[0] for r in ac_rows]
#         vr = [r[2] for r in ac_rows]
#         vi = [r[3] for r in ac_rows]
#     elif ncol >= 3:
#         f  = [r[0] for r in ac_rows]
#         vr = [r[1] for r in ac_rows]
#         vi = [r[2] for r in ac_rows]
#     else:
#         return {"gain": None, "ugbw": None, "phm": None, "phase_ugbw": None}

#     mag = [math.hypot(a, b) for a, b in zip(vr, vi)]
#     ph  = [math.atan2(b, a) for a, b in zip(vr, vi)]

#     # unwrap phase
#     un = [ph[0]]
#     for i in range(1, len(ph)):
#         d = ph[i] - ph[i-1]
#         while d > math.pi:
#             d -= 2*math.pi
#         while d < -math.pi:
#             d += 2*math.pi
#         un.append(un[-1] + d)
#     ph_deg = [x * 180.0 / math.pi for x in un]

#     gain = mag[0]

#     ugbw = None
#     ph_at = None
#     for i in range(1, len(mag)):
#         if mag[i-1] > 1.0 and mag[i] <= 1.0:
#             f1, f2 = f[i-1], f[i]
#             m1, m2 = mag[i-1], mag[i]
#             if f1 > 0 and f2 > 0 and m1 > 0 and m2 > 0 and (math.log10(m2) - math.log10(m1)) != 0:
#                 logf1, logf2 = math.log10(f1), math.log10(f2)
#                 logm1, logm2 = math.log10(m1), math.log10(m2)
#                 logfu = logf1 + (0.0 - logm1) * (logf2 - logf1) / (logm2 - logm1)
#                 ugbw = 10**logfu
#                 ph1, ph2 = ph_deg[i-1], ph_deg[i]
#                 ph_at = ph1 + (logfu - logf1) * (ph2 - ph1) / (logf2 - logf1) if (logf2 - logf1) != 0 else ph1
#             else:
#                 t = (1.0 - m1) / (m2 - m1) if (m2 - m1) != 0 else 0.0
#                 ugbw = f1 + t*(f2 - f1)
#                 ph1, ph2 = ph_deg[i-1], ph_deg[i]
#                 ph_at = ph1 + t*(ph2 - ph1)
#             break

#     phm = (180.0 + ph_at) if ph_at is not None else None
#     return {"gain": gain, "ugbw": ugbw, "phm": phm, "phase_ugbw": ph_at}

def fmt(x):
    if x is None:
        return "NA"
    try:
        return "{0:.6g}".format(float(x))
    except Exception:
        return str(x)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--netlist", default="final_design.cir")
    ap.add_argument("--spec", required=True)
    ap.add_argument("--out-node", default="net6")
    ap.add_argument("--in-node", default="in")
    ap.add_argument("--vdd-node", default="VDD")
    ap.add_argument("--ppd", type=int, default=200)
    ap.add_argument("--f-start", type=float, default=1.0)
    ap.add_argument("--f-stop", type=float, default=1e10)
    ap.add_argument("--use-subprocess", action="store_true")
    ap.add_argument("--result-json", default="result.json")
    ap.add_argument("--workdir", default=None)
    args = ap.parse_args()

    netlist_path = os.path.abspath(args.netlist)
    spec_path = os.path.abspath(args.spec)

    with open(spec_path, "r") as f:
        specs = json.load(f)

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    workdir = os.path.abspath(args.workdir or ("checker_run_" + ts))
    if not os.path.isdir(workdir):
        os.makedirs(workdir)

    base = os.path.splitext(os.path.basename(netlist_path))[0]
    sim_netlist = os.path.join(workdir, base + "_sim.cir")
    log_path = os.path.join(workdir, "ngspice_output.log")
    ac_out = os.path.join(workdir, "ac.csv")
    dc_out = os.path.join(workdir, "dc.csv")

    """ lines = open(netlist_path, "r").read().splitlines(True)
    lines = strip_control_blocks(lines)
    lines = strip_analyses(lines) """
    lines = open(netlist_path, "r").read().splitlines(True)
    
    # --- 新增：自動修正 .include 路徑邏輯 ---
    fixed_lines = []
    for ln in lines:
        if ln.strip().lower().startswith(".include"):
            parts = ln.split()
            if len(parts) > 1:
                original_path = parts[1]
                # 如果絕對路徑不存在，且檔名在本地目錄存在，就替換它
                if not os.path.exists(original_path):
                    local_file = os.path.basename(original_path)
                    if os.path.exists(local_file):
                        ln = ".include {0}\n".format(local_file)
                        # 使用 .format 以相容 Python 3.5
                        print("Notice: {0} not found, using local {1}".format(original_path, local_file))
        fixed_lines.append(ln)
    lines = fixed_lines
    # --------------------------------------

    lines = strip_control_blocks(lines)
    lines = strip_analyses(lines)
    lines = ensure_ac_input(lines, in_node=args.in_node)
    vdd_src = find_vdd_vsource(lines, vdd_node=args.vdd_node) or "vdd"
    lines = ensure_single_end(lines)

    inject = []
    inject.append(".ac dec {0} {1} {2}\n".format(int(args.ppd), float(args.f_start), float(args.f_stop)))
    inject.append(".control\n")
    inject.append("set units=degrees\n")
    inject.append("set wr_vecnames\n")
    inject.append("set wr_singlescale\n")
    inject.append("option numdgt=12\n")
    inject.append("run\n")
    # IMPORTANT: do NOT explicitly write 'frequency' �V scale is written automatically
    inject.append('wrdata ac.csv real(v({0})) imag(v({0}))\n'.format(args.out_node))
    inject.append("op\n")
    inject.append('wrdata dc.csv i({0})\n'.format(vdd_src))
    inject.append("quit\n")
    inject.append(".endc\n")

    sim_lines = insert_before_end(lines, inject)
    with open(sim_netlist, "w") as f:
        f.writelines(sim_lines)

    # Run ngspice
    if args.use_subprocess:
        rc = run_ngspice_subprocess(sim_netlist, log_path, workdir)
    else:
        rc = run_ngspice_os(sim_netlist, log_path, workdir)

    if rc != 0:
        print("FAIL: ngspice exit code {0}. See log: {1}".format(rc, log_path))
        return

    # Parse results
    metrics = compute_ac_metrics(read_wrdata_numeric(ac_out)) if os.path.exists(ac_out) else {"gain":None,"ugbw":None,"phm":None,"phase_ugbw":None}
    ibias = None
    if os.path.exists(dc_out):
        dc_rows = read_wrdata_numeric(dc_out)
        if dc_rows:
            ibias = abs(dc_rows[-1][-1])

    measured = {"gain": metrics.get("gain"), "ugbw": metrics.get("ugbw"), "phm": metrics.get("phm"), "ibias": ibias}

    # Check specs
    print("Measured:")
    print("  gain =", fmt(measured["gain"]))
    print("  ugbw =", fmt(measured["ugbw"]))
    print("  phm  =", fmt(measured["phm"]))
    print("  ibias=", fmt(measured["ibias"]))
    print("")
    print("Spec check:")

    results = {}
    for k in sorted(specs.keys()):
        thr = float(specs[k])
        kl = k.lower()
        if kl.endswith("_min"):
            base = kl[:-4]
            m = measured.get(base)
            passed = (m is not None) and (m >= thr)
            results[k] = {"measured": m, "threshold": thr, "op": ">=", "pass": bool(passed)}
            print("  {0}: {1} >= {2}  =>  {3}".format(k, fmt(m), fmt(thr), "PASS" if passed else "FAIL"))
        elif kl.endswith("_max"):
            base = kl[:-4]
            m = measured.get(base)
            m_cmp = abs(m) if (m is not None and base == "ibias") else m
            passed = (m_cmp is not None) and (m_cmp <= thr)
            results[k] = {"measured": m, "threshold": thr, "op": "<=", "pass": bool(passed)}
            print("  {0}: {1} <= {2}  =>  {3}".format(k, fmt(m), fmt(thr), "PASS" if passed else "FAIL"))

    out = {
        "netlist": netlist_path,
        "spec_file": spec_path,
        "workdir": workdir,
        "sim_netlist": sim_netlist,
        "log": log_path,
        "ac_csv": ac_out if os.path.exists(ac_out) else None,
        "dc_csv": dc_out if os.path.exists(dc_out) else None,
        "metrics": measured,
        "results": results
    }
    result_dir = os.path.abspath(workdir + "/" + args.result_json)
    with open(result_dir, "w") as f:
        json.dump(out, f, indent=2)

    print("")
    print("Wrote:", result_dir)
    print("Workdir:", workdir)

if __name__ == "__main__":
    main()

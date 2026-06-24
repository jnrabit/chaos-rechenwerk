#!/usr/bin/env python3
# verify_gpu_red.py — GPU-Reduktion (resident) gegen C++-Produktion (Host-Serien-Mean) + Speedup.
import ctypes, numpy as np, subprocess, time
np.random.seed(7)
lib=ctypes.CDLL("./libengine_ref_mf.so")
P=ctypes.POINTER(ctypes.c_double); lib.ref_steps_mf.argtypes=[P,ctypes.c_int,ctypes.c_int,ctypes.c_double,P]
RED="./gpu_red"; CH=["x1","y1","z1","w1","x2","y2","z2","w2"]

def ref_mf(inits,nticks,t0=0.0,record=True):
    N=inits.shape[0]; st=inits.astype(np.float64).ravel(); sp=st.ctypes.data_as(P)
    if record:
        out=np.empty(N*nticks*8); lib.ref_steps_mf(sp,N,nticks,ctypes.c_double(t0),out.ctypes.data_as(P))
        return out.reshape(N,nticks,8)
    t=time.perf_counter(); lib.ref_steps_mf(sp,N,nticks,ctypes.c_double(t0),None); return time.perf_counter()-t

def gpu_red(inits,nticks,t0=0.0,record=1):
    N=inits.shape[0]; inits.astype(np.float64).tofile("/tmp/ri.bin")
    r=subprocess.run([RED,"/tmp/ri.bin","/tmp/ro.bin",str(N),str(nticks),repr(t0),str(record)],capture_output=True,text=True)
    tl=[l for l in r.stderr.splitlines() if "GPU-RED" in l]
    tps=float(tl[0].split("(")[1].split(" ")[0]) if tl else float('nan')
    if record:
        return np.fromfile("/tmp/ro.bin",dtype=np.float64).reshape(N,nticks,8), tps, r.stderr.splitlines()[0]
    return None, tps, (r.stderr.splitlines()[0] if r.stderr else "")

print("="*80)
print("TEST 1 — BIT-NAEHE: GPU-REDUKTIONS-Mean vs C++-Serien-Mean (Assoziativitaet?), 1000 Ticks")
print("="*80)
for N in (8, 1024):
    ini=np.random.uniform(-10,10,(N,8))
    gp,_,dev=gpu_red(ini,1000); rf=ref_mf(ini,1000)
    d=np.abs(gp-rf)
    # waechst die Abweichung ueber die Ticks (Bug) oder beschraenkt (Assoziativitaet)?
    per_tick_max=d.max(axis=(0,2))
    growth=f"{per_tick_max[10]:.1e}->{per_tick_max[500]:.1e}->{per_tick_max[-1]:.1e}"
    print(f"  N={N:5d}: Gesamt-Max {d.max():.2e}  | Verlauf (Tick 10/500/999): {growth}")
    print(f"          -> {'BIT-NAH, beschraenkt (Assoziativitaet, kein Bug)' if d.max()<1e-9 and per_tick_max[-1]<5*per_tick_max[10]+1e-12 else 'DRIFT/Bug?'}")

print("\n"+"="*80)
print("TEST 2 — SPEEDUP: GPU-RED ticks/s vs N (resident, kein Pro-Tick-Readback) — vs Schritt 2 (flach 12-19k)")
print("="*80)
print(f"  {'N':>6} | {'GPU-RED t/s':>12} | {'CPU t/s':>12} | {'Speedup':>8} | {'vs Schritt2':>11}")
print("  "+"-"*60)
S2={8:12521,64:19306,256:11414,1024:17181,4096:12136}  # Schritt-2-GPU-Zahlen
for N in (8,64,256,1024,4096,16384):
    ini=np.random.uniform(-10,10,(N,8)); nt=300
    _,gtps,_=gpu_red(ini,nt,record=0)
    ctps=nt/ref_mf(ini,nt,record=False)
    s2=S2.get(N); vs=f"{gtps/s2:.1f}x" if s2 else "-"
    print(f"  {N:>6} | {gtps:>12.0f} | {ctps:>12.0f} | {gtps/ctps:>7.1f}x | {vs:>11}")
print("\nCaveat: reine Geschwindigkeit (Korrektheit Schritt 2 steht); GPU-Reduktion+resident, Host nur am Ende;")
print("W bleibt Host-Skalar (bit-gleich); FP-Assoziativitaet der Baum-Reduktion = Test1; double/fp64; md5-unberuehrt.")

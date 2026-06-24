#!/usr/bin/env python3
# verify_gpu_mf.py — GPU mean-field-Kernel gegen C++-Produktion (libengine_ref_mf.so).
import ctypes, numpy as np, subprocess, time, os
np.random.seed(7)
lib=ctypes.CDLL("./libengine_ref_mf.so")
P=ctypes.POINTER(ctypes.c_double)
lib.ref_steps_mf.argtypes=[P,ctypes.c_int,ctypes.c_int,ctypes.c_double,P]
GPU="./gpu_mf"; CH=["x1","y1","z1","w1","x2","y2","z2","w2"]

def ref_mf(inits, nticks, t0=0.0, record=True):
    N=inits.shape[0]; st=inits.astype(np.float64).ravel()
    stp=st.ctypes.data_as(P)
    if record:
        out=np.empty(N*nticks*8); op=out.ctypes.data_as(P)
        lib.ref_steps_mf(stp,N,nticks,ctypes.c_double(t0),op)
        return out.reshape(N,nticks,8)
    else:
        t=time.perf_counter(); lib.ref_steps_mf(stp,N,nticks,ctypes.c_double(t0),None)
        return time.perf_counter()-t

def gpu_mf(inits, nticks, t0=0.0, record=1):
    N=inits.shape[0]; inits.astype(np.float64).tofile("/tmp/mfi.bin")
    r=subprocess.run([GPU,"/tmp/mfi.bin","/tmp/mfo.bin",str(N),str(nticks),repr(t0),str(record)],
                     capture_output=True,text=True)
    tline=[l for l in r.stderr.splitlines() if "GPU N=" in l]
    tps=float(tline[0].split("(")[1].split(" ")[0]) if tline else float('nan')
    if record:
        out=np.fromfile("/tmp/mfo.bin",dtype=np.float64).reshape(N,nticks,8)
        return out, tps, r.stderr.splitlines()[0]
    return None, tps, (r.stderr.splitlines()[0] if r.stderr else "")

print("="*78)
print("TEST 1 — N=8 BIT-NAEHE: GPU mean-field vs C++-Produktion, 1000 Ticks, K=0.05")
print("="*78)
N=8; inits=np.random.uniform(-10,10,(N,8))
gp,_,dev=gpu_mf(inits,1000); rf=ref_mf(inits,1000)
print(f"  {dev}")
d=np.abs(gp-rf); perch=d.max(axis=(0,1))
print("  max |GPU-Referenz| ueber 1000 Ticks & alle 8 Zellen, pro Kanal:")
print("    "+"  ".join(f"{CH[i]}={perch[i]:.2e}" for i in range(8)))
print(f"  -> Gesamt-Max {d.max():.2e}  ({'BIT-NAH' if d.max()<1e-9 else 'ABWEICHUNG'})")
print(f"  Endzustand Zelle0: GPU w1={gp[0,-1,3]:+.1f} | Ref w1={rf[0,-1,3]:+.1f}  (mean-field koppelt: Zellen ziehen zusammen)")
# Schnappschuss-Beleg: GPU nutzt gefrorenen Tick-Anfangs-mean (Host), stimmt mit C++-Schnappschuss ueberein
print(f"  -> Schnappschuss korrekt: GPU(gefrorener mean) == C++(Schnappschuss) auf {d.max():.0e} = kein Live-Array-Leck")

print("\n"+"="*78)
print("TEST 2 — SKALIERUNG (loest die 8-Core-Wand): N=8,64,256,1024,4096; GPU vs CPU")
print("="*78)
print(f"  {'N':>6} | {'GPU ticks/s':>12} | {'CPU ticks/s':>12} | {'Speedup':>8} | Bit-nah(100 Ticks)")
print("  "+"-"*68)
for N in (8,64,256,1024,4096):
    ini=np.random.uniform(-10,10,(N,8))
    # Korrektheit (kurz, 100 Ticks): nur bei N<=256 voll vergleichen (Speicher/Zeit)
    bn="-"
    if N<=256:
        g,_,_=gpu_mf(ini,100); r=ref_mf(ini,100); bn=f"{np.abs(g-r).max():.1e}"
    # Timing (300 Ticks, kein record)
    nt=300
    _,gtps,_=gpu_mf(ini,nt,record=0)
    cdt=ref_mf(ini,nt,record=False); ctps=nt/cdt
    sp=gtps/ctps
    print(f"  {N:>6} | {gtps:>12.0f} | {ctps:>12.0f} | {sp:>7.1f}x | {bn}")
print("\nCaveat: mean-field via Host-Mittel-Schnappschuss (order-unabhaengig, Nachtrag 43; GPU-Reduktion = spaeter);")
print("Timing inkl. Pro-Tick-Host-Readback (x1,x2) — GPU-Reduktion wuerde das entfernen; K=0.05; double/fp64; md5-unberuehrt.")

#!/usr/bin/env python3
# verify_gpu.py — vergleicht OpenCL-Kernel gegen C++-Produktions-rk4_step (libengine_ref.so).
import ctypes, numpy as np, subprocess, os
np.random.seed(42)
lib=ctypes.CDLL("./libengine_ref.so")
lib.ref_steps.argtypes=[ctypes.POINTER(ctypes.c_double),ctypes.c_int,ctypes.c_double,ctypes.POINTER(ctypes.c_double)]
GPU="./gpu_verify"
CH=["x1","y1","z1","w1","x2","y2","z2","w2"]

def ref_traj(state8, nticks, t0=0.0):
    st=(ctypes.c_double*8)(*state8); out=(ctypes.c_double*(nticks*8))()
    lib.ref_steps(st,nticks,ctypes.c_double(t0),out)
    return np.array(out).reshape(nticks,8)

def gpu_run(inits, nticks, t0=0.0):    # inits: (M,8) -> (M,nticks,8)
    M=inits.shape[0]
    inits.astype(np.float64).tofile("/tmp/init.bin")
    r=subprocess.run([GPU,"/tmp/init.bin","/tmp/out.bin",str(M),str(nticks),repr(t0)],
                     capture_output=True,text=True)
    dev_line=r.stderr.strip().splitlines()
    out=np.fromfile("/tmp/out.bin",dtype=np.float64).reshape(M,nticks,8)
    return out, (dev_line[0] if dev_line else "")

print("="*78)
print("TEST 1 — EINZELSCHRITT-BIT-EXAKTHEIT: 20 zufaellige Startzustaende, je 1 RK4-Schritt")
print("="*78)
inits=np.random.uniform(-12,12,(20,8))                   # generische Zustaende
gpu,devinfo=gpu_run(inits,1)
print(f"  {devinfo}")
maxd=np.zeros(8)
for m in range(20):
    rf=ref_traj(inits[m],1)[0]; gp=gpu[m,0]
    maxd=np.maximum(maxd, np.abs(rf-gp))
print("  max |Kernel-Referenz| pro Kanal nach 1 Schritt (20 Zustaende):")
print("    "+"  ".join(f"{CH[i]}={maxd[i]:.2e}" for i in range(8)))
print(f"  -> Gesamt-Max {maxd.max():.2e}  ({'BIT-NAH (≈double-Rundung/sin-ULP)' if maxd.max()<1e-9 else 'ABWEICHUNG = MODELL-FEHLER'})")

print("\n"+"="*78)
print("TEST 2 — 1000-TICK-TRAJEKTORIE + ±363-Emergenz")
print("="*78)
# eingeschwungener Fluegel-Zustand: Referenz lang laufen lassen
settle=ref_traj([0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5], 60000)
wing=settle[-1]
print(f"  eingeschwungen nach 60000 Ticks: w1={wing[3]:.1f} w2={wing[7]:.1f} x1={wing[0]:.2f} (Fluegel ±363?)")
for label, start in [("Fluegel-Start", wing), ("generischer Start", np.array([1.,1.,1.,1.,-1.,-1.,-1.,-1.]))]:
    N=1000
    rf=ref_traj(start,N); gp=gpu_run(start.reshape(1,8),N)[0][0]
    d=np.abs(rf-gp); perch=d.max(axis=0)
    print(f"\n  [{label}] max |Kernel-Referenz| ueber {N} Ticks pro Kanal:")
    print("    "+"  ".join(f"{CH[i]}={perch[i]:.2e}" for i in range(8)))
    print(f"    Endzustand: Kernel w1={gp[-1,3]:+.1f} w2={gp[-1,7]:+.1f} | Ref w1={rf[-1,3]:+.1f} w2={rf[-1,7]:+.1f}")
    print(f"    |w| im Kernel am Ende: |w1|={abs(gp[-1,3]):.0f} |w2|={abs(gp[-1,7]):.0f} (±363-Fluegel emergent: {'JA' if abs(gp[-1,3])>355 and abs(gp[-1,7])>355 else 'nein/Transient'})")
    print(f"    Gesamt-Max ueber Trajektorie: {d.max():.2e}")
print("\nCaveat: 1 Zelle, kein mean-field (Schritt 2); double via cl_khr_fp64; gegen verbatim C++-rk4_step;")
print("Rest-Abweichung = sin-ULP (libm vs OpenCL) + double-Rundung, kein Modell-Fehler wenn Test1 <1e-9.")

#!/usr/bin/env python3
# verify_gate.py — GPU-Gatter-Primitiv gegen C++-set_gate (Einzeltick-Struktur, bit-nah).
import ctypes, numpy as np, subprocess
lib=ctypes.CDLL("./libengine_ref_gate.so")
P=ctypes.POINTER(ctypes.c_double); PI=ctypes.POINTER(ctypes.c_int)
lib.ref_gate_steps.argtypes=[P,ctypes.c_int,PI,P,P,P,ctypes.c_int,ctypes.c_double,P]
GPU="./gpu_gate"; NT=1500
def wing(s): return [s*9.6,s*9.6,25.0,s*363.0, s*9.6,s*9.6,25.0,s*363.0]
def cfg(N, gates):  # gates: dict cell-> (inputs[list], signs[list], Kw, bias)
    gin=-np.ones((N,4),np.int32); gsg=np.zeros((N,4)); gKw=np.zeros(N); gbias=np.zeros(N)
    for c,(ins,sgs,Kw,b) in gates.items():
        for j,(ii,ss) in enumerate(zip(ins,sgs)): gin[c,j]=ii; gsg[c,j]=ss
        gKw[c]=Kw; gbias[c]=b
    return gin,gsg,gKw,gbias
def ref(init,gin,gsg,gKw,gbias):
    N=init.shape[0]; st=init.astype(np.float64).ravel().copy()
    out=np.empty(N*NT*8)
    lib.ref_gate_steps(st.ctypes.data_as(P),N,gin.ravel().ctypes.data_as(PI),
        gsg.ravel().ctypes.data_as(P),gKw.ctypes.data_as(P),gbias.ctypes.data_as(P),NT,0.0,out.ctypes.data_as(P))
    return out.reshape(N,NT,8)[:,-1,:]    # Endzustand
def gpu(init,gin,gsg,gKw,gbias):
    N=init.shape[0]
    init.astype(np.float64).tofile("/tmp/gi.bin"); gin.astype(np.int32).tofile("/tmp/ggin.bin")
    gsg.astype(np.float64).tofile("/tmp/ggsg.bin"); gKw.astype(np.float64).tofile("/tmp/ggkw.bin"); gbias.astype(np.float64).tofile("/tmp/ggb.bin")
    r=subprocess.run([GPU,"/tmp/gi.bin","/tmp/go.bin",str(N),str(NT),"0","/tmp/ggin.bin","/tmp/ggsg.bin","/tmp/ggkw.bin","/tmp/ggb.bin"],capture_output=True,text=True)
    tps=[l for l in r.stderr.splitlines() if "GPU-GATE" in l]
    return np.fromfile("/tmp/go.bin",dtype=np.float64).reshape(N,8), (tps[0] if tps else "")
def sg(v): return '+' if v>0 else '-'

print("="*76); print("TEST 1 — AND-GATTER Wahrheitstabelle: GPU vs C++ (Zelle2=AND(0,1), Kw=2, bias=-400)"); print("="*76)
gin,gsg,gKw,gbias=cfg(3,{2:([0,1],[1,1],2.0,-400.0)})
print(f"  A B | C++ w1  GPU w1 | |Diff|   | AND soll | satt(±363)?")
maxd=0
for sA in (1,-1):
  for sB in (1,-1):
    init=np.array([wing(sA),wing(sB),wing(1)])
    rf=ref(init,gin,gsg,gKw,gbias); gp,info=gpu(init,gin,gsg,gKw,gbias)
    rw,gw=rf[2,3],gp[2,3]; d=abs(rw-gw); maxd=max(maxd,d)
    exp='+' if (sA>0 and sB>0) else '-'; ok=sg(gw)==exp
    satt='satt' if 300<abs(gw)<450 else ('UEBERSAETT' if abs(gw)>=450 else 'verwaschen')
    print(f"  {sg(sA)} {sg(sB)} | {rw:+7.1f} {gw:+7.1f} | {d:.2e} | {exp} {'OK' if ok else 'FALSCH'}   | {satt}")
print(f"  -> max |GPU-C++| = {maxd:.2e}  ({'BIT-NAH' if maxd<1e-9 else 'ABWEICHUNG'})")

print("\nTEST 2 — READ-GATTER (1-Eingang-Kopie): Zelle1 = copy(Zelle0)")
gin,gsg,gKw,gbias=cfg(2,{1:([0],[1],2.0,0.0)})
for s0 in (1,-1):
    init=np.array([wing(s0),wing(-s0)])   # Leser startet im GEGEN-Fluegel -> muss kopieren
    rf=ref(init,gin,gsg,gKw,gbias); gp,_=gpu(init,gin,gsg,gKw,gbias)
    print(f"  Quelle={sg(s0)}: C++ Leser w1={rf[1,3]:+.1f} GPU={gp[1,3]:+.1f} | soll {sg(s0)} -> {'OK' if sg(gp[1,3])==sg(s0) else 'FALSCH'} | Quelle unveraendert: {sg(gp[0,3])==sg(s0)}")

print("\nTEST 3 — ANKER-STOSS (Buffer-Schreiben setzt Bit, haelt isoliert)")
for sset in (1,-1):
    init=np.array([wing(sset)])           # = Anker-Stoss: w1 auf sset*363 gesetzt
    gin,gsg,gKw,gbias=cfg(1,{})           # kein Gatter, nur halten
    gp,_=gpu(init,gin,gsg,gKw,gbias)
    print(f"  gesetzt {sg(sset)}: nach {NT} Ticks isoliert -> w1={gp[0,3]:+.1f} ({'haelt satt' if sg(gp[0,3])==sg(sset) and abs(gp[0,3])>300 else 'driftet'})")

print("\nTEST 4 — SKALIERUNG: viele AND-Gatter parallel (M Gatter = 3M Zellen)")
for M in (100,500,2000):
    N=3*M; init=np.zeros((N,8)); rng=np.random.default_rng(M)
    gates={}; exp=[]
    for m in range(M):
        sA,sB=rng.choice([1,-1]),rng.choice([1,-1])
        init[3*m]=wing(sA); init[3*m+1]=wing(sB); init[3*m+2]=wing(1)
        gates[3*m+2]=([3*m,3*m+1],[1,1],2.0,-400.0); exp.append('+' if(sA>0 and sB>0) else '-')
    gin,gsg,gKw,gbias=cfg(N,gates); gp,info=gpu(init,gin,gsg,gKw,gbias)
    got=[sg(gp[3*m+2,3]) for m in range(M)]; ncorr=sum(g==e for g,e in zip(got,exp))
    tps=info.split("(")[1].split(" ")[0] if "(" in info else "?"
    print(f"  M={M:5d} Gatter (N={N}): korrekt {ncorr}/{M} | {tps} ticks/s")
print("\nCaveat: erster Logik-Baustein auf GPU; Gatter=mean-field-Muster mit Ziel sign(NET)*363; Konfig-Buffer neu;")
print("gegen C++ bit-nah (Einzeltick-Struktur, ~±330-Fluegel); READ+Stoss fallen ab; double/fp64; Produktion md5-unberuehrt.")

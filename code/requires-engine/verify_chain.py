#!/usr/bin/env python3
# verify_chain.py — verkettete Mehr-Gatter-Logik auf GPU (XNOR/XOR/Konflikt) vs C++ (kontinuierlich).
import ctypes, numpy as np, subprocess
lib=ctypes.CDLL("./libengine_ref_gate.so")
P=ctypes.POINTER(ctypes.c_double); PI=ctypes.POINTER(ctypes.c_int)
lib.ref_gate_steps.argtypes=[P,ctypes.c_int,PI,P,P,P,ctypes.c_int,ctypes.c_double,P]
GPU="./gpu_gate"; NT=2000; KW=2.0
def wing(s): return [s*9.6,s*9.6,25.0,s*363.0, s*9.6,s*9.6,25.0,s*363.0]
def cfg(N,gates):
    gin=-np.ones((N,4),np.int32); gsg=np.zeros((N,4)); gKw=np.zeros(N); gbias=np.zeros(N)
    for c,(ins,sgs,b) in gates.items():
        for j,(ii,ss) in enumerate(zip(ins,sgs)): gin[c,j]=ii; gsg[c,j]=ss
        gKw[c]=KW; gbias[c]=b
    return gin,gsg,gKw,gbias
def ref(init,g):
    N=init.shape[0]; st=init.astype(np.float64).ravel().copy(); out=np.empty(N*NT*8)
    lib.ref_gate_steps(st.ctypes.data_as(P),N,g[0].ravel().ctypes.data_as(PI),g[1].ravel().ctypes.data_as(P),
        g[2].ctypes.data_as(P),g[3].ctypes.data_as(P),NT,0.0,out.ctypes.data_as(P))
    return out.reshape(N,NT,8)[:,-1,:]
def gpu(init,g):
    N=init.shape[0]; init.astype(np.float64).tofile("/tmp/ci.bin"); g[0].astype(np.int32).tofile("/tmp/cgin.bin")
    g[1].astype(np.float64).tofile("/tmp/cgsg.bin"); g[2].astype(np.float64).tofile("/tmp/cgkw.bin"); g[3].astype(np.float64).tofile("/tmp/cgb.bin")
    r=subprocess.run([GPU,"/tmp/ci.bin","/tmp/co.bin",str(N),str(NT),"0","/tmp/cgin.bin","/tmp/cgsg.bin","/tmp/cgkw.bin","/tmp/cgb.bin"],capture_output=True,text=True)
    t=[l for l in r.stderr.splitlines() if "GPU-GATE" in l]
    return np.fromfile("/tmp/co.bin",dtype=np.float64).reshape(N,8), (t[0].split("(")[1].split(" ")[0] if t and "(" in t[0] else "?")
def sg(v): return '+' if v>0 else '-'
# Gatter-Konstruktoren (a,b = Zellindizes)
def OR(a,b):  return ([a,b],[1,1],+400.0)
def NAND(a,b):return ([a,b],[-1,-1],+400.0)
def AND(a,b): return ([a,b],[1,1],-400.0)
def AND3(a,b,c): return ([a,b,c],[1,1,1],-800.0)

# XNOR-Netz (5 Zellen): 0=a 1=b | 2=OR 3=NAND 4=XNOR=NAND(2,3)
def xnor_cfg(): return cfg(5,{2:OR(0,1),3:NAND(0,1),4:NAND(2,3)})
def xor_cfg():  return cfg(5,{2:OR(0,1),3:NAND(0,1),4:AND(2,3)})  # XOR=AND(OR,NAND)

print("="*78); print("TEST 1 — XNOR (Gleichheit) verkettet: OR->NAND->NAND(Z1,Z2). GPU vs C++"); print("="*78)
g=xnor_cfg(); md=0
print("  a b | C++ XNOR  GPU | |Diff| | soll(gleich) | Zwischenstufen satt")
for sa in (1,-1):
  for sb in (1,-1):
    init=np.array([wing(sa),wing(sb),wing(1),wing(1),wing(1)])
    rf=ref(init,g); gp,_=gpu(init,g); d=abs(rf[4,3]-gp[4,3]); md=max(md,d)
    exp='+' if sa==sb else '-'; inter=[abs(gp[c,3]) for c in (2,3,4)]
    print(f"  {sg(sa)} {sg(sb)} | {sg(rf[4,3])}({rf[4,3]:+.0f}) {sg(gp[4,3])}({gp[4,3]:+.0f}) | {d:.1e} | {exp} {'OK' if sg(gp[4,3])==exp else 'X'} | min{min(inter):.0f} max{max(inter):.0f} {'satt' if 300<min(inter) and max(inter)<450 else 'PROBLEM'}")
print(f"  -> XNOR max|GPU-C++|={md:.1e} ({'BIT-EXAKT' if md==0 else 'bit-nah' if md<1e-9 else 'DRIFT'})")

print("\nTEST 2 — XOR (Verschiedenheit): OR->NAND->AND(Z1,Z2)")
g=xor_cfg(); md=0
for sa in (1,-1):
  for sb in (1,-1):
    init=np.array([wing(sa),wing(sb),wing(1),wing(1),wing(1)])
    rf=ref(init,g); gp,_=gpu(init,g); d=abs(rf[4,3]-gp[4,3]); md=max(md,d)
    exp='+' if sa!=sb else '-'
    print(f"  {sg(sa)} {sg(sb)} -> XOR GPU {sg(gp[4,3])} (soll {exp}) {'OK' if sg(gp[4,3])==exp else 'X'} | |Diff| {d:.1e}")
print(f"  -> XOR max|GPU-C++|={md:.1e}")

print("\n"+"="*78); print("TEST 3 — KONFLIKT (16 Zellen, Tiefe 4): XNOR(S)·XNOR(P)·XOR(O), 3-Eingang-AND"); print("="*78)
# 0-5 = S1,S2,P1,P2,O1,O2 | 6,7,8=XNOR_S | 9,10,11=XNOR_P | 12,13,14=XOR_O | 15=AND3
conf=cfg(16,{6:OR(0,1),7:NAND(0,1),8:NAND(6,7), 9:OR(2,3),10:NAND(2,3),11:NAND(9,10),
             12:OR(4,5),13:NAND(4,5),14:AND(12,13), 15:AND3(8,11,14)})
faelle=[("Widerspruch S=P=O!=",(1,1,0),(1,1,1),1),("O gleich",(1,1,1),(1,1,1),0),
        ("S verschieden",(1,1,0),(0,1,1),0),("P verschieden",(1,1,0),(1,0,1),0)]
md=0
print("  Fall                  | T1     T2     | Cs Cp Co | C++ GPU Konflikt | soll | satt-Tiefe")
for nm,T1,T2,exp in faelle:
    init=np.zeros((16,8))
    for k,v in enumerate([T1[0],T2[0],T1[1],T2[1],T1[2],T2[2]]): init[k]=wing(1 if v else -1)
    for k in range(6,16): init[k]=wing(1)
    rf=ref(init,conf); gp,info=gpu(init,conf); d=abs(rf[15,3]-gp[15,3]); md=max(md,d)
    Cs,Cp,Co=sg(gp[8,3]),sg(gp[11,3]),sg(gp[14,3]); konf=sg(gp[15,3])
    inter=[abs(gp[c,3]) for c in (6,7,8,9,10,11,12,13,14,15)]
    ok=(konf=='+')==(exp==1)
    print(f"  {nm:21}| {T1} {T2} | {Cs}  {Cp}  {Co}  | {sg(rf[15,3])}   {konf}  ={1 if konf=='+' else 0}       | {exp} {'OK' if ok else 'X'} | min{min(inter):.0f} {'satt' if min(inter)>300 and max(inter)<450 else 'PROBLEM'} d={d:.0e}")
print(f"  -> KONFLIKT max|GPU-C++|={md:.1e} ({'BIT-EXAKT (Regeneration traegt durch Tiefe 4)' if md==0 else 'bit-nah' if md<1e-9 else 'DRIFT'})")

print("\nTEST 4 — SKALIERUNG: M Konflikt-Komparatoren parallel (16M Zellen)")
base={6:OR(0,1),7:NAND(0,1),8:NAND(6,7),9:OR(2,3),10:NAND(2,3),11:NAND(9,10),12:OR(4,5),13:NAND(4,5),14:AND(12,13),15:AND3(8,11,14)}
for M in (50,200,1000):
    N=16*M; init=np.zeros((N,8)); gates={}; exp=[]; rng=np.random.default_rng(M)
    for m in range(M):
        o=16*m; T=[rng.choice([0,1]) for _ in range(6)]   # S1,S2,P1,P2,O1,O2
        for k in range(6): init[o+k]=wing(1 if T[k] else -1)
        for k in range(6,16): init[o+k]=wing(1)
        for c,(ins,sgs,b) in base.items(): gates[o+c]=([o+i for i in ins],sgs,b)
        exp.append(1 if (T[0]==T[1] and T[2]==T[3] and T[4]!=T[5]) else 0)
    g=cfg(N,gates); gp,info=gpu(init,g)
    got=[1 if gp[16*m+15,3]>0 else 0 for m in range(M)]; ncorr=sum(a==b for a,b in zip(got,exp))
    print(f"  M={M:4d} Konflikte (N={N}): korrekt {ncorr}/{M} | {info} ticks/s")
print("\nCaveat: verkettete Logik auf GPU; Regenerations-Bauregel (Nachtrag 45) auf GPU; kontinuierlich (kein Staging noetig);")
print("XNOR/XOR/Konflikt=Mini-Ossifikat-Ops; gegen C++ bit-nah; double/fp64; Produktion md5-unberuehrt.")

#!/usr/bin/env python3
# verify_ossifikat.py — Mini-Ossifikat Teil 1: ein neues Tripel PARALLEL gegen N bestehende
# auf Konflikt pruefen (eins-gegen-viele Broadcast, N Komparatoren in EINEM Settle, Bulk-Readback).
import numpy as np, subprocess, time
GPU="./gpu_gate"; NT=2000
def wing(s): return [s*9.6,s*9.6,25.0,s*363.0, s*9.6,s*9.6,25.0,s*363.0]
KW=2.0
# Layout: Zelle 0,1,2 = S_neu,P_neu,O_neu (geteilt, Broadcast). Komparator m ab base=3+13m:
#   base+0,1,2 = S_m,P_m,O_m (bestehend) | base+3..12 = 10 Konflikt-Gatter, Flag=base+12
def build(new, existing):
    N=len(existing); NC=3+13*N
    init=np.zeros((NC,8)); gin=-np.ones((NC,4),np.int32); gsg=np.zeros((NC,4)); gKw=np.zeros(NC); gb=np.zeros(NC)
    SN,PN,ON=0,1,2
    init[SN]=wing(1 if new[0] else -1); init[PN]=wing(1 if new[1] else -1); init[ON]=wing(1 if new[2] else -1)
    def G(c,ins,sgs,b):
        for j,(i,s) in enumerate(zip(ins,sgs)): gin[c,j]=i; gsg[c,j]=s
        gKw[c]=KW; gb[c]=b
    for m,(s,p,o) in enumerate(existing):
        b=3+13*m
        init[b+0]=wing(1 if s else -1); init[b+1]=wing(1 if p else -1); init[b+2]=wing(1 if o else -1)
        for k in range(3,13): init[b+k]=wing(1)
        # XNOR_S = NAND(OR(SN,S_m), NAND(SN,S_m))   (Broadcast: SN=Zelle0 fuer ALLE m)
        G(b+3,[SN,b+0],[1,1],+400); G(b+4,[SN,b+0],[-1,-1],+400); G(b+5,[b+3,b+4],[-1,-1],+400)
        # XNOR_P
        G(b+6,[PN,b+1],[1,1],+400); G(b+7,[PN,b+1],[-1,-1],+400); G(b+8,[b+6,b+7],[-1,-1],+400)
        # XOR_O = AND(OR(ON,O_m), NAND(ON,O_m))
        G(b+9,[ON,b+2],[1,1],+400); G(b+10,[ON,b+2],[-1,-1],+400); G(b+11,[b+9,b+10],[1,1],-400)
        # AND3 -> Flag
        G(b+12,[b+5,b+8,b+11],[1,1,1],-800)
    return init,gin,gsg,gKw,gb,N
def run(init,gin,gsg,gKw,gb):
    N8=init.shape[0]; init.astype(np.float64).tofile("/tmp/oi.bin"); gin.astype(np.int32).tofile("/tmp/ogin.bin")
    gsg.astype(np.float64).tofile("/tmp/ogsg.bin"); gKw.astype(np.float64).tofile("/tmp/ogkw.bin"); gb.astype(np.float64).tofile("/tmp/ogb.bin")
    t=time.perf_counter()
    r=subprocess.run([GPU,"/tmp/oi.bin","/tmp/oo.bin",str(N8),str(NT),"0","/tmp/ogin.bin","/tmp/ogsg.bin","/tmp/ogkw.bin","/tmp/ogb.bin"],capture_output=True,text=True)
    wall=time.perf_counter()-t
    st=np.fromfile("/tmp/oo.bin",dtype=np.float64).reshape(N8,8)   # Bulk-Readback (1 Transfer)
    return st, wall
def flags_of(st,N): return [1 if st[3+13*m+12,3]>0 else 0 for m in range(N)]
def ref_flags(new,existing): return [1 if (new[0]==s and new[1]==p and new[2]!=o) else 0 for (s,p,o) in existing]

print("="*76); print("MINI-OSSIFIKAT Teil 1: ein neues Tripel gegen N bestehende, PARALLEL"); print("="*76)
tests=[
 ("KONSISTENT (kein Konflikt)", (1,1,0), [(1,1,0),(0,1,1),(1,0,1),(0,0,0)]),
 ("WIDERSPRUCH (mit Tripel 0)",  (1,1,1), [(1,1,0),(0,1,1),(1,0,1),(0,0,0)]),
 ("TRENNSCHAERFE (nur Tripel 2)",(1,1,0), [(1,1,0),(1,0,1),(1,1,1),(0,1,1)]),
]
for name,new,exist in tests:
    init,gin,gsg,gKw,gb,N=build(new,exist); st,_=run(init,gin,gsg,gKw,gb)
    fl=flags_of(st,N); rf=ref_flags(new,exist)
    conflict=any(fl); which=[i for i,f in enumerate(fl) if f]
    ok=fl==rf
    print(f"\n  {name}: neu={new}, bestehend={exist}")
    print(f"     GPU-Flags={fl} | Ref={rf} | {'PASST' if ok else 'FALSCH'}")
    print(f"     -> Melde-ODER: {'KONFLIKT mit Tripel '+str(which) if conflict else 'KONSISTENT, aufnehmbar'}")

print("\n"+"="*76); print("SKALIERUNG: N=10/100/1000 bestehende, ein neues Tripel, EIN Settle"); print("="*76)
print(f"  {'N':>5} | {'Flags korrekt':>14} | {'Konflikte':>9} | {'Zellen':>7} | {'wall (ms)':>9}")
for N in (10,100,1000):
    rng=np.random.default_rng(N); exist=[tuple(int(x) for x in rng.integers(0,2,3)) for _ in range(N)]
    new=tuple(int(x) for x in rng.integers(0,2,3))
    init,gin,gsg,gKw,gb,_=build(new,exist); st,wall=run(init,gin,gsg,gKw,gb)
    fl=flags_of(st,N); rf=ref_flags(new,exist); ncorr=sum(a==b for a,b in zip(fl,rf))
    print(f"  {N:>5} | {ncorr:>6}/{N:<6} | {sum(fl):>9} | {3+13*N:>7} | {wall*1000:>9.0f}")
print("\nCaveat: erste Mini-Ossifikat-Kern-Op (eins-gegen-viele parallel); Komposition Nachtrag 56/57 + Parallel 58;")
print("Broadcast via geteilte Zellen 0/1/2; ODER ueber N Flags; Strom (Teil 2) spaeter; gegen Python-Ref; md5-unberuehrt.")

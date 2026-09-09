#!/usr/bin/env python3
# verify_strom_regeln.py — Mini-Ossifikat Teil 3: REGELN (Retract + Staging/Ratifizierung).
# Erweitert den lebenden Wissensspeicher (Teil 2) um zwei Ossifikat-Regeln, auf dem SELBEN
# GPU-Konflikt-Primitiv (unveraendert, eins-gegen-viele, 1 Settle, Bulk-Readback, Flag-ODER):
#   A (add)      neues Tripel gegen den AKTIVEN Bestand (committed ∪ pending) pruefen;
#                kein Konflikt -> STAGEN (pending), Konflikt -> VERWERFEN.
#   X (ratify)   alle pending -> committed (kein Re-Check noetig: beim Stagen schon gegen
#                alle Aktiven geprueft; Konflikt ist symmetrisch + statisch).
#   R (retract)  alle aktiven Fakten mit Subjekt S + Praedikat P entfernen (Slot frei).
# Der aktive Bestand (committed ∪ pending) lebt host-seitig als Liste; die Konflikt-Pruefung
# bleibt auf der GPU. Der endgueltige Stand = committed-Bestand, exakt gegen Python-Referenz.
# double/fp64; gpu_gate UNVERAENDERT.
import numpy as np, subprocess, time
GPU="./gpu_gate"; NT=2000; KW=2.0; MAX_SLOTS=1000
def wing(s): return [s*9.6,s*9.6,25.0,s*363.0, s*9.6,s*9.6,25.0,s*363.0]

# ---- Kern-Op (identisch Teil 1/2): neues Tripel gegen die aktiven Slots, EIN Settle ----
def build(new, bestand):
    N=len(bestand); NC=3+13*N
    init=np.zeros((NC,8)); gin=-np.ones((NC,4),np.int32); gsg=np.zeros((NC,4)); gKw=np.zeros(NC); gb=np.zeros(NC)
    SN,PN,ON=0,1,2
    init[SN]=wing(1 if new[0] else -1); init[PN]=wing(1 if new[1] else -1); init[ON]=wing(1 if new[2] else -1)
    def G(c,ins,sgs,b):
        for j,(i,s) in enumerate(zip(ins,sgs)): gin[c,j]=i; gsg[c,j]=s
        gKw[c]=KW; gb[c]=b
    for m,(s,p,o) in enumerate(bestand):
        b=3+13*m
        init[b+0]=wing(1 if s else -1); init[b+1]=wing(1 if p else -1); init[b+2]=wing(1 if o else -1)
        for k in range(3,13): init[b+k]=wing(1)
        G(b+3,[SN,b+0],[1,1],+400); G(b+4,[SN,b+0],[-1,-1],+400); G(b+5,[b+3,b+4],[-1,-1],+400)   # XNOR_S
        G(b+6,[PN,b+1],[1,1],+400); G(b+7,[PN,b+1],[-1,-1],+400); G(b+8,[b+6,b+7],[-1,-1],+400)   # XNOR_P
        G(b+9,[ON,b+2],[1,1],+400); G(b+10,[ON,b+2],[-1,-1],+400); G(b+11,[b+9,b+10],[1,1],-400)  # XOR_O
        G(b+12,[b+5,b+8,b+11],[1,1,1],-800)                                                       # AND3 -> Flag
    return init,gin,gsg,gKw,gb,N
def gpu_check(new, bestand):
    if len(bestand)==0: return [], 0.0
    init,gin,gsg,gKw,gb,N=build(new,bestand)
    init.astype(np.float64).tofile("/tmp/ri.bin"); gin.astype(np.int32).tofile("/tmp/rgin.bin")
    gsg.astype(np.float64).tofile("/tmp/rgsg.bin"); gKw.astype(np.float64).tofile("/tmp/rgkw.bin"); gb.astype(np.float64).tofile("/tmp/rgb.bin")
    t=time.perf_counter()
    subprocess.run([GPU,"/tmp/ri.bin","/tmp/ro.bin",str(3+13*N),str(NT),"0","/tmp/rgin.bin","/tmp/rgsg.bin","/tmp/rgkw.bin","/tmp/rgb.bin"],capture_output=True,text=True)
    wall=time.perf_counter()-t
    st=np.fromfile("/tmp/ro.bin",dtype=np.float64).reshape(3+13*N,8)
    flags=[1 if st[3+13*m+12,3]>0 else 0 for m in range(N)]
    return flags, wall

# ---- REGEL-Schleife: add / ratify / retract, ueber den aktiven Bestand (committed ∪ pending) ----
def engine(stream, anfangsbestand=None, verbose=False):
    bestand=list(anfangsbestand) if anfangsbestand else []   # aktive (s,p,o)
    status=['committed']*len(bestand)                         # 'pending' | 'committed'
    prot=[]; twall=0.0
    for op in stream:
        if op[0]=='A':
            S,P,O=op[1],op[2],op[3]
            assert len(bestand)<MAX_SLOTS, "MAX_SLOTS ueberschritten"
            flags,w=gpu_check((S,P,O),bestand); twall+= (w if bestand else 0.0)
            which=[k for k,f in enumerate(flags) if f]
            if which:
                prot.append(("VERWORFEN",(S,P,O),which))
                if verbose: print(f"    A {S}{P}{O}  VERWORFEN   -> Konflikt mit {which}")
            else:
                slot=len(bestand); bestand.append((S,P,O)); status.append('pending')
                prot.append(("STAGED",(S,P,O),[]))
                if verbose: print(f"    A {S}{P}{O}  STAGED      -> Slot {slot} (pending)")
        elif op[0]=='X':
            n=sum(1 for st in status if st=='pending')
            status=['committed']*len(status)
            prot.append(("RATIFIZIERT",n,[]))
            if verbose: print(f"    X ratify: {n} pending -> committed")
        elif op[0]=='R':
            S,P=op[1],op[2]
            keep=[i for i,(s,p,o) in enumerate(bestand) if not (s==S and p==P)]
            removed=len(bestand)-len(keep)
            bestand=[bestand[i] for i in keep]; status=[status[i] for i in keep]
            prot.append(("ZURUECKGEZOGEN",removed,[]))
            if verbose: print(f"    R {S}{P}  ZURUECKGEZOGEN ({removed} Fakten)")
        else:
            raise ValueError(f"unbekannte Operation {op}")
    committed=[bestand[i] for i in range(len(bestand)) if status[i]=='committed']
    return committed, prot, twall

# ---- Python-Referenz: dieselben Regeln, rein sequenziell, NumPy-frei ----
def ref_engine(stream, anfangsbestand=None):
    bestand=list(anfangsbestand) if anfangsbestand else []
    status=['committed']*len(bestand)
    for op in stream:
        if op[0]=='A':
            S,P,O=op[1],op[2],op[3]
            konflikt=any(s==S and p==P and o!=O for (s,p,o) in bestand)
            if not konflikt: bestand.append((S,P,O)); status.append('pending')
        elif op[0]=='X':
            status=['committed']*len(status)
        elif op[0]=='R':
            S,P=op[1],op[2]
            keep=[i for i,(s,p,o) in enumerate(bestand) if not (s==S and p==P)]
            bestand=[bestand[i] for i in keep]; status=[status[i] for i in keep]
    return [bestand[i] for i in range(len(bestand)) if status[i]=='committed']

# ================================================================
print("="*78); print("MINI-OSSIFIKAT Teil 3 — REGELN (Retract + Staging/Ratifizierung)"); print("="*78)

# ---- Szenario A: lesbarer Lebenszyklus (S:Himmel=1/Gras=0, P:Farbe=1/Zustand=0, O:gruen=1/blau=0) ----
print("\nSZENARIO A — lesbarer Lebenszyklus: add -> staged, ratify -> committed, retract -> frei")
stromA=[('A',1,1,0),('A',0,1,1),('A',1,1,1),('X',),('A',1,1,1),('R',0,1),('A',0,1,0),('X',)]
labelA=["A Himmel-Farbe-blau","A Gras-Farbe-gruen","A Himmel-Farbe-gruen (Konflikt!)",
        "X ratify","A Himmel-Farbe-gruen (immer noch Konflikt)","R Gras-Farbe (retract)",
        "A Gras-Farbe-blau (Slot wieder frei)","X ratify"]
commitA,protA,_=engine(stromA,verbose=True)
refA=ref_engine(stromA)
print(f"  Endbestand GPU (committed, {len(commitA)}): {commitA}")
print(f"  Referenz                 ({len(refA)}): {refA}")
print(f"  -> == Referenz: {'JA' if commitA==refA else 'NEIN'}")
exp_prot=[("STAGED",),("STAGED",),("VERWORFEN",),("RATIFIZIERT",2),("VERWORFEN",),("ZURUECKGEZOGEN",1),("STAGED",),("RATIFIZIERT",1)]
got=[(p[0],)+((p[1],) if isinstance(p[1],int) else ()) for p in protA]
print(f"  -> Protokoll-Signatur {[''.join(map(str,g)) for g in got]}  {'OK' if all(p[0]==e[0] for p,e in zip(protA,exp_prot)) else 'ABWEICHUNG'}")

# ---- Szenario B: Anfangsbestand + Retract eines INITIAL-Slots + Staging-Race ----
print("\nSZENARIO B — Anfangsbestand [(0,1,1),(1,0,0)], Retract eines Initial-Slots, Staging-Race")
initB=[(0,1,1),(1,0,0)]
stromB=[('A',0,1,0),('R',0,1),('A',0,1,0),('A',1,0,1),('X',)]   # 1.Konflikt init; retract->frei; wieder frei; staged; ratify
commitB,protB,_=engine(stromB,anfangsbestand=initB,verbose=True)
refB=ref_engine(stromB,anfangsbestand=initB)
print(f"  Endbestand GPU: {commitB} | Referenz: {refB} | == {'JA' if commitB==refB else 'NEIN'}")
print(f"  -> Retract traf Initial-Slot 0 (pending-geschuetztes S+P wieder frei): "
      f"{'JA' if protB[1][0]=='ZURUECKGEZOGEN' and protB[1][1]==1 else 'NEIN'}")

# ---- Szenario C: Zufalls-Strom (Seed-fest), Endbestand exakt gegen Referenz ----
print("\nSZENARIO C — Zufalls-Strom (Seed-fest): add/ratify/retract, Endbestand exakt gegen Referenz")
def gen_stream(n_ops, seed):
    rng=np.random.default_rng(seed)
    active=[]; ops=[]
    for _ in range(n_ops):
        r=rng.random()
        if r<0.62 or not active:
            ops.append(('A',int(rng.integers(0,2)),int(rng.integers(0,2)),int(rng.integers(0,2))))
            # nur gelegentlich ratify/retract einhaengen
        elif r<0.78:
            ops.append(('X',))
        else:
            s,p,o=active[rng.integers(0,len(active))]
            ops.append(('R',int(s),int(p)))
        # aktiven Bestand fuer Retract-Ziele nachfuehren (nur committed)
        st=list(active)
        for op in ops[-1:]:
            if op[0]=='A':
                S,P,O=op[1],op[2],op[3]
                if not any(s==S and p==P and o!=O for (s,p,o) in st): st.append((S,P,O))
            elif op[0]=='R':
                S,P=op[1],op[2]; st=[(s,p,o) for (s,p,o) in st if not(s==S and p==P)]
        active=st
    return ops
for n_ops in (50,200,500):
    stream=gen_stream(n_ops, 1000+n_ops)
    t0=time.perf_counter()
    commit,prot,twall=engine(stream)
    wall=time.perf_counter()-t0
    ref=ref_engine(stream)
    eq='JA' if commit==ref else 'NEIN'
    nadd=sum(1 for p in prot if p[0]=='STAGED'); nver=sum(1 for p in prot if p[0]=='VERWORFEN')
    nret=sum(1 for p in prot if p[0]=='ZURUECKGEZOGEN' and p[1]>0); nrat=sum(1 for p in prot if p[0]=='RATIFIZIERT' and p[1]>0)
    print(f"  {n_ops:>6} Ops | committed {len(commit):>3} | staged {nadd:>3} | verworfen {nver:>3} | retract {nret:>3} | ratify {nrat:>3} | ==Ref {eq} | wall {wall:>6.1f}s")
    assert commit==ref, f"Endbestand weicht ab bei {n_ops} Ops"

print("\nCaveat: Regel-Erweiterung des lebenden Wissensspeichers (Teil 2) — Retract + Staging/Ratifizierung;")
print("GPU-Konflikt-Primitiv UNVERAENDERT (eins-gegen-viele, 1 Settle); Orchestrierung host-sequenziell (LEITFADEN §5);")
print("Konflikt symmetrisch+statisch => ratify ohne Re-Check korrekt; gegen Python-Referenz; double/fp64; md5-unberuehrt.")

#!/usr/bin/env python3
# verify_strom.py — Mini-Ossifikat Teil 2: DER STROM (lebender Wissensspeicher).
# Ein Strom von Tripeln, jedes gegen den WACHSENDEN Bestand auf Konflikt geprueft:
#   kein Konflikt -> aufnehmen (Bestand waechst, n_aktiv++), Konflikt -> melden + verwerfen.
# Kern-Op = Teil 1 (Nachtrag 59): Broadcast + n_aktiv Komparatoren + 1 Settle + Bulk-Readback + Flag-ODER.
# Strom-Schleife host-sequenziell (Nachtrag 58). gpu_gate UNVERAENDERT. double/fp64. Produktion md5-unberuehrt.
import numpy as np, subprocess, time
GPU="./gpu_gate"; NT=2000; KW=2.0; MAX_SLOTS=1000
def wing(s): return [s*9.6,s*9.6,25.0,s*363.0, s*9.6,s*9.6,25.0,s*363.0]

# ---- Kern-Op aus Teil 1: neues Tripel gegen die aktiven Slots, EIN Settle ----
# Layout: Zelle 0,1,2 = S_neu,P_neu,O_neu (Broadcast). Komparator m ab base=3+13m:
#   base+0,1,2 = bestehendes Tripel | base+3..12 = 10 Konflikt-Gatter, Flag=base+12
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
    """Kern-Op: Flags pro aktivem Slot (1=Konflikt). Leerer Bestand -> kein Settle, [] ."""
    if len(bestand)==0: return [], 0.0
    init,gin,gsg,gKw,gb,N=build(new,bestand)
    init.astype(np.float64).tofile("/tmp/si.bin"); gin.astype(np.int32).tofile("/tmp/sgin.bin")
    gsg.astype(np.float64).tofile("/tmp/sgsg.bin"); gKw.astype(np.float64).tofile("/tmp/sgkw.bin"); gb.astype(np.float64).tofile("/tmp/sgb.bin")
    t=time.perf_counter()
    subprocess.run([GPU,"/tmp/si.bin","/tmp/so.bin",str(3+13*N),str(NT),"0","/tmp/sgin.bin","/tmp/sgsg.bin","/tmp/sgkw.bin","/tmp/sgb.bin"],capture_output=True,text=True)
    wall=time.perf_counter()-t
    st=np.fromfile("/tmp/so.bin",dtype=np.float64).reshape(3+13*N,8)     # Bulk-Readback (1 Transfer)
    flags=[1 if st[3+13*m+12,3]>0 else 0 for m in range(N)]              # Flag-ODER vorbereitet
    return flags, wall

# ---- Die STROM-Schleife: pruefen -> entscheiden -> aufnehmen ----
def strom_lauf(strom, anfangsbestand=None, verbose=False):
    bestand=list(anfangsbestand) if anfangsbestand else []   # = aktive Slots, n_aktiv=len(bestand)
    n_init=len(bestand); prot=[]; twall=0.0; checks=0
    for idx,trip in enumerate(strom):
        assert len(bestand)<MAX_SLOTS, "MAX_SLOTS ueberschritten"
        flags,w=gpu_check(trip,bestand); twall+=w; checks+= (1 if bestand else 0)
        which=[k for k,f in enumerate(flags) if f]
        if which:
            prot.append(("VERWORFEN",trip,which))           # n_aktiv UNVERAENDERT
            if verbose: print(f"    [{idx}] {trip}  VERWORFEN  -> Konflikt mit Slot {which} (={[bestand[k] for k in which]})")
        else:
            slot=len(bestand); bestand.append(trip)          # schreib Slot n_aktiv, n_aktiv++
            prot.append(("AUFGENOMMEN",trip,[]))
            if verbose: print(f"    [{idx}] {trip}  AUFGENOMMEN -> Slot {slot}")
    return bestand,prot,twall,checks,n_init

# ---- Python-Referenz: derselbe Strom, rein sequenziell, NumPy-frei ----
def ref_strom(strom, anfangsbestand=None):
    bestand=list(anfangsbestand) if anfangsbestand else []
    for trip in strom:
        s0,p0,o0=trip
        konflikt=any(s0==s and p0==p and o0!=o for (s,p,o) in bestand)
        if not konflikt: bestand.append(trip)
    return bestand

# ================================================================
print("="*78); print("MINI-OSSIFIKAT Teil 2 — DER STROM (lebender Wissensspeicher)"); print("="*78)

# ---- Szenario A: leerer Start, lesbar. Jeder Konflikt zwingend gegen STREAM-Aufgenommenes (Test 3) ----
print("\nSZENARIO A — leerer Anfangsbestand, lesbar (S:Himmel=1/Gras=0, P:Farbe=1/Zustand=0, O:gruen=1/blau=0)")
labelA=["Himmel-Farbe-blau","Gras-Farbe-gruen","Himmel-Farbe-gruen","Himmel-Zustand-gruen",
        "Gras-Zustand-blau","Gras-Farbe-blau","Himmel-Zustand-blau","Himmel-Farbe-blau(dup)"]
stromA=[(1,1,0),(0,1,1),(1,1,1),(1,0,1),(0,0,0),(0,1,0),(1,0,0),(1,1,0)]
bestA,protA,_,_,_=strom_lauf(stromA,verbose=False)
for (st,trip,which),lab in zip(protA,labelA):
    extra=f"-> Konflikt mit Slot {which}" if which else "-> Slot neu"
    print(f"    {lab:24} {trip}  {st:11} {extra}")
refA=ref_strom(stromA)
print(f"  Endbestand GPU ({len(bestA)} Slots): {bestA}")
print(f"  Referenz       ({len(refA)} Slots): {refA}")
print(f"  -> (1) Bestand waechst: n_aktiv={len(bestA)} | (4) == Referenz: {'JA' if bestA==refA else 'NEIN'}")
verworfen=[p for p in protA if p[2]]
stream_konflikte=all(min(w)>=0 for _,_,w in verworfen)  # alle which-Indizes existieren
print(f"  -> (2) verworfen: {len(verworfen)} Tripel, je gemeldet mit Slot | (3) leerer Start => alle Konflikte gegen STREAM-aufgenommene Slots")

# ---- Szenario B: kleiner Anfangsbestand -> Konflikt gegen INITIAL-Slot ----
print("\nSZENARIO B — Anfangsbestand vorgeladen = [(0,1,1) Gras-Farbe-gruen]")
initB=[(0,1,1)]; stromB=[(0,1,0),(1,1,1),(0,1,1)]   # 1.: Konflikt mit Initial-Slot0; 2.: neu; 3.: Duplikat(konsistent)
bestB,protB,_,_,_=strom_lauf(stromB,anfangsbestand=initB,verbose=True)
refB=ref_strom(stromB,anfangsbestand=initB)
print(f"  Endbestand GPU: {bestB} | Referenz: {refB} | == {'JA' if bestB==refB else 'NEIN'}")
b0_ok = protB[0][0]=="VERWORFEN" and protB[0][2]==[0]
print(f"  -> erstes Tripel kollidiert gegen INITIAL-Slot 0: {'JA' if b0_ok else 'NEIN'} (Gegenstueck zu A: Konflikt gegen anfaenglichen Bestand)")

# ---- Szenario C: Skalierung/Tempo + Endbestand exakt gegen Referenz ----
print("\nSZENARIO C — Zufalls-Strom (Seed-fest), Endbestand exakt gegen Referenz, Tempo")
print(f"  {'n_strom':>8} | {'aufgenommen':>11} | {'verworfen':>9} | {'==Ref':>6} | {'wall (s)':>8} | {'ms/Tripel':>9} | {'ms/Check letzte':>15}")
for nstrom in (50,200,500):
    rng=np.random.default_rng(1000+nstrom)
    strom=[tuple(int(x) for x in rng.integers(0,2,3)) for _ in range(nstrom)]
    t0=time.perf_counter()
    best,prot,twall,checks,_=strom_lauf(strom)
    wall=time.perf_counter()-t0
    ref=ref_strom(strom)
    nauf=len(best); nverw=sum(1 for p in prot if p[2])
    # letzte Check-Zeit grob: ein Check gegen den vollen Bestand
    fl,wlast=gpu_check((1,1,1),best)
    eq = 'JA' if best==ref else 'NEIN'
    print(f"  {nstrom:>8} | {nauf:>11} | {nverw:>9} | {eq:>6} | {wall:>8.2f} | {wall/nstrom*1000:>9.1f} | {wlast*1000:>13.0f} (N={len(best)})")
    assert nauf+nverw==nstrom, "Buchhaltung n_aktiv stimmt nicht"

print("\nCaveat: lebender Wissensspeicher (Strom + wachsender Bestand) ueber die statische Kern-Op (Teil 1) hinaus;")
print("Bestand statisch vorallokiert-konzept (n_aktiv-Zaehler, MAX_SLOTS); jedes Tripel gegen den AKTUELLEN Bestand;")
print("Strom-Schleife host-sequenziell, Pruefung pro Tripel parallel (1 Settle); gegen Python-Referenz; double/fp64; md5-unberuehrt.")

# Leitfaden — GPU-Strang & Mini-Ossifikat

*Praktisches Nachbau-/Fortsetzungs-Dokument für den OpenCL-GPU-Strang der Produktions-Engine.
Begleitend zum Erzähl-Protokoll `GEDANKENSPRUNG_von_LoRA_zum_Anker.md` (Nachträge 48–59).
Stand: 2026-06-21. Produktion `die Produktions-.so` md5 `3a6344c…` durchweg BEWEISBAR UNBERÜHRT.*

---

## 0. Ausgangslage & Zweck

**Problem:** Die ganze Ossifikat-Logik (Tripel, Gleichheit, Suche, Konflikt) war auf der C++-Engine
am **8-Core-Limit** bewiesen — aber 8 Cores reichen nur für ~2 Tripel. Realistisches Ossifikat braucht
viele Tripel → **GPU-Skalierung**.

**Wegwahl:** OpenCL (rohe GPU-Kernel) statt PyTorch — weil hardware-näher und die **Hardware-Tür offen**
hält (rdtsc-/Temp-Kopplung). Bestätigt durch Legacy-Funde (s.u.).

**Engine-Quelle (Referenz, nur lesen):** `der nicht-oeffentlichen Produktions-Engine-Quelle` → `die Produktions-.so`.
**GPU-Artefakte liegen in:** `grounding_check/`.

---

## 1. Das Produktions-Modell (die Wahrheit, gegen die alles verifiziert wird)

Pro Zelle: **S8 = 8 doubles** (zwei Lorenz-Hälften): `x1,y1,z1,w1 + x2,y2,z2,w2`.

**Deriv (pro Hälfte):**
```
dx = σ(y − x)
dy = x(ρ − z) − y + W·w
dz = xy − βz
dw = −w + x·z − W·y
```
**Mean-field-Kopplung** (nur x-Kanäle): `dx1 += K·(mean.x1 − x1)`, `dx2 += K·(mean.x2 − x2)`.
Der `mean` ist ein **Schnappschuss aller Zellen am Tick-Anfang** (separate Variable, NICHT Live-Array)
→ reihenfolge-unabhängig/parallel-korrekt.

**Parameter:** σ=10, ρ=28, β=8/3, DT=0.005, K=0.05,
`W = 0.225 + 0.018·sin(1.8·t_global) + temp·0.001`.

**Integration:** RK4, 4 Stufen, alle mit demselben gefrorenen `mean` und `W`.

**Die ±363-Anker** (w-Flügel-Fixpunkte) sind **emergent**, keine hartkodierte Konstante.
(Im Einzelschritt-W-Testaufbau sitzen sie bei ~±330 — das ist eine Treiber-Konvention, kein Fehler.)

**Logik-Bausteine (C++-seitig, Python-orchestriert über Hooks):**
- Gatter (regenerierend): Deriv-Term `d.w1 += gK·(sign(NET)·363 − w1)`, NET = gewichtete Eingangssumme + bias.
- Anker-Stoß (Bit setzen): Zustands-Schreiben (`perturb_core`).
- READ-Gatter: gerichtete Kopier-Kopplung (1-Eingang-Gatter, non-destruktiv).
- Tripel: 3k-Adressierung (S an 3k, P an 3k+1, O an 3k+2), Carry-Kette.
- ★ **Bauregel (entscheidend):** verkettete Logik braucht die **regenerierende** Gatterform
  `sign(NET)·363` (nicht roh `NET−w1`). Macht die Verkettung **tiefen- und timing-unabhängig**.

---

## 2. Was vorher existierte (Ist-Analyse, keine Doppelarbeit)

- ROCm/PyTorch im Projekt (`~/collect`) trieb **nur die KI-Schicht** (Embeddings, Vision, Monitoring) —
  **nie** die Lorenz-Dynamik. Toolchain aber installiert (`ein Setup-Skript`).
- **Keine** Tensor-/GPU-Lorenz-Dynamik existierte (das einzige Python-Lorenz war ein skalares 3D-Spielzeug).
- **Legacy-OpenCL-Funde** (`ein frueheres privates OpenCL-Projekt`, Feb 2026) als Vorlage:
  - `timecrystal_gpu_simple.cpp` — sauberstes OpenCL-Boilerplate (Context/Queue/Program/Kernel/Buffer,
    GPU→CPU-Fallback, Build-Log-Dump, persistent-kernel-Muster). **Als Gerüst genutzt.**
  - `monolith_v11_sensor.cpp` — das **rdtsc-Hardware-in-the-loop-Muster** (`read_hardware_jitter`).
  - `ein Legacy-OpenCL-Modell` — Doppelkern-Modell, aber CPU-Chaos + exportiert die C-ABI-Signaturen
    (→ ein OpenCL-`.so` wäre drop-in gegen `die ctypes-Bruecke (Python)`).

---

## 3. Die Schritte (Reihenfolge, Befund, Datei)

| # | Schritt | Kernbefund | Artefakte |
|---|---------|-----------|-----------|
| **S1** | Doppelkern-Lorenz **einer** Zelle als OpenCL-Kernel, gegen C++ | **bit-nah**: Einzelschritt 8.88e-16, 1000 Ticks 1e-13. `cl_khr_fp64` (double) **verfügbar auf gfx1100** (RX 7600). w-Flügel-Bistabilität emergiert identisch. | `engine_gpu_verify.cpp`, `verify_gpu.py` |
| **S2** | **mean-field**: N Zellen parallel, Schnappschuss-Mean | bit-nah (6.5e-13), Schnappschuss korrekt (kein Live-Leck), **skaliert bis 4096 Zellen** = Core-Wand für Kapazität gelöst. ABER Speedup latenz-gebunden (Pro-Tick-Host-Readback). | `engine_gpu_mf.cpp`, `verify_gpu_mf.py` |
| **S2.5** | **GPU-Reduktion** (mean auf GPU, Zustand resident) | Latenz-Engpass weg: **70–85k ticks/s** (statt 12–19k), **5.8× @ N=4096**. FP-Assoziativität der Baum-Reduktion geprüft = harmlos (kein Bug). Grenze: single-WG fällt bei N=16384 (multi-WG wäre nächste Opt). | `engine_gpu_red.cpp`, `verify_gpu_red.py` |
| **S3-vor** | **Hardware-Quellen validieren** (Ryzen 7 5800X) | rdtsc **mäßig**: unabhängig (Autokorr.≈0), aber dünn (78% identisch, ~1.4 bit/Aufruf wg. constant_tsc; ~64 Aufrufe für 8 bit). **Temp** (amdgpu 46–47°C / k10temp 43°C) ist die bessere Quelle für den langsamen W-Drift. → Kombination gewählt. | `jitter_probe.cpp` |
| **S3a** | **Temp→W-Drift** koppeln (sanfter Hook `temp·0.001`) | Mulde **hält, vertieft sogar** (Median 327→359; mehr Temp = mehr W = mehr Stabilisierung; strukturell sicher). Shannon gesättigt. Zeit-Integration stabilisiert (1.4×). **Run-Streuung ~1e-4** (Hardware-Varianz sichtbar; entkoppelt = bit-identisch). | `engine_gpu_warr.cpp`, `verify_temp.py` |
| **S3b** | **+ rdtsc-Jitter** in W (volle Kombi) | Jitter **dominiert Entropie um 4 Größenordnungen** (Streuung 1.5e-1 vs Temp 1.3e-5) → Bit-Muster hardware-abhängig, bei sauberen Bits. Mulde sicher bis **j_scale=0.5** (±0.25 auf W). **Preis: 28× Speedup** (64 Aufrufe/Tick host-bound). Regler: j_scale (Stärke), K-Ticks/Thread (Häufigkeit). | `engine_gpu_hw.cpp`, `verify_hw.py` |
| **S5a** | **Gatter-Primitiv** auf GPU (= mean-field-Muster mit Ziel `sign(NET)·363`) | **bit-EXAKT** (0.00e+00 vs C++!) — der ±363-Fixpunkt dämpft FP-Unterschiede vollständig. READ-Gatter + Anker-Stoß fallen ab. **2000 Gatter parallel**, alle korrekt, 43k ticks/s. | `engine_gpu_gate.cpp`, `verify_gate.py` |
| **S5b** | **Verkettete Logik** (XNOR/XOR/Konflikt) auf GPU | tiefen-unabhängig: XNOR/XOR (Tiefe 3) 5.7e-14, **KONFLIKT (Tiefe 4) bit-exakt 0.0** (tiefstes Netz = sauberstes). **Kein Staging nötig** (GPU vereinfacht — die Merker-Krücke aus dem 8-Core-Limit entfällt). 1000 Komparatoren parallel. | `verify_chain.py` |
| **S6-prof** | **Speedup-Profil** Mini-Ossifikat | **NICHT host-sync-bound** (Steuer-Sync <1%, die Nachtrag-55-Sorge widerlegt). GPU-bound auf **Settling**. Hebel = **Parallelisieren** (alle Vergleiche in 1 Settle) → 70–200×, NICHT Sync-Opt. | `profile_ossifikat.cpp` |
| **S6a** | ★★ **Mini-Ossifikat Kern-Op**: 1 neues Tripel gegen N bestehende parallel | 3 Fälle korrekt (konsistent / Konflikt mit i / Trennschärfe), GPU == Referenz. **Skaliert flach auf N=1000** (alle Flags korrekt, 1 Settle, GPU-Settle ~14–49ms). | `verify_ossifikat.py` (+ `engine_gpu_gate.cpp`) |
| **S6b** | ★★★ **Mini-Ossifikat STROM**: lebender Wissensspeicher (Strom aufnehmen, Bestand wächst, Widersprüche verwerfen) | Endbestand **Slot-für-Slot == Referenz** bis 500 Tripel. Prüft gegen den **wachsenden** Bestand (auch früher-im-Strom-Aufgenommenes). Check-Zeit flach (~280–316ms = Subprocess-Boden, Bestand kostet nichts extra). 2 Harness-Bugs ehrlich behoben, kein Engine-Bug. **DIE KRÖNUNG.** | Strom-Skript (+ `engine_gpu_gate.cpp`) |
| **S6c** | **Persistenter Kontext** (Feinschliff): Engine resident, Strom in einer Session statt Subprocess-pro-Check | **17×** schneller: ~300ms → **~17ms/Check** (= reiner Settle, IO 0.1ms). Ergebnis identisch (==Ref), kein State-Leak. **500 Tripel in ~8.8s statt ~150s** — der lebende Speicher läuft flüssig. Schlüssel: Komparator-Config unabhängig von n_aktiv → resident. | `strom_resident.cpp` |
| **S6d** | **Reichere Tripel**: Mehr-Bit-Felder (4 Bit/Feld → 16 Werte je S/P/O = echte Entitäten) | BIT-PROBE exhaustiv **32/32** (O-Sweep: verschieden=mind. ein Bit anders; S-Sweep: gleich braucht alle Bits). Tiefe-5-Verkettung trägt (Regenerations-Bauregel). ★ **Reichtum ist ~GRATIS**: 53 Zellen/Komp statt 13, aber ms/Check unverändert ~17.8ms (Settle ist tick- nicht zell-dominiert) → 16×16×16 kostet wie 2×2×2. Strom ==Ref. | (Mehr-Bit-Variante + `engine_gpu_gate.cpp`) |
| **S6e** | **Settle-Boden senken**: braucht es 2000 Ticks oder schwingt das Netz früher ein? | Flag bit-exakt == 2000-Tick-Wahrheit + stabil **ab 500 Ticks für ALLE Fälle** (Tiefe erzwingt KEINEN höheren Boden: schwere Einzelbit-Fälle = leichte = 500). ★ Disziplin-Fang: NT=100 las zufällig richtig, aber Netz verwaschen (min\|w1\|=0.3) → Stabilitäts-Kriterium verwarf es korrekt. **Default 750** (Flag stabil seit 500 + alle Stufen satt). **2.57×**: ~17.8 → **~7ms/Check**. | `settle_boden.cpp` |
| **S6f** | **Aufräum-Vorfrage** (Bulk-Readback / multi-WG lohnen?): reine Profil-Messung | **NICHTS zu holen** bei realistischem Betrieb (Dutzende–Hunderte Slots). Readback längst gebündelt (0.14ms@100, 2% der Settle-Zeit @16384). multi-WG zielt auf den reduce_mean-single-WG-Engpass, den das Konflikt-Netz nicht hat (run_gate ist schon NDRange/multi-WG). Speedup-Seite **abgeschlossen mit Gewissheit**. ★ Korrektur: Settle ist NICHT flach mit N, sondern ~linear mit Zellzahl NC — wirkte flach weil Stores klein blieben. | `profile_aufraeum.cpp` |

---

## 4. Die Mini-Ossifikat-Architektur (Kern-Operation, S6a)

**Zielbild:** Wissensspeicher mit Konsistenzprüfung. N bestehende Tripel = der Wissensgraph.
Ein neues Tripel (S_neu,P_neu,O_neu) kommt → widerspricht es irgendeinem? (gleiches S+P, anderes O).

**Architektur (eins-gegen-viele, parallel — NICHT N sequenzielle Checks):**
1. **Broadcast:** das neue Tripel als geteilte GPU-Zellen (z.B. 0/1/2), Eingang in ALLE N Komparatoren.
2. **N Konflikt-Komparatoren parallel** (das bit-exakte Tiefe-4-Netz, je ~13 Zellen), jeder vergleicht
   das Neue mit einem bestehenden Tripel i. **Alle N in EINEM Settle** (~2000 Ticks).
3. **Bulk-Readback** aller N Flags in einem Transfer (kein Per-Bit-Read!).
4. **Melde-Logik:** ODER über die N Flags (Host oder GPU-Reduktion) → "konsistent" / "Konflikt mit i".

**Konflikt-Bedingung (das Logik-Herz):** `XNOR(S) UND XNOR(P) UND XOR(O)` = 1 ⟺ Widerspruch.

---

## 5. Architektur-Grenzen (ehrlich, konsistent über den Strang)

- **Substrat rechnet, Steuerung extern** (die durchgehende Grenze): die Dynamik + Gatter laufen auf der
  GPU parallel (Kapazität gelöst), aber die **Orchestrierung** (Tripel-Adressierung, Such-/Programmfluss,
  Strom) bleibt **host-sequenziell**. Das Profil (S6-prof) zeigt: das kostet <1%, ist also KEIN Problem —
  der Hebel ist Parallelisieren der Vergleiche, nicht die Steuerung.
- **Hardware-Kopplung trade-off:** reiche Entropie (Jitter, 28× langsamer) ODER voller Speedup (ohne).
  Regler bewusst wählbar. Temp allein ist fast gratis, aber schwach.
- **Determinismus:** ohne Hardware-Kopplung bit-reproduzierbar; mit Jitter run-to-run verschieden
  (echte physische Varianz, bei intakter Logik).

---

## 6. Stand & offene Fäden

**MINI-OSSIFIKAT STEHT** — die Treppe ist komplett: 1✓2✓3✓4✓ (Logik) · 5✓ (GPU-Portierung) · Speedup✓ ·
6a✓ (Kern-Op) · **6b✓ (Strom = lebender Wissensspeicher, die Krönung).**
Ein lebender Wissensspeicher auf chaos-basiertem, hardware-fähigem Substrat: nimmt einen Strom von Fakten auf,
hält sein eigenes wachsendes Wissen konsistent, meldet+verwirft Widersprüche — exakt gegen Referenz bis 500 Tripel,
konstante Check-Zeit.

**Offene Nebenfäden (alle optional):**
- Bulk-Readback / GPU-Flag-Reduktion (sekundärer Speedup, trivial).
- Persistenter Kontext statt Subprocess (eliminiert den ~300ms-wall-Boden → ganzer Check ~50ms).
- multi-WG-Reduktion (für N≫4096, Settle-Boden senken).
- direkter Zustands-Störterm (riskantere Hardware-Kopplung, mit Sweep-Kalibrierung).
- **dynamische Entropie** (tieferes Maß als Shannon — wie chaotisch die Trajektorie, vertagt).
- Hardware-Treppe Stufe 2/3 (analoge Schaltung / Resonanzraum).
- **Ossifikat-Regeln erweitern** (Retract, Staging/Ratifizierung, mehr Prädikat-Typen) — der Weg vom Mini-
  Ossifikat zum vollen Ossifikat-Funktionsumfang.

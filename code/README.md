# Code — Verifikations- & Demonstrations-Artefakte (GPU-Strang)

Eigenständige Programme, die die Behauptungen des Beweises nachrechnen. Die zugrundeliegende
**Produktions-Engine (das CPU-Substrat) ist nicht Teil dieser Veröffentlichung**; die Programme hier
implementieren die Dynamik (gekoppelte Lorenz-Halbsysteme, RK4, dt=0.005, σ=10/ρ=28/β=8/3) eigenständig
in OpenCL bzw. reinem C++.

## Voraussetzungen
- GPU mit OpenCL-Treiber und **`cl_khr_fp64`** (double precision). Entwickelt/gemessen auf AMD gfx1100.
- `g++` (C++17), Linker-Flag `-lOpenCL` (außer `lyapunov.cpp` = reines C++).
- Python 3 + NumPy (für die `verify_*.py`).

## Bauen
```bash
g++ -O2 <datei>.cpp -o <name> -lOpenCL     # OpenCL-Programme
g++ -O2 lyapunov.cpp -o lyapunov           # reines C++ (kein -lOpenCL)
```

## Übersicht

| Datei | Prüft / demonstriert |
|---|---|
| `lyapunov.cpp` | Kontraktion der ±363-Mulde (λ<0) vs. freies Chaos (λ>0), Benettin-Verfahren. |
| `engine_gpu_verify.cpp` | Einzelzell-Doppelkern-Dynamik (RK4, fp64) auf der GPU. |
| `engine_gpu_mf.cpp` | Mean-Field-Kopplung (Tick-Anfangs-Schnappschuss). |
| `engine_gpu_red.cpp` | Mean-Field mit GPU-Reduktion (resident, kein Pro-Tick-Readback). |
| `engine_gpu_warr.cpp` | Antrieb W aus einer Temperatur-Zeitreihe (Hardware-Kopplung, Teil 1). |
| `engine_gpu_hw.cpp` | Zusätzlicher rdtsc-Jitter auf W (Hardware-Kopplung, Teil 2). |
| `engine_gpu_gate.cpp` | Gatter-Primitiv (`sign(NET)·363`-Regeneration) auf der GPU. |
| `jitter_probe.cpp` | Validierung der rdtsc-Entropiequelle. |
| `strom_resident.cpp` | Lebender Wissensspeicher, 1-Bit-Felder, resident. |
| `strom_richer.cpp` | Lebender Wissensspeicher, Mehr-Bit-Felder (echte Entitäten). |
| `settle_boden.cpp` | Minimale Settle-Tick-Zahl für bit-exakte Konflikt-Flags. |
| `profile_aufraeum.cpp` | Readback-/Settle-Skalierung (lohnen Bulk-Readback / multi-WG?). |
| `profile_ossifikat.cpp` | Host-Steuerungs-Profil (sequenziell vs. parallel). |
| `verify_ossifikat.py` | Eins-gegen-viele Konflikt-Check parallel, gegen Python-Referenz. |
| `verify_strom.py` | Faktenstrom (aufnehmen/verwerfen), gegen Python-Referenz. |

> Die `verify_*.py` hier treiben das `gpu_gate`-Binary (zuerst `engine_gpu_gate.cpp` bauen als `gpu_gate`)
> und vergleichen gegen eine reine Python-Referenz — **keine** Produktions-Engine nötig.

Die Skripte in [`requires-engine/`](requires-engine/) belegen die bit-genaue GPU↔CPU-Gleichheit, brauchen
dafür aber die nicht-veröffentlichte Produktions-Engine.

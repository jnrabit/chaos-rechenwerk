# `requires-engine/` — bit-nahe GPU↔CPU-Verifikation

Diese Skripte belegen den **stärksten** Verifikations-Schritt: dass der OpenCL-Kernel **bit-nah**
(double precision) dieselben Werte liefert wie eine unabhängige C++-Referenz. Dafür laden sie per
`ctypes` eine Referenz-Bibliothek (`libengine_ref*.so`).

**Seit Kurzem lauffähig:** Eine **Clean-Room-Referenz** (`engine_ref.cpp`) ist beigefügt. Sie
implementiert exakt die im BEWEIS / LEITFADEN publizierte Dynamik (Doppelkern-Lorenz, RK4, dt=0.005,
σ=10/ρ=28/β=8/3, W=0.225+0.018·sin(1.8t), Mean-Field K=0.05, Gatter `sign(NET)·363`). Sie ist
**nicht** die nicht-veröffentlichte Produktions-Engine (md5 `3a6344c…`), sondern eine unabhängige
Neuimplementierung desselben Derivs.

**Ehrliche Einordnung:** Der Vergleich belegt die GPU-Kernel damit gegen eine *unabhängige*
C++-Implementierung — bit-nah für die Dynamik (~1e-13 über 1000 Ticks), bit-nah für die
Gatter-Logik (~1e-14, logische Ausgabe exakt korrekt). Die im LEITFADEN notierte bit-*exakte*
(0.0) Übereinstimmung gilt nur gegenüber der eigentlichen Produktions-Engine.

## Bauen & laufen

Aus dem `code/`-Verzeichnis (damit `./gpu_*` und `./libengine_ref*.so` gefunden werden):

```bash
cd code
make verify-requires-engine
```

Das baut `gpu_gate`, `gpu_verify`, `gpu_mf`, `gpu_red` und die drei Referenz-`.so` und führt alle
fünf Skripte aus. Einzeln:

```bash
make requires-engine                       # nur bauen
python3 requires-engine/verify_gpu.py      # Dynamik (Einzelzelle)
python3 requires-engine/verify_gpu_mf.py   # Mean-Field
python3 requires-engine/verify_gpu_red.py  # GPU-Reduktion
python3 requires-engine/verify_gate.py     # Gatter-Primitiv
python3 requires-engine/verify_chain.py    # Verkettete Logik (XNOR/XOR/Konflikt)
```

| Datei | Vergleicht |
|---|---|
| `engine_ref.cpp` | Die Clean-Room-Referenz (exportiert `ref_steps`, `ref_steps_mf`, `ref_gate_steps`). |
| `verify_gpu.py` | GPU-Einzelzell-Kernel ↔ C++-`rk4_step`. |
| `verify_gpu_mf.py` | GPU-Mean-Field ↔ C++-Mean-Field. |
| `verify_gpu_red.py` | GPU-Reduktions-Mean-Field ↔ C++-Serien-Mean. |
| `verify_gate.py` | GPU-Gatter-Primitiv ↔ C++-Gatter. |
| `verify_chain.py` | Verkettete Logik (XNOR/XOR/Konflikt) GPU ↔ C++. |

Die eigenständig lauffähigen Demonstratoren liegen eine Ebene höher in [`../`](../).

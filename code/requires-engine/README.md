# `requires-engine/` — bit-genaue GPU↔CPU-Verifikation

Diese Skripte belegen den **stärksten** Verifikations-Schritt: dass der OpenCL-Kernel **bit-genau** (bzw.
bit-nah, double precision) dieselben Werte liefert wie die C++-Produktions-Engine. Dafür laden sie per
`ctypes` eine Referenz-Bibliothek (`libengine_ref*.so`), die aus der **nicht-veröffentlichten
Produktions-Engine** gebaut wird.

**Sie laufen daher nicht eigenständig** — sie sind hier zur **Transparenz der Methodik** enthalten: sie
zeigen, *wie* die Gleichheit gemessen wurde (Referenz-Hook → identische Schrittfunktion → Vergleich gegen
den GPU-Kernel), auch ohne dass die Referenz-Engine mitveröffentlicht ist.

| Datei | Vergleicht |
|---|---|
| `verify_gpu.py` | GPU-Einzelzell-Kernel ↔ C++-`rk4_step`. |
| `verify_gpu_mf.py` | GPU-Mean-Field ↔ C++-Mean-Field. |
| `verify_gpu_red.py` | GPU-Reduktions-Mean-Field ↔ C++-Referenz. |
| `verify_gate.py` | GPU-Gatter-Primitiv ↔ C++-Gatter. |
| `verify_chain.py` | Verkettete Logik (XNOR/XOR/Konflikt) GPU ↔ C++. |

Die eigenständig lauffähigen Demonstratoren liegen eine Ebene höher in [`../`](../).

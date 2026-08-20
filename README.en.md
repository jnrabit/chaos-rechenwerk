# Computing on a Chaotic Substrate — a Feasibility Proof

*A coupled Lorenz dynamics as a universal, functionally complete, Turing-capable
computing engine, with a measured physical root of stability.*

**Jakob Notter, 2026 — independent research project.**

[Deutsch](README.md) · **English**

[![DOI](https://img.shields.io/badge/DOI-10.5281%2Fzenodo.20829024-blue)](https://doi.org/10.5281/zenodo.20829024)
[![License: CC BY-NC 4.0](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)

> The primary documents are written in German. This README is an English entry point;
> the documents in [`docs/`](docs/) remain in German.

---

## Summary

This project demonstrates that a **chaotic physical substrate** — a field of coupled Lorenz
attractors — can serve as a **full computing engine**: it stores bits, realizes a functionally
complete logic, can be chained into arbitrary switching networks, is Turing-capable, scales to
thousands of parallel units, and can be coupled to real hardware entropy without losing its logic.
The stability everything rests on is not postulated but **measured**: the system's operating point
is demonstrably *contracting* (negative Lyapunov exponent), which explains all correctness
properties as consequences of a single physical fact.

**What this is:** a feasibility proof — evidence that computation on this substrate is *possible in
principle and in full*, end to end from the dynamics to the application, each stage verified against
a reference.

**What this is _not_:** a practical advantage. *Simulated* on conventional hardware, the substrate is
slower than direct logic. The potential benefits show only in *real* analog hardware and only at
scale — both outside the scope of this proof.

---

## Status & Collaboration Note

> **Status & Collaboration Note:** This repository presents a fully verified mathematical & numerical
> proof of concept for universal, bit-exact computing on chaotic substrates. While software simulation
> limits its practical speedup, the underlying principles (contraction via driven Lorenz systems,
> λ = -0.69, regenerative anchors) are specifically designed for physical implementation.
>
> **I am currently not actively pursuing further simulation-based code development.** If you are a
> research group working in **Photonic Computing, Memristor Arrays, or Optomechanical Reservoirs** and
> wish to test this architecture on real analog/physical hardware, feel free to use this code or reach
> out for collaboration.

---

## Guide

| Document | Content |
|---|---|
| [`docs/BEWEIS_Chaos-Rechenwerk.md`](docs/BEWEIS_Chaos-Rechenwerk.md) | **The main document** — the full feasibility proof, from the dynamics to the application. |
| [`docs/LEITFADEN_GPU_Strang.md`](docs/LEITFADEN_GPU_Strang.md) | Technical reference of the GPU strand (porting dynamics + logic to OpenCL, verified step by step). |
| [`docs/GEDANKENSPRUNG_von_LoRA_zum_Anker.md`](docs/GEDANKENSPRUNG_von_LoRA_zum_Anker.md) | Development log — the complete, unvarnished research trajectory including dead ends and corrections. |

Recommended start: **BEWEIS** (the what and why), then **LEITFADEN** (the how of verification).

---

## How to verify

The code in [`code/`](code/) are the **verification and demonstration artifacts of the GPU strand** —
standalone programs that recompute the claims of the proof. The underlying production engine (the CPU
substrate) is **not** part of this publication; the programs here implement the dynamics independently
in OpenCL or pure C++.

**Requirements:** a GPU with an OpenCL driver and **`cl_khr_fp64`** (double precision), `g++`,
Python 3 with NumPy. Developed and measured on an AMD gfx1100.

**Build & run (examples):**

```bash
cd code

# Pure dynamics — Lyapunov well test (no OpenCL needed, pure C++):
g++ -O2 lyapunov.cpp -o lyapunov && ./lyapunov

# Settle-floor measurement (OpenCL):
g++ -O2 settle_boden.cpp -o settle_boden -lOpenCL && ./settle_boden

# Living knowledge store, resident (OpenCL):
g++ -O2 strom_richer.cpp -o strom_richer -lOpenCL && ./strom_richer

# Cleanup / profiling pre-question (OpenCL):
g++ -O2 profile_aufraeum.cpp -o profile_aufraeum -lOpenCL && ./profile_aufraeum
```

**What checks what (selection):**

| File | Checks |
|---|---|
| `lyapunov.cpp` | The ±363 well is *contracting* (λ<0) vs. free Lorenz chaos (λ>0) — the physical root of stability. |
| `engine_gpu_verify.cpp` | Single-cell dual-core dynamics (RK4, fp64) on the GPU. |
| `engine_gpu_mf.cpp` / `engine_gpu_red.cpp` | Mean-field coupling on the GPU (snapshot; GPU reduction). |
| `engine_gpu_gate.cpp` | The gate primitive (logic) on the GPU. |
| `engine_gpu_warr.cpp` / `engine_gpu_hw.cpp` | Hardware coupling (temperature / rdtsc jitter) into the drive W. |
| `strom_resident.cpp` / `strom_richer.cpp` | The "living" knowledge store: a fact stream that stays consistent and reports contradictions (1-bit / multi-bit fields). |
| `settle_boden.cpp` | From which tick count the conflict net settles bit-exactly. |
| `profile_aufraeum.cpp` / `profile_ossifikat.cpp` | Profiling (readback / settle scaling; host control). |
| `verify_strom.py` / `verify_ossifikat.py` | Drive the `gpu_gate` binary; compare the result against a pure Python reference. |

The scripts in [`code/requires-engine/`](code/requires-engine/) establish the **bit-exact** agreement
GPU ↔ CPU, but for that they require the non-published production engine — see the README there.

---

## License

[Creative Commons **CC BY-NC 4.0**](LICENSE) — use, sharing, and building upon are permitted **with
attribution**, but **non-commercially**.

**For commercial use, please get in touch** (via the author's profile).

## Citation

See [`CITATION.cff`](CITATION.cff). DOI: [10.5281/zenodo.20829024](https://doi.org/10.5281/zenodo.20829024).

# Rechnen auf einem chaotischen Substrat — ein Machbarkeitsbeweis

*Eine gekoppelte Lorenz-Dynamik als universelles, funktional vollständiges, Turing-fähiges
Rechenwerk, mit gemessener physikalischer Stabilitäts-Wurzel.*

**Jakob Notter, 2026 — eigenständiges Forschungsprojekt.**

**Deutsch** · [English](README.en.md)

[![DOI](https://img.shields.io/badge/DOI-10.5281%2Fzenodo.20829024-blue)](https://doi.org/10.5281/zenodo.20829024)
[![License: CC BY-NC 4.0](https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey)](LICENSE)

---

## Zusammenfassung

Dieses Projekt zeigt, dass ein **chaotisches physikalisches Substrat** — ein Feld gekoppelter
Lorenz-Attraktoren — als **vollwertiges Rechenwerk** dienen kann: es speichert Bits, realisiert eine
funktional vollständige Logik, lässt sich zu beliebigen Schaltnetzen verketten, ist Turing-fähig,
skaliert auf tausende parallele Einheiten und lässt sich an echte Hardware-Entropie koppeln, ohne seine
Logik zu verlieren. Die Stabilität, auf der alles ruht, ist nicht postuliert, sondern **gemessen**: der
Arbeitspunkt des Systems ist nachweislich *kontrahierend* (negativer Lyapunov-Exponent), was sämtliche
Korrektheits-Eigenschaften als Konsequenzen einer einzigen physikalischen Eigenschaft erklärt.

**Was dies ist:** ein Machbarkeitsbeweis — der Nachweis, dass Berechnung auf diesem Substrat *prinzipiell
und vollständig* möglich ist, durchgehend von der Dynamik bis zur Anwendung, jede Stufe gegen eine
Referenz verifiziert.

**Was dies _nicht_ ist:** ein praktischer Vorteil. Auf konventioneller Hardware *simuliert*, ist das
Substrat langsamer als direkte Logik. Die potenziellen Vorteile zeigen sich erst in *echter* analoger
Hardware und erst bei Skalierung — beides außerhalb des Rahmens dieses Beweises.

---

## Status- & Kollaborations-Hinweis

> **Status- & Kollaborations-Hinweis:** Dieses Repository präsentiert einen vollständig verifizierten
> mathematischen & numerischen Machbarkeitsbeweis für universelles, bit-exaktes Rechnen auf chaotischen
> Substraten. Während die Software-Simulation den praktischen Geschwindigkeitsgewinn begrenzt, sind die
> zugrunde liegenden Prinzipien (Kontraktion durch getriebene Lorenz-Systeme, λ = −0,69, regenerative
> Anker) gezielt für die physikalische Implementierung konzipiert.
>
> **Ich verfolge derzeit keine aktive Weiterentwicklung des simulationsbasierten Codes.** Falls Sie eine
> Forschungsgruppe in den Bereichen **Photonisches Rechnen, Memristor-Arrays oder optomechanische
> Reservoirs** sind und diese Architektur auf echter analoger/physischer Hardware testen möchten, können
> Sie diesen Code gerne nutzen oder für eine Zusammenarbeit Kontakt aufnehmen.

---

## Wegweiser

| Dokument | Inhalt |
|---|---|
| [`docs/BEWEIS_Chaos-Rechenwerk.md`](docs/BEWEIS_Chaos-Rechenwerk.md) | **Das Hauptdokument** — der vollständige Machbarkeitsbeweis von der Dynamik bis zur Anwendung. |
| [`docs/LEITFADEN_GPU_Strang.md`](docs/LEITFADEN_GPU_Strang.md) | Technische Referenz des GPU-Strangs (Portierung der Dynamik + Logik auf OpenCL, Schritt für Schritt verifiziert). |
| [`docs/GEDANKENSPRUNG_von_LoRA_zum_Anker.md`](docs/GEDANKENSPRUNG_von_LoRA_zum_Anker.md) | Entstehungsprotokoll — der vollständige, ungeschönte Forschungs-Verlauf inkl. Sackgassen und Korrekturen. |

Empfohlener Einstieg: **BEWEIS** (das Was und Warum), dann **LEITFADEN** (das Wie der Verifikation).

---

## Verifikation ("How to verify")

Der Code in [`code/`](code/) sind die **Verifikations- und Demonstrations-Artefakte des GPU-Strangs** —
eigenständige Programme, die die Behauptungen des Beweises nachrechnen. Die zugrundeliegende
Produktions-Engine (das CPU-Substrat) ist **nicht** Teil dieser Veröffentlichung; die hier enthaltenen
Programme implementieren die Dynamik eigenständig in OpenCL bzw. reinem C++.

**Voraussetzungen:** eine GPU mit OpenCL-Treiber und **`cl_khr_fp64`** (double precision), `g++`,
Python 3 mit NumPy. Entwickelt/gemessen auf AMD gfx1100.

**Bauen & laufen (Beispiele):**

```bash
cd code

# Reine Dynamik — Lyapunov-Mulde-Test (kein OpenCL nötig, reines C++):
g++ -O2 lyapunov.cpp -o lyapunov && ./lyapunov

# Settle-Boden-Messung (OpenCL):
g++ -O2 settle_boden.cpp -o settle_boden -lOpenCL && ./settle_boden

# Lebender Wissensspeicher, resident (OpenCL):
g++ -O2 strom_richer.cpp -o strom_richer -lOpenCL && ./strom_richer

# Aufräum-/Profil-Vorfrage (OpenCL):
g++ -O2 profile_aufraeum.cpp -o profile_aufraeum -lOpenCL && ./profile_aufraeum
```

**Was prüft was (Auswahl):**

| Datei | Prüft |
|---|---|
| `lyapunov.cpp` | Die ±363-Mulde ist *kontrahierend* (λ<0) vs. freies Lorenz-Chaos (λ>0) — die physikalische Stabilitäts-Wurzel. |
| `engine_gpu_verify.cpp` | Einzelzell-Doppelkern-Dynamik (RK4, fp64) auf der GPU. |
| `engine_gpu_mf.cpp` / `engine_gpu_red.cpp` | Mean-Field-Kopplung auf der GPU (Schnappschuss; GPU-Reduktion). |
| `engine_gpu_gate.cpp` | Das Gatter-Primitiv (Logik) auf der GPU. |
| `engine_gpu_warr.cpp` / `engine_gpu_hw.cpp` | Hardware-Kopplung (Temperatur / rdtsc-Jitter) auf den Antrieb W. |
| `strom_resident.cpp` / `strom_richer.cpp` | Der "lebende" Wissensspeicher: ein Faktenstrom, der konsistent bleibt und Widersprüche meldet (1-Bit- bzw. Mehr-Bit-Felder). |
| `settle_boden.cpp` | Ab welcher Tick-Zahl das Konflikt-Netz bit-exakt einschwingt. |
| `profile_aufraeum.cpp` / `profile_ossifikat.cpp` | Profiling (Readback-/Settle-Skalierung; Host-Steuerung). |
| `verify_strom.py` / `verify_ossifikat.py` | Treiben das `gpu_gate`-Binary; vergleichen das Ergebnis gegen eine reine Python-Referenz. |

Die Skripte in [`code/requires-engine/`](code/requires-engine/) belegen die **bit-genaue** Übereinstimmung
GPU ↔ CPU, benötigen dafür aber die nicht-veröffentlichte Produktions-Engine — siehe die README dort.

---

## Lizenz

[Creative Commons **CC BY-NC 4.0**](LICENSE) — Nutzung, Weitergabe und Aufbau erlaubt **mit
Namensnennung**, aber **nicht-kommerziell**.

**Für kommerzielle Nutzung bitte Kontakt aufnehmen** (über das Profil des Autors).

## Zitieren

Siehe [`CITATION.cff`](CITATION.cff). DOI: [10.5281/zenodo.20829024](https://doi.org/10.5281/zenodo.20829024).

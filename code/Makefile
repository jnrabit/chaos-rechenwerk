# Makefile — Chaos-Rechenwerk, GPU-Strang (Verifikations- & Demonstrations-Artefakte)
#
#   make                        baut alles (reines C++ + OpenCL-Programme + Referenz-.so)
#   make lyapunov               nur der reine C++-Test (kein OpenCL nötig)
#   make gpu_gate               das Gatter-Binary, das die eigenständigen verify_*.py treiben
#   make verify                 baut gpu_gate + läuft die eigenständigen Verifikationen
#   make verify-requires-engine baut die GPU↔CPU-Verifikation (Clean-Room-Referenz) und läuft sie
#   make clean                  entfernt alle gebauten Artefakte

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++17
OPENCL_LIBS = -lOpenCL

# Reines C++ (kein OpenCL)
PURE = lyapunov

# OpenCL-Programme (je unter ihrem eigenen Namen gebaut)
OCL = engine_gpu_verify engine_gpu_mf engine_gpu_red engine_gpu_warr \
      engine_gpu_hw jitter_probe settle_boden strom_resident strom_richer \
      profile_aufraeum profile_ossifikat

# Kurznamen, die die verify_*.py-Skripte erwarten (gebaut aus engine_gpu_*.cpp)
ALIASES = gpu_gate gpu_verify gpu_mf gpu_red

# Clean-Room-Referenzbibliotheken (C-ABI), die die requires-engine-Skripte laden
REF_LIBS = libengine_ref.so libengine_ref_mf.so libengine_ref_gate.so

BINS = $(PURE) $(OCL) $(ALIASES)

.PHONY: all verify verify-strom verify-ossifikat verify-strom-regeln requires-engine verify-requires-engine clean

all: $(BINS) $(REF_LIBS)

lyapunov: lyapunov.cpp
	$(CXX) $(CXXFLAGS) $< -o $@

$(OCL): %: %.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCL_LIBS)

gpu_gate: engine_gpu_gate.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCL_LIBS)

gpu_verify: engine_gpu_verify.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCL_LIBS)

gpu_mf: engine_gpu_mf.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCL_LIBS)

gpu_red: engine_gpu_red.cpp
	$(CXX) $(CXXFLAGS) $< -o $@ $(OPENCL_LIBS)

# Alle drei .so enthalten dieselben drei Symbole (ref_steps, ref_steps_mf, ref_gate_steps);
# die Skripte laden je nach Bedarf eine der drei Dateien.
$(REF_LIBS): requires-engine/engine_ref.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC $< -o $@

requires-engine: gpu_gate gpu_verify gpu_mf gpu_red $(REF_LIBS)

verify-strom: gpu_gate
	python3 verify_strom.py

verify-ossifikat: gpu_gate
	python3 verify_ossifikat.py

verify-strom-regeln: gpu_gate
	python3 verify_strom_regeln.py

# Aus dem code/-Verzeichnis laufen lassen, damit ./gpu_* und ./libengine_ref*.so gefunden werden.
verify-requires-engine: requires-engine
	python3 requires-engine/verify_gpu.py
	python3 requires-engine/verify_gpu_mf.py
	python3 requires-engine/verify_gpu_red.py
	python3 requires-engine/verify_gate.py
	python3 requires-engine/verify_chain.py

verify: verify-strom verify-ossifikat

clean:
	rm -f $(BINS) $(REF_LIBS)

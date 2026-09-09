# Convenience-Proxies für `make` im code/-Verzeichnis (Build-System siehe code/Makefile).
#
#   make                        baut alle Verifikations-/Demo-Programme
#   make verify                 baut gpu_gate und läuft die eigenständigen Verifikationen
#   make verify-requires-engine baut die GPU↔CPU-Verifikation (Clean-Room-Referenz) und läuft sie
#   make clean                  entfernt alle gebauten Artefakte

.PHONY: all verify verify-requires-engine verify-strom-regeln clean

all:
	$(MAKE) -C code all

verify:
	$(MAKE) -C code verify

verify-requires-engine:
	$(MAKE) -C code verify-requires-engine

verify-strom-regeln:
	$(MAKE) -C code verify-strom-regeln

clean:
	$(MAKE) -C code clean

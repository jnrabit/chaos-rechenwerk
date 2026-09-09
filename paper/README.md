# Paper (arXiv)

Skelett für die arXiv-Einreichung. Die Inhalte stammen aus `docs/BEWEIS_Chaos-Rechenwerk.md`
(das Was/Warum) und `docs/LEITFADEN_GPU_Strang.md` (die Messzahlen).

## Bauen

```bash
pdflatex main && bibtex main && pdflatex main && pdflatex main
# oder: latexmk -pdf main
```

## Was noch zu tun ist (TODO-Marker in `main.tex`)

- **Abbildungen** aus dem Repo (`files (1)/*.png`) als PDF einbetten: Flügel/Bistabilität
  (`a1_ring_epizentrum.png`, `a1b_halten.png`), Lyapunov-Kontrast, Konflikt-Netz.
- **Einleitung** mit Forschungsstand (Lorenz 1963, Reservoir Computing, Chaos Computing,
  Memristor/Photonik als Ziel-Hardware) ausbauen.
- **Literatur** ergänzen (Lorenz, Reservoir/Chaos-Computing-Klassiker).
- Messzahlen aus `LEITFADEN` §3 einpflegen (70–85k ticks/s, 5.8× @ N=4096, ~7 ms/Check).

## Einreich-Hinweise

- **Kategorien:** primär `cs.ET` (Emerging Technologies), sekundär `nlin.CD` (Chaotic Dynamics).
- **Lizenz:** Paper unter arXiv-Standardlizenz; der Code bleibt **CC BY-NC 4.0** (separat im
  Repo). Beides ist sauber trennbar.
- **DOI/Zenodo** und `CITATION.cff` (im Repo-Wurzel) referenzieren.

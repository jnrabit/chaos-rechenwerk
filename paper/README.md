# Paper (arXiv)

Das arXiv-Papier liegt in `main.tex` (Literatur inline via `thebibliography`, kein `.bib`).
Die Inhalte stammen aus `docs/BEWEIS_Chaos-Rechenwerk.md` (das Was/Warum) und
`docs/LEITFADEN_GPU_Strang.md` (die Messzahlen).

## Bauen

```bash
python3 make_figures.py .     # erzeugt fig_bistabilitaet.pdf, fig_lyapunov.pdf
pdflatex main && pdflatex main
# oder: latexmk -pdf main
```

## Was schon drin ist

- Titel, Abstract, Einleitung (mit Reservoir-/Chaos-Computing-Einordnung), alle Abschnitte
  spiegelbildlich zum BEWEIS, Messzahlen aus dem LEITFADEN, Literatur (Lorenz, Jaeger,
  Maass et al., Munakata et al., Ditto et al., Benettin et al., Pesin).
- Zwei Abbildungen, direkt aus der publizierten Dynamik erzeugt (`make_figures.py`):
  `fig_bistabilitaet.pdf` (Speicher/Flip) und `fig_lyapunov.pdf` (Kontraktion vs. Chaos,
  volles Spektrum + $h_{KS}$).

## Was noch offen ist (freiwillig)

- Ein Gatter/Schaltnetz-**Schema** (z. B. TikZ: Konflikt-Komparator XNOR·XNOR·XOR → AND3).
- Ggf. eine Abbildung zur Hardware-Kopplung (Run-to-run-Nichtdeterminismus).
- Feinschliff der Prosa je Zielpublikum.

## Einreich-Hinweise

- **Kategorien:** primär `cs.ET` (Emerging Technologies), sekundär `nlin.CD` (Chaotic Dynamics).
- **Lizenz:** Paper unter arXiv-Standardlizenz; der Code bleibt **CC BY-NC 4.0** (separat im
  Repo). Beides ist sauber trennbar.
- **DOI/Zenodo** und `CITATION.cff` (im Repo-Wurzel) referenzieren.

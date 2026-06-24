# Rechnen auf einem chaotischen Substrat — ein Machbarkeitsbeweis

*Eine gekoppelte Lorenz-Dynamik als universelles, funktional vollständiges, Turing-fähiges Rechenwerk,
mit gemessener physikalischer Stabilitäts-Wurzel.*

Jakob Notter (jnrabit), 2026. Eigenständiges Forschungsprojekt.
Begleitende Arbeitsdokumente: `GEDANKENSPRUNG_von_LoRA_zum_Anker.md` (Entstehungsprotokoll),
`LEITFADEN_GPU_Strang.md` (technische Referenz).

---

## Zusammenfassung

Dieses Dokument zeigt, dass ein **chaotisches physikalisches Substrat** — ein Feld gekoppelter
Lorenz-Attraktoren — als **vollwertiges Rechenwerk** dienen kann: es speichert Bits, realisiert eine
funktional vollständige Logik, lässt sich zu beliebigen Schaltnetzen verketten, ist Turing-fähig, skaliert
auf tausende parallele Einheiten und lässt sich an echte Hardware-Entropie koppeln, ohne seine Logik zu
verlieren. Die Stabilität, auf der alles ruht, ist nicht postuliert, sondern **gemessen**: der Arbeitspunkt
des Systems ist nachweislich *kontrahierend* (negativer Lyapunov-Exponent), was sämtliche
Korrektheits-Eigenschaften als Konsequenzen einer einzigen physikalischen Eigenschaft erklärt.

**Was dies ist:** ein Machbarkeitsbeweis — der Nachweis, dass Berechnung auf diesem Substrat *prinzipiell
und vollständig* möglich ist, durchgehend von der Dynamik bis zur Anwendung, jede Stufe gegen eine
Referenz verifiziert.

**Was dies nicht ist:** ein praktischer Vorteil. Auf konventioneller Hardware *simuliert*, ist das Substrat
langsamer als direkte Logik. Die potenziellen Vorteile (siehe §7) zeigen sich erst in *echter* analoger
Hardware und erst bei Skalierung — beides außerhalb des Rahmens dieses Beweises.

---

## 1. Das Substrat

Eine Recheneinheit („Zelle") besteht aus zwei gekoppelten Lorenz-Halbsystemen, zusammen 8 Zustandsgrößen
(`x1,y1,z1,w1` und `x2,y2,z2,w2`). Die Dynamik pro Halbsystem:

```
dx = σ(y − x)
dy = x(ρ − z) − y + W·w
dz = xy − βz
dw = −w + x·z − W·y
```

Parameter: σ=10, ρ=28, β=8/3 (das klassische chaotische Lorenz-Regime), Integration per RK4, dt=0.005.
Zellen koppeln über ein *Mean-Field* auf den x-Kanälen (`dx += K·(x̄ − x)`, K=0.05), wobei x̄ ein
**Schnappschuss** aller Zellen zu Tick-Beginn ist — dadurch ist die Kopplung reihenfolge-unabhängig und
exakt parallelisierbar.

Der Term `W` ist der **Antrieb** (`W = 0.225 + 0.018·sin(1.8t) + …`). Er ist der Schlüssel: bei diesem
Arbeitspunkt bildet die `w`-Komponente **zwei stabile Pole** bei etwa ±363 aus — ein bistabiler
Schalter. Diese Pole sind *emergent*, nicht einkodiert.

---

## 2. Bit und Speicher

Die beiden ±363-Pole („Flügel") dienen als **Bit**: +363 ist die eine Polarität, −363 die andere. Ein
kurzer Zustands-Stoß setzt das Bit (kippt die Zelle in den gewünschten Flügel), wo es danach von selbst
**festgehalten** wird. Das ist ein nicht-flüchtiger Ein-Bit-Speicher aus reiner Dynamik.

Entscheidend ist die Stabilität: einmal gesetzt, bleibt das Bit satt und sauber, auch über tausende
Integrationsschritte. Die Ursache dieser Stabilität wird in §6 gemessen.

---

## 3. Funktional vollständige Logik

Ein **Gatter** ist ein Rückkopplungsterm, der die `w`-Komponente einer Zelle auf einen *regenerierten*
Zielwert zieht:

```
dw += gK·( sign(NET)·363 − w )
```

`NET` ist die gewichtete Summe der Gatter-Eingänge (plus Bias). Der Ausgang wird also nicht roh
durchgereicht, sondern auf den **sauberen ±363-Anker regeneriert** — das Vorzeichen von `NET` entscheidet
die Polarität. Mit geeigneten Gewichten/Bias realisiert dieser eine Term AND, OR, NAND usw.; ein
READ-Gatter (ein Eingang, gerichtet, nicht-destruktiv) kopiert ein Bit.

**Die Regenerations-Bauregel.** Dass der Ausgang auf den festen Anker `sign(NET)·363` regeneriert (statt
roh `NET`), ist die zentrale Konstruktionsentscheidung des gesamten Werks. Sie macht Gatter
*verkettbar*: weil jeder Ausgang wieder ein voll-sattes ±363-Bit ist, kann er bedenkenlos Eingang des
nächsten Gatters sein. Ohne Regeneration zerfällt die Signalqualität über die Verkettungstiefe; mit ihr
nicht.

Diese eine Regel trägt im weiteren Verlauf an drei voneinander unabhängigen Stellen (§4, §5, §6) — das
Kennzeichen eines tragfähigen Prinzips.

---

## 4. Verkettung — beliebige Schaltnetze

Aus den Grundgattern werden mehrstufige Netze gebaut, indem Zwischenergebnisse als eigene Zellen gehalten
und als Eingänge der nächsten Stufe gelesen werden. Verifiziert wurden u.a. XNOR und XOR (je dreistufig,
`OR→NAND→AND`-Verkettung) und eine vierstufige Konflikt-Bedingung mit dreifachem UND.

**Tiefen-Unabhängigkeit (gemessen).** Über die Verkettungstiefe akkumuliert sich *kein* Fehler — im
Gegenteil: das tiefste geprüfte Netz war das *sauberste* (bit-exakte Übereinstimmung mit der Referenz).
Grund: jede Stufe regeneriert auf den ±363-Fixpunkt, sodass Abweichungen an jeder Stufe neu ausgelöscht
statt weitergetragen werden. Damit sind beliebige kombinatorische Schaltnetze realisierbar — das System
ist **funktional vollständig**.

**Turing-Fähigkeit.** In der Entwicklung (auf dem Vorläufer-Substrat) wurden zusätzlich ein
Impuls-getakteter Programmzähler, ein speicherndes Steuerwerk und bedingte Verzweigung gezeigt — zusammen
mit unbegrenzt erweiterbarem Speicher die Bausteine universeller Berechnung. Eine architektonische Grenze
ist dabei sauber benannt: das Substrat kann sich *nicht selbst takten* (Zustandswechsel über die
Separatrix brauchen eine externe Zustands-Injektion); der Takt ist extern. Das Substrat *rechnet und
hält*, die *Steuerung* ist extern — eine Eigenschaft, die durch das ganze Werk konsistent bleibt.

---

## 5. Skalierung auf paralleler Hardware

Die Dynamik wurde von einer 8-Kern-Referenzimplementierung (C++) auf einen **GPU-Datenpfad** (OpenCL,
doppelte Genauigkeit) portiert und gestaffelt verifiziert:

- **Dynamik**: ein Doppelkern-Lorenz-Schritt stimmt bit-nah mit der Referenz (≈1e-13 über 1000 Schritte);
  das Mean-Field skaliert auf tausende Zellen; eine GPU-seitige Reduktion beseitigt den Latenz-Engpass.
- **Logik**: das Gatter-Primitiv (strukturell identisch zur Mean-Field-Kopplung, nur mit rekonstruiertem
  Ziel) läuft **bit-exakt** auf der GPU — exakter noch als die Dynamik, weil der ±363-Fixpunkt die
  Rundungsunterschiede zwischen CPU und GPU vollständig wegzieht (die Regenerations-Bauregel, zum zweiten
  Mal tragend). Tausende Gatter laufen parallel in einem Kernel-Aufruf.
- **Verkettung**: mehrstufige Netze laufen auf der GPU *ohne* die sequenzielle Zwischenspeicher-Strategie,
  die das 8-Kern-Limit erzwungen hatte — die Kapazität der GPU *vereinfacht* die Logik.

Damit ist die Kapazitätsgrenze der Referenz (wenige Zellen) aufgehoben: tausende Zellen und Gatter rechnen
gleichzeitig.

---

## 6. Die physikalische Wurzel: gemessene Kontraktion

Alle Korrektheits-Eigenschaften (stabiler Speicher, bit-exakte Gatter, tiefen-unabhängige Verkettung,
Robustheit gegen Störung) lassen sich auf **eine** messbare Eigenschaft zurückführen.

Per Benettin-Verfahren (zwei minimal getrennte Trajektorien, δ₀=1e-9, mit Renormierung gegen Sättigung)
wurde der größte Lyapunov-Exponent λ bestimmt:

| Zustand | λ_max | Bedeutung |
|---|---|---|
| Am Arbeitspunkt, im Flügel | **−0.69** | kontrahierend (Störungen klingen ab) |
| Im Flügel, mit Gatter-Term | **−1.23** | stärker kontrahierend |
| Reines Lorenz (Antrieb W=0) | **+0.91** | divergierend (klassisches Chaos) |

**Ergebnis:** Der Arbeitspunkt ist **kontrahierend** (λ<0), während dasselbe System ohne Antrieb chaotisch
divergiert (λ>0). Der Antrieb `W` verwandelt chaotische Divergenz in eine Mulde, die Störungen *aktiv
auslöscht*. Die Dämpfungs-Zeitskala 1/|λ| ≈ 160–290 Schritte erklärt quantitativ, warum ein gemessener
Einschwing-Boden von ~750 Schritten für korrekte Logik ausreicht (mehrere Dämpfungs-Zeitskalen) — zwei
unabhängig gewonnene Zahlen, die übereinstimmen.

Ein vertiefender Befund: schon ein *generischer* Startzustand fällt am Arbeitspunkt von selbst in die
kontrahierende Mulde. Die Stabilität ist also keine lokale Eigenschaft der Anker, sondern eine **globale
Eigenschaft des Betriebspunkts** — freies Chaos existiert nur, wenn der Antrieb ganz entfällt.

Damit ist die Regenerations-Bauregel auch physikalisch verstanden: der Gatter-Term *verstärkt* die ohnehin
vorhandene Kontraktion (−0.69 → −1.23). Bit-Exaktheit, Tiefen-Unabhängigkeit und Störungs-Robustheit sind
keine drei Zufälle, sondern Konsequenzen dieser einen kontrahierenden Dynamik.

---

## 7. Kopplung an echte Physik

Das Substrat wurde an reale Hardware-Entropie der ausführenden Maschine gekoppelt: die GPU-Temperatur
(langsamer Drift, eine Rückkopplung der vom Rechnen erzeugten Wärme in die eigene Dynamik) und
CPU-Zyklus-Jitter (schnelle Mikrovarianz). Ergebnis:

- Die Logik **bleibt intakt** — die Mulde hält, die Bits bleiben sauber. Strukturell folgt das aus §6:
  der Antrieb wirkt stabilisierend, zusätzlicher Antrieb vertieft die Mulde, statt sie zu zerstören.
- Das System wird dabei **nicht-deterministisch**: wiederholte Läufe unterscheiden sich messbar (echte
  physische Varianz fließt in die Zustände), während entkoppelte Läufe bit-identisch bleiben.

Das ist die einzige Stelle, an der das Substrat etwas tut, das digitale Logik nicht von selbst tut:
physisches Rauschen *im Rechnen* tragen, statt es separat zu erzeugen.

---

## 8. Tragfähigkeit an einer realen Anwendung (Illustration)

Als nicht-triviales Testobjekt wurde die Kern-Operation eines Wissensspeichers mit Konsistenzprüfung auf
dem Substrat realisiert: ein neues Fakten-Tripel wird gegen einen Bestand bestehender Tripel auf
Widerspruch geprüft (gleiches Subjekt+Prädikat, anderes Objekt), parallel, in einem Durchlauf; konsistente
werden aufgenommen, widersprüchliche gemeldet. Verifiziert gegen eine unabhängige Referenz bis zu mehreren
hundert Tripeln, mit mehr-bit-kodierten Entitäten.

Diese Anwendung *illustriert* die Tragfähigkeit (das Substrat führt ein echtes Programm aus), begründet das
Substrat aber **nicht** — dieselbe Anwendung läuft auf konventioneller Hardware ebenso. Ihr Wert hier ist
allein der Nachweis: die gezeigte Logik trägt eine reale, mehrstufige, datenabhängige Aufgabe.

---

## 9. Reichweite und Grenze (ehrlich)

**Bewiesen:** Ein chaotisches Lorenz-Substrat kann universell rechnen — Speicher, funktional vollständige
Logik, beliebige Schaltnetze, Turing-Fähigkeit, parallele Skalierung, Kopplung an echte Entropie — und die
Stabilität, auf der das ruht, ist als kontrahierende Dynamik *gemessen*, nicht angenommen. Jede Stufe ist
gegen eine Referenz verifiziert.

**Nicht bewiesen, und außerhalb der Reichweite dieses Beweises:** ein *praktischer Vorteil*. Drei
plausible Kandidaten existieren — (a) echte Entropie im Rechnen für Krypto/stochastische Verfahren,
(b) intrinsische Fehlerkorrektur durch die kontrahierende Mulde, (c) Rechnen und Speichern am selben Ort
(in-memory). Alle drei sind **in Simulation nicht einlösbar** (simuliertes Chaos auf konventioneller
Hardware hat keinen dieser Vorteile real) und würden sich erst in *echter analoger Hardware* und erst bei
*Skalierung* zeigen. Diese Stufe ist hier weder gebaut noch behauptet.

Der Status dieses Werks ist damit der eines **Machbarkeitsbeweises**: der Nachweis, dass der Weg gangbar
ist, vollständig und sauber belegt — die Eintrittskarte für eine physische Stufe, die ein praktischer
Vorteil voraussetzen würde. Ob ein solcher Vorteil real entsteht, ist eine offene, empirische Frage, deren
Beantwortung eine Hardware-Umsetzung erfordert.

---

## 10. Reproduzierbarkeit

Alle Messungen wurden gegen eine unveränderte Referenz-Implementierung geführt (deren Integrität per
Hash vor und nach jeder Messung geprüft wurde). Die GPU-Portierung verwendet denselben Kernel-Quelltext
für Verifikation und Anwendung. Die Beweiskette ist in `GEDANKENSPRUNG_von_LoRA_zum_Anker.md` Schritt für
Schritt protokolliert; die technischen Parameter, Dateinamen und Messzahlen in `LEITFADEN_GPU_Strang.md`.

Methodisches Prinzip durchgehend: jede Annahme wird gemessen, bevor sie geglaubt wird; die Quelle/Annahme
wird vor ihrer Nutzung validiert; ein Negativbefund gilt; dem eigenen Maß wird misstraut (mehrfach wurden
vermutete Engpässe oder Effekte gemessen-widerlegt, und Auswertungsfehler offen korrigiert).

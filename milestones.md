# NexDB — Milestones (drum spre pachetele npm & python)

> Scop: rulare **local-only**, apoi client libraries pentru **npm** și **PyPI**.
> Principiu-cheie: un client library e un **wrapper subțire peste wire protocol**.
> Deci prioritatea o dau lucrurile care schimbă **contractul de pe fir** — dacă le
> schimbi *după* ce ai publicat pachetele, spargi toți clienții.

---

## 🔴 P0 — Blocante (trebuie ÎNAINTE de pachete)

Afectează formatul de pe fir. Le rezolvi acum, o singură dată, ca să nu faci
breaking changes în pachete mai târziu.

- [x] **Coliziune delimitator `|` în rezultate** ✅ *(rezolvat — JSON line-delimited)*
  `NetworkServer::executeQuery` serializează acum răspunsurile ca **o linie JSON**:
  `{"type":"ok"}`, `{"type":"rows","rows":[[...],...]}`, `{"type":"switch","db":...}`,
  `{"type":"error","message":...}`. Valorile trec prin `jsonEscape`, deci `|`, `\n` și
  literalul `END` nu mai sparg parsarea. Teste noi în `tests/test_network.cpp` (12, 13).
  Rămâne de adăugat `"columns"` (item separat mai jos) și versionarea.

- [x] **Lipsesc numele coloanelor în răspuns** ✅ *(rezolvat)*
  `Engine::executeSelect` reține numele coloanelor (schema pentru `SELECT *`, coloanele
  proiectate altfel) în `lastColumns`, expus prin `Engine::getLastColumns()` și golit la
  începutul fiecărui `query()`. Ambii consumatori citesc din același getter:
  `NetworkServer::executeQuery` emite acum `{"type":"rows","columns":[...],"rows":[...]}`,
  iar GUI-ul (`drawResultsPanel`) desenează un rând-header cu numele coloanelor.
  Un `SELECT` cu 0 rânduri întoarce totuși header-ul (`"rows":[]`), spre deosebire de
  comenzile non-SELECT care rămân `{"type":"ok"}`.
  → Ăsta e cel mai important pentru DX-ul „connect in 2 lines of code".

- [ ] **Versiune de protocol + framing clar**
  Un câmp de versiune în handshake (ex. `NEXDB/1`) ca să poți evolua protocolul
  fără să spargi pachetele vechi. Ieftin acum, salvator mai târziu.

### 🎯 Propunere concretă de format nou (line-delimited JSON)
```
→  SELECT * FROM users\n
←  {"type":"rows","columns":["id","name"],"rows":[[1,"Alice"],[2,"Bob"]]}\n
←  {"type":"ok"}\n
←  {"type":"switch","db":"myproject"}\n
←  {"type":"error","message":"..."}\n
```
Avantaj: un singur `readline` + `JSON.parse` / `json.loads` în client, zero
ambiguități de delimitatori, extensibil (poți adăuga câmpuri fără breaking change).

---

## 🟠 P1 — Recomandate înainte de release-ul pachetelor (nu blocante)

- [ ] **Escape pentru `'` în string-uri** (`INSERT INTO t VALUES (1, don't)` sparge parserul).
  Se poate face client-side în pachet (parametrizare), dar mai curat server-side.
- [ ] **Mesaje de eroare clare + în engleză** (uniformizare; acum `Parse error: 5` nu ajută la debug).
- [ ] **Validare config** — crash dacă `config.json` are valori invalide (ex. `"port": "abc"`).

---

## 🟡 P2 — După primul release al pachetelor (fără breaking change)

Features SQL — clientul doar pasează string-ul mai departe, deci le adaugi oricând:

- [ ] **JOIN** (INNER + LEFT) — necesar pentru „DB relațional real"
- [ ] **ORDER BY** și **LIMIT**
- [ ] **Prepared statements** (parametrizare + escaping, parțial în pachet)
- [ ] **Rebuild index la fiecare query** — acum O(N)/query, devine lent pe tabele mari
- [ ] **Truncare WAL** — crește nelimitat pe procese long-running

---

## 🟢 P3 — Polish & UX (prioritate joasă)

- [ ] Custom title bar în GUI (undecorated window, drag, hover, flat UI)
- [ ] Compatibilitate wire cu psql / pgAdmin / MySQL Workbench (irelevant pentru local-only)
- [ ] TLS (irelevant pentru local-only)

---

## ✅ Deja rezolvate (prerechizite pentru pachete)

- [x] **Conexiuni concurente** — ASIO thread pool (`config.json: thread_count`)
- [x] **Comenzi multiple per conexiune** — `handleClient` se re-apelează după fiecare răspuns
      (`Networking/NetworkServer.cpp:246`); conexiunea rămâne deschisă. *Fără asta pachetul ar fi fost inutil.*
- [x] Tipuri complete — FLOAT, BOOL, NULL
- [x] Folder `databases/` cu auto-creare la startup
- [x] Un fișier `.db` cu tabele multiple (pages tag-uite cu `tableId`)
- [x] Fișier de configurare (`config.json`)
- [x] Caractere `|` și `~` în WAL (percent-encoding)
- [x] UPDATE / DELETE atomice (WAL `REPLACE_BEGIN` / `REPLACE_ROW`)
- [x] Fix memory leak `Condition` (WHERE)
- [x] Localhost bypass (`127.0.0.1` / `::1` → `AUTH OK` direct) — simplifică clientul local

---

## 📦 Ordinea de start recomandată

1. **P0** — îngheață și repară wire protocol-ul (delimitator, coloane, versiune).
2. **Pachet Python** — referință completă de flux în `tests/test_network.cpp`
   (handshake + query = practic specificația clientului).
3. **Pachet npm** — aceeași logică portată.
4. **P1 → P2 → P3** — incremental, fără să atingi pachetele.

### Adaosuri sugerate de mine (nu erau pe listă)
- **Ping/health command** (ex. `PING` → `PONG`) — util pentru client ca să testeze conexiunea.
- **Suită de teste de conformitate a protocolului** — un fișier de test care validează
  fiecare tip de răspuns; ambele pachete se validează contra lui.
- **Semantic versioning pe pachete** legat de versiunea de protocol (ex. pachet `1.x` ↔ `NEXDB/1`).
- **Timeout & reconnect automat** în client (mai ales pentru `SWITCH` la `USE DATABASE`).
- **CI simplu** (GitHub Actions) care rulează `tests/` la fiecare push — înainte să depinzi de pachete.

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

- [x] **Versiune de protocol + framing clar** ✅ *(rezolvat)*
  Serverul trimite acum un banner ca **prima linie** pe orice conexiune (înainte de auth):
  `NEXDB/1.0.0\n` (constanta `NetworkServer::PROTOCOL_VERSION`, emisă în `acceptConnections`,
  deci acoperă și calea localhost-bypass, și cea autentificată). Clientul citește prima linie
  și verifică versiunea majoră înainte de a continua. Helper-ii din `tests/test_network.cpp`
  citesc banner-ul + `CHALLENGE` din același `streambuf` (evită pierderea octeților la segmente
  TCP combinate); test nou `ServerSendsProtocolBanner` (Test 14). Framing-ul rămâne
  line-delimited JSON (un `readline` + `JSON.parse`). README actualizat cu noul handshake.

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

- [x] **Escape pentru `'` în string-uri** ✅ *(rezolvat — server-side, SQL-standard)*
  `Lexer::readString` tratează acum un apostrof dublat (`''`) ca `'` literal; un `'` singur
  închide string-ul. Deci `INSERT INTO t VALUES (1, 'don''t')` stochează `don't`. Fixul e
  complet în lexer — parserul doar consumă token-ul `STRING`, iar la ieșire `'` e inofensiv
  (`jsonEscape` acoperă `"`/`\`, storage-ul e length-prefixed, `walEncode` codează doar
  `|`/`~`/`%`/newline). Teste noi în `tests/test_lexer.cpp` (11–15) și un round-trip
  end-to-end în `tests/test_engine.cpp` (11b).
- [x] **Mesaje de eroare clare + în engleză** ✅ *(rezolvat)*
  `Parse error: 5` (cast enum→int la `Engine.cpp:242`) devine acum text lizibil printr-un
  helper `parseErrorMessage(ParseError)` din `Frontend/Parser/Parser.hpp` (ex.
  `Parse error: Empty column or value list`). Același helper e folosit și în logul de recovery
  din `WALManager.cpp` (`WAL parse error: ...`). Restul mesajelor din `Engine.cpp` erau deja în
  engleză — uniformizate stilistic (fără punct final). Struct-ul mort `ParseResult` din
  `Parser.hpp` a fost eliminat. Test nou anti-regresie în `tests/test_engine.cpp` (11c).
- [x] **Validare config** ✅ *(rezolvat)* — crash dacă `config.json` are valori invalide (ex. `"port": "abc"`).
- [ ] **Constrângeri de coloană: UNIQUE și AUTOINCREMENT**
  Azi cheia e implicită: prima coloană a tabelului, dacă e INT, e indexată și unică (`keyOf`,
  `checkRow`, verificarea din `executeUpdate`). În P1 pentru că mutarea la cheie declarată schimbă
  ce vede un client, deci trebuie decisă înainte de pachetul Python.
  - **Implicit vs. declarat.** Cu `UNIQUE` în schemă, regula „poziția 0 INT = unică” devine ascunsă
    și se bate cu cea explicită (n-ai putea avea un INT neunic pe prima coloană). Recomandat: cheie
    **doar declarată**. Catalogul fără flaguri se citește ca „coloana 0 INT e UNIQUE”, deci tabelele
    vechi nu se schimbă. Tabelele noi fără `UNIQUE` nu mai au cheie (schimbare vizibilă).
  - **Catalog compatibil.** `Catalog::load` (`Catalog/Catalog.cpp:68-106`) împarte `name TYPE` după
    spațiu și ignoră restul (`pair.size() >= 2`), deci `id INT UNIQUE AUTOINCREMENT` e citit fără
    erori de codul vechi. De extins: `Columns` (`AST/Columns/Columns.hpp`) cu flaguri, plus `save()`.
  - **Ce se refolosește.** `checkRow`, `keyOf`, `insertRow(row, enforceUnique)` și verificarea din
    `executeUpdate` se generalizează de la „coloana 0” la „fiecare coloană UNIQUE”. Semantica rămâne:
    comparație după tip, NULL în afara unicității, verificare înainte de WAL, rescrierea și
    recovery-ul nu verifică.
  - **Index.** `BPlusTree` e unul singur, cu chei `int`. Pentru mai multe coloane UNIQUE sau UNIQUE
    pe STRING: un index per coloană, sau verificare prin scan (O(N) per INSERT, același ordin cu
    `rebuildIndex`, care rulează oricum la fiecare query, vezi P2). Indexul pentru `WHERE x = …`
    poate rămâne pe o singură coloană.
  - **AUTOINCREMENT.** Doar pe INT, implică UNIQUE. Valoarea se generează în `Engine::executeInsert`
    **înainte** de `wal->logInsert`, ca WAL-ul să aibă valoarea concretă și replay-ul să fie
    determinist. Contor: `max(cheie) + 1` calculat la `rebuildIndex` (scanează deja tot), sau
    persistat în catalog (atunci id-urile șterse nu se refolosesc). Coloană omisă din INSERT sau
    NULL → se generează; valoare explicită → permisă, verificată ca unică, mută contorul dacă e mai
    mare. `Table::nextKey` **nu** e bun ca contor: numără rânduri și se resetează la rescriere.
  - **Protocol.** Fără breaking change: erori noi (`Duplicate key` pe alte coloane) și, opțional,
    `"insert_id"` în `{"type":"ok"}` după INSERT. `PROTOCOL_VERSION` rămâne.

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

- [x] ✅ **GUI în stil VS Code + custom title bar** *(rezolvat)*
  - **Fereastra** e undecorated (`FLAG_WINDOW_UNDECORATED`), cu bara de titlu desenată de noi:
    drag, dublu-click = maximize/restore, butoane minimize / maximize-restore / close (close roșu
    la hover), resize din margini și colțuri (GLFW nu dă margini native unei ferestre undecorated).
    Drag-ul folosește poziția cursorului pe ecran (`Platform::getCursorScreenPos`), pentru că
    poziția relativă a mouse-ului rămâne în urmă cât timp fereastra se mișcă. Maximize umple
    work area-ul monitorului (`Platform::getWorkArea`), deci nu acoperă taskbar-ul; un drag pe
    fereastra maximizată o restaurează sub cursor, ca în Windows.
  - **Explorer (stânga):** toate bazele din `databases/` → tabele → coloane cu tipul lor, baza
    activă evidențiată. Chevron = expand; click pe altă bază = `USE`; click pe tabel = `SELECT *`
    (cu `USE` înainte dacă e în altă bază). Refresh după fiecare query reușit, la 2 s (cu
    `try_lock`, ca un query lung de pe rețea să nu blocheze un frame) și din buton.
  - **Panoul central** (rotunjit, 3 secțiuni):
    - Results: grid cu coloane aliniate, header fix, coloană `#`, scroll vertical + orizontal
      (Shift+rotiță), NULL italic și estompat ca să nu semene cu `'NULL'`, număr de rânduri și
      timp de execuție în header.
    - Query: `GUI/TextEditor` — multi-linie, numere de linie, highlight SQL, selecție cu mouse și
      Shift, Ctrl+A/C/X/V, undo/redo, istoric Ctrl+Sus/Jos. Ctrl+Enter / F5 / Run rulează
      selecția sau tot textul; `;` separă statement-urile, care rulează pe rând și se opresc la
      prima eroare. Textul rămâne în editor după rulare (alegere, se poate schimba).
    - Log: culori pe tip de mesaj, timp de execuție, buton Clear.
  - **Panoul din dreapta:** rezervat pentru viitor.
  - **Status bar:** stare server + port (click = start/stop), baza activă, Ln/Col, temă (click =
    dark/light).
  - Panourile laterale se închid / deschid din title bar sau cu Ctrl+B / Ctrl+Alt+B, cu animație;
    panoul central se adaptează. Toate marginile (lățimi laterale, split-urile Results / Query /
    Log) se trag cu mouse-ul.
  - Fișiere noi: `GUI/TextEditor.*`, `GUI/Platform.*` (`windows.h` stă separat, nu se poate include
    lângă `raylib.h`). `GUI/Colors.hpp` e acum un `struct Theme` (dark / light) cu paleta veche.
    Fonturile încarcă și diacriticele românești. Accesori noi: `Catalog::getTableNames()`,
    `Engine::getCurrentDatabase()`, `NetworkServer::getEngineMutex()`.
  - Limitări: Win+săgeți și snap-ul la marginea ecranului nu merg (fereastră fără bară nativă);
    screenshot-urile din README arată încă layout-ul vechi. GUI-ul nu e acoperit de suita de teste.
- [x] ✅ **Zoom pe tot GUI-ul (Ctrl + rotița)** *(rezolvat)*
  - Layout-ul rămâne în pixeli logici (nicio constantă / literal schimbat); desenarea trece printr-un
    `Camera2D` cu `zoom = uiZoom`, iar `SetMouseScale(1/zoom)` face ca `GetMousePosition()` să fie
    tot logic, deci hover / click / splitter-e / editorul merg neschimbate.
  - Ce lucrează în pixeli reali e tratat separat: scissor-ele (`beginScissor` din `GUI/Zoom.hpp`,
    folosit și de `TextEditor`), `GetScreenWidth/Height` (împărțite la zoom), resize-ul ferestrei și
    mărimea minimă (`900×560 × zoom`, fereastra crește singură dacă e mai mică, și la restore după
    maximize). Fonturile se recoc la `32 × zoom`, deci textul e la fel de clar la orice zoom; textul
    e aliniat la pixel (`snapToPixel`) ca să nu iasă moale la 110% / 125%.
  - 80% – 200%, pas 10%, plafonat la cât încape fereastra minimă în work area (pe 2560×1600 la 150%
    scaling: 200%). `setZoom` rulează doar din `deferred`, după `EndDrawing`, ca un font să nu fie
    eliberat cât timp batch-ul de desenare încă îl folosește. Indicator „Zoom 130%” în status bar.
  - Salvat în `gui_settings.ini` (lângă exe, `key=value`; cheile necunoscute sunt ignorate, valori
    invalide → 100%).
  - Limitări: liniile de 1 px pot ieși puțin moi la zoom fracționar; zona de resize de la margine
    crește odată cu zoom-ul; un touchpad face cel mult un pas pe cadru.
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

### Inconsistențe reale
- ✅ **CREATE TABLE no-op silențios** *(rezolvat)* — `Catalog::createTable` întoarce acum `bool`
  (`false` dacă numele există sau lista e goală; verificarea și inserarea sub același lock, deci
  atomic), iar `Engine::executeCreate` aruncă `Table <name> already exists` la `false` — simetric
  cu `DROP TABLE`. Prin rețea clientul primește `{"type":"error",...}`, schema veche rămâne
  neatinsă. Testele concurente din Catalog nu se schimbă (Catalog nu aruncă). `HandshakeTestServer`
  șterge acum fișierele și *înainte* de a deschide engine-ul, ca un rest de la o rulare picată să
  nu facă `CREATE TABLE` să eșueze. Docs: `CLIENT_PROTOCOL.md` §7.5 rescris, README actualizat.
  Teste: `EngineTest.CreateExistingTableThrowsAndKeepsSchema`,
  `NetworkServerTest.CreateExistingTableReturnsError` (26), plus `EXPECT_TRUE/FALSE` pe valoarea
  întoarsă în `CatalogTest` 7 și 10. Ambele teste noi pică pe comportamentul vechi (verificat).
  Descrierea originală, pentru context:
  **CREATE TABLE no-op silențios** — asta e bug. Catalog::createTable (Catalog/Catalog.cpp:23-24) face return fără nimic dacă numele există sau dacă lista de coloane e goală, iar serverul răspunde {"type":"ok"}. Se comportă ca IF NOT EXISTS fără ca cineva să fi cerut asta, și e asimetric față de DROP TABLE care aruncă eroare (Engine.cpp:69). Clientul nu poate distinge "am creat tabelul" de "exista deja cu altă schemă".
- ✅ **BOOLEAN/TEXT** *(rezolvat — aliasuri reale)* — `Parser::parseCreate` folosește un helper
  `canonicalColumnType` care acceptă `TEXT` / `BOOLEAN` (orice majuscule) pe poziția de tip și le
  normalizează la `STRING` / `BOOL` înainte de catalog. Intenționat **nu** sunt keyword-uri în
  lexer: ar fi rezervat cuvintele și ar fi spart coloane numite `text` / `boolean`. Fiindcă
  normalizarea se face în parser, ramurile `"BOOLEAN"` / `"TEXT"` din `Table::validateValueForType`
  au fost șterse (rămâneau cod mort), la fel și ramura moartă `type == "NULL"`.
  Teste: `ParserTest` 14–16 (normalizare, `text`/`boolean` ca nume de coloane, tip necunoscut tot
  eroare), `EngineTest.TypeAliasesAreStoredAsCanonicalTypes`. Docs: `CLIENT_PROTOCOL.md` §5.1, §6.1, README.
  Descrierea originală, pentru context:
  **BOOLEAN/TEXT** — inconsistență internă: Table::validateValueForType le acceptă, parserul le respinge. Cod mort, nu bug funcțional.
- ✅ **NULL adevărat** *(rezolvat)* — `using Value = std::optional<std::string>` în `AST/Row/Row.hpp`
  (`std::nullopt` = SQL NULL); `Row`, `InsertStatement`, assignments din `UpdateStatement` și
  `Condition::value` folosesc `Value`. Lexer: `NULL`/`IS`/`NOT` devin keyword-uri. Parser: helper
  `parseValue()` (NULL nequotat → nullopt, `'NULL'` → string) folosit în INSERT/UPDATE/WHERE, plus
  `col IS [NOT] NULL` (op `"IS NULL"`/`"IS NOT NULL"`). `validateValueForType` acceptă NULL pentru
  orice tip. Storage/WAL: marker `%N` prin `encodeValue`/`decodeValue` (`StringUtils.hpp`) —
  `walEncode` scrie `%` doar urmat de 2 cifre hex, deci nu există coliziune. WHERE: orice
  comparație cu NULL e falsă (inclusiv `!=`), `= NULL` nu se potrivește cu nimic. Index: NULL în
  cheie (prima coloană, dacă e INT) e permis, fără intrare în index. Pe fir: `null` JSON, nu `"NULL"`
  (`PROTOCOL_VERSION` rămâne `1.0.0`, nimic publicat încă). GUI afișează `NULL`.
  Bug-uri reparate pe drum: (1) replay-ul WAL construia `VALUES (1,Alice Smith)` fără ghilimele —
  orice string cu spațiu/virgulă/`'`/gol pica la parsare și rândul se pierdea; acum `sqlLiteral`
  quotează tot. (2) `''` era eroare de parsare (verificare pe text gol în loc de tip token).
  (3) `split` arunca un câmp gol de la final, deci `''` în ultima coloană revenea cu o coloană
  lipsă; nou `splitFields` pentru datele de rând. (4) `UpdateStatement::condition` neinițializat
  → `delete` pe pointer garbage la `UPDATE` fără `WHERE`.
  Teste: `LexerTest` 16, `ParserTest` 17–21, `TableTest` 11–14, `EngineTest` 18–21,
  `WALManagerTest` 10, `WALRecoveryTest` 11–13, `NetworkServerTest` 27. Verificate prin mutații
  (replay fără ghilimele, `"NULL"` pe fir, `split` vechi → testele lor pică).
  Docs: `CLIENT_PROTOCOL.md` §4, §5.3, §5.4, §6.1, §6.2, §10; README.
  Descrierea originală, pentru context:
  **NULL nu e un NULL adevărat** — nu există un caz special pentru NULL nicăieri:
  `validateValueForType("NULL", "INT")` ajunge pe ramura INT și pică la `N`. Deci în `INT`/`FLOAT`/`BOOL`
  un `INSERT ... NULL` dă eroare de tip, iar în `STRING` e stocat ca textul `"NULL"`, identic cu
  `'NULL'`. `CLIENT_PROTOCOL.md` §5.3 documentează deja asta, dar **README-ul încă minte**: zice
  „NULL values can be inserted into any column regardless of type” (secțiunea FLOAT/BOOL și sub
  lista de tipuri), iar exemplul `INSERT INTO products VALUES (2, 'Out of stock', NULL, false)`
  ar eșua (NULL într-o coloană FLOAT). Un NULL real e o funcționalitate separată: marker în
  storage, sărit la validare, comparații în WHERE, și `null` în JSON-ul de pe fir (deci atinge
  și protocolul — mai bine înainte de pachete).

### Bug-uri care strică datele în tăcere (rezolvate înainte de pachete)
Erau documentate în `CLIENT_PROTOCOL.md` ca ciudățenii de ocolit în client; reparate acum ca
pachetele să nu construiască workaround-uri în jurul lor.

- ✅ **Numerele negative își pierdeau semnul** — `Lexer::tokenize` arunca `-` ca necunoscut, deci
  `VALUES (1, -5)` stoca `5` și `WHERE price > -0.5` rula ca `> 0.5`. Acum `-`/`+` lipit de o cifră
  face parte din număr (`+5` se stochează `5`, ca să compare egal cu `5`). `- 5` cu spațiu rămâne
  ignorat (decizie: doar semnul, restul caracterelor necunoscute tot se aruncă).
  Teste: `LexerTest` 17–18, `EngineTest.NegativeNumbersKeepTheirSign`.
- ✅ **Lista de coloane de la INSERT era ignorată** — `INSERT INTO users (name, id) VALUES ('Alice', 1)`
  punea `'Alice'` în `id`. `Engine::executeInsert` așază acum valorile după schemă; coloanele lipsă
  primesc NULL; coloană necunoscută / repetată → eroare. WAL-ul primește rândul deja reordonat, deci
  replay-ul pozițional rămâne corect. Teste: `EngineTest.InsertColumnListIsAppliedBySchema`,
  `InsertColumnListErrors`, `WALRecoveryTest.InsertWithColumnListIsLoggedInSchemaOrder`.
- ✅ **UPDATE eșua în tăcere** — coloană necunoscută sau valoare de tip greșit dădeau `{"type":"ok"}`.
  `Engine::executeUpdate` validează acum înainte (aceleași mesaje ca SELECT/INSERT), iar `query()`
  verifică și existența tabelului pentru UPDATE (lipsea). Nou pe fir: `{"type":"ok","affected":N}`
  pentru INSERT/UPDATE/DELETE (`Engine::getLastAffected()`, `Table::deleteRow`/`updateRow` întorc
  numărul); DDL rămâne `{"type":"ok"}`. GUI loghează `Rows affected: N`. Câmp adăugat, deci
  `PROTOCOL_VERSION` rămâne `1.0.0`. Teste: `EngineTest.UpdateErrorsAreReported`, `AffectedRowCounts`,
  `NetworkServerTest.DmlReportsAffectedRows` (28); testele de rețea care așteptau `ok` exact la DML
  au fost actualizate.
- ✅ **Coloană necunoscută în WHERE nu dădea eroare** — arăta ca „0 rânduri". Acum
  `Column X does not exist in table Y` la SELECT/UPDATE/DELETE (`requireConditionColumn` în Engine).
  Test: `EngineTest.UnknownWhereColumnIsAnError`.
- ✅ **Bonus — primul statement după pornire lipsea din WAL** — `WALManager::readLog` citea până la EOF
  fără `file.clear()`, deci stream-ul rămânea în `failbit` și prima scriere se pierdea până la primul
  `commit()`. Datele ajungeau pe disc, dar acel statement nu era protejat la crash. Găsit când testul
  pentru INSERT cu listă de coloane a găsit WAL-ul gol. Test: `WALRecoveryTest.FirstStatementAfterStartupIsLogged`.
- ✅ **Bonus — un replay WAL care eșuează nu mai oprește engine-ul** — `replaySQL` prinde excepțiile din
  `execute` și le loghează (`WAL replay error: ...`), la fel ca erorile de parsare; înainte, constructorul
  `Engine` arunca. Test: `WALRecoveryTest.FailingReplayDoesNotAbortRecovery`.

Toate verificate prin mutații (fiecare fix dezactivat → testele lui pică). Docs: `CLIENT_PROTOCOL.md`
§4, §6.1, §6.3, §6.4, §7.2, §7.4, §10; README (formate de răspuns).

### Deschise — găsite cu `teste.md` (§9)
Comportamentul real al engine-ului, documentat în `teste.md`. Primele patru ar trebui rezolvate
înainte de pachetul Python (schimbă ce vede un client); ultimul poate rămâne. Toate cele cinci sunt
rezolvate acum; ce s-a găsit pe drum e mai jos, la „Deschise — găsite pe drum”.

- [x] **Id-uri duplicate + index inconsistent** — decis: cheie unică. Prima coloană a tabelului,
  dacă e INT, nu mai acceptă o valoare existentă (`(name STRING, id INT)` nu are nicio cheie): `Insert/Update failed for table T: Duplicate key N for column id`.
  Cheia se compară ca int (`007` = `7`); NULL nu intră în unicitate (oricâte rânduri). Verificarea e
  în `Table::checkRow` (rulată de `insertRow` **înainte** de scrierea pe pagină) și, la UPDATE, în
  `Engine::executeUpdate` pe starea calculată de `previewUpdate`, doar când SET atinge cheia. Ambele
  verificări sunt înainte de WAL, deci un statement respins nu lasă nimic necommitat. Rescrierea
  din `deleteRow`/`updateRow` și replay-ul WAL nu verifică unicitatea (`insertRow(row, false)`,
  `!inRecovery`), ca duplicatele vechi să nu dispară în tăcere; ele rămân citibile, iar
  `WHERE id = x` întoarce tot un singur rând dintre ele. Bonus: un INT care trece de verificarea de
  cifre dar depășește `int` (`99999999999`) era scris pe pagină și abia apoi raporta eroare; acum
  nu se mai scrie nimic. Teste: `EngineTest.DuplicateKeyInsertIsRejected`, `ManyNullKeysAreAllowed`,
  `UpdateToExistingKeyIsRejected`, `UpdateWithoutKeyConflictSucceeds`,
  `TableTest.DuplicateKeyIsRejectedUnlessUniquenessIsOff`, `OverflowingKeyIsNotStored`,
  `WALRecoveryTest.RejectedInsertIsNotLogged`, `NetworkServerTest.DuplicateKeyInsertReturnsError`
  (29). Mutații: fără căutarea din `checkRow`, fără verificarea din `executeUpdate`, `checkRow` după
  `logInsert`, rescriere cu unicitate, `insertRow` fără `checkRow` → fiecare pică cel puțin un test.
  Docs: README (Names), `CLIENT_PROTOCOL.md` §5.3, §7.3 și lista de erori, `teste.md` 2.9, 3.10,
  3.20, 4.1, 4.16, §9. `PROTOCOL_VERSION` rămâne `1.0.0` (doar un mesaj de eroare nou).
  Inițial: prima coloană (INT) e indexată, dar nu unică: `SELECT ... WHERE id = 1` prin index
  întoarce **un** rând, `UPDATE ... WHERE id = 1` le modifică pe toate.
  Regulă temporară: cheia implicită pe poziția 0 se înlocuiește cu cheie declarată, vezi
  P1 „Constrângeri de coloană: UNIQUE și AUTOINCREMENT”.
- [x] **Coloane duplicate în CREATE TABLE** — `(a INT, a STRING)` dă acum
  `Column a specified more than once`, același text ca la INSERT, și nu creează nimic. Verificarea
  e în `Engine::executeCreate` (helper `requireUniqueNames`), nu în parser, fiindcă un `ParseError`
  nu poate purta numele coloanei. Numele țin cont de majuscule, deci `(a INT, A STRING)` rămâne
  valid. CREATE TABLE nu e în WAL, iar cataloagele vechi cu duplicate se încarcă la fel ca înainte
  (`findColumn` o ia pe prima). Inițial: acceptat, doar prima copie era accesibilă.
  Test: `EngineTest.CreateTableWithDuplicateColumnsIsRejected`.
- [x] **`=` / `!=` compară text** — `evaluateCondition` alege comparația după tipul coloanei:
  INT/FLOAT compară numeric la toți cei 6 operatori (`9.50 = 9.5`, `007 = 7`, deci `=` e adevărat
  exact când nici `<`, nici `>` nu sunt), BOOL compară valoarea logică la `=` / `!=` (`TRUE`, `true`,
  `1` sunt egale), STRING rămâne pe text. Lista de BOOL acceptate stă acum într-un singur loc
  (`boolValue` din `Table.cpp`), folosită și de validare, și de comparație. Limită: INT-urile peste
  2^53 se compară aproximativ. Pe drum:
  - **Calea prin index** (`selectRow`) folosea `std::stoi`: `id = 5.5` întorcea rândul 5, iar
    `id = 'abc'` / `id = 99999999999` trimiteau clientului eroarea `stoi`. Acum literalul se citește
    cu `stod`; doar un întreg exact care încape în `int` merge în index, restul cade pe scanarea
    completă, care dă răspunsul numeric corect.
  - **SELECT și UPDATE/DELETE nu mai sunt inconsecvente**: `previewUpdate` / `previewDelete`
    folosesc `evaluateCondition`, nu indexul, deci `SELECT ... WHERE id = 007` găsea rândul, dar
    `UPDATE ... WHERE id = 007` nu atingea nimic.
  - **Decis: un literal de alt tip în WHERE e eroare** (`requireValidCondition` din `Engine.cpp`,
    pentru SELECT, UPDATE, DELETE): `age = 'abc'` → `Type validation failed (invalid value for
    column age of type INT)`, în loc de 0 rânduri. Pe INT și FLOAT merge orice număr
    (`age > 2.5`, `id = 5.0`), pe BOOL doar cele 6 forme. `IS [NOT] NULL` și `= NULL` nu se
    verifică. Replay-ul WAL nu e afectat (rulează DELETE fără WHERE și INSERT).
  Teste: `EngineTest.EqualityComparesByColumnType`, `KeyLookupUsesNumericValue`,
  `WrongTypeLiteralInWhereIsAnError`.
- [x] **`_` la începutul unui nume** — `tokenize` intra în `readWord` doar pe `isalpha`, deci un
  `_` de la început cădea pe ramura de caractere necunoscute și era aruncat (`_x` devenea `x`).
  Acum intră și pe `_`; cast-ul la `unsigned char` evită UB la octeții UTF-8.
  Teste: `LexerTest.UnderscoreStartsAnIdentifier`, `ParserTest.ColumnNameMayStartWithUnderscore`.
- [x] **`CREATE DATABASE` pe un nume existent** — dă acum `Database '<name>' already exists` și
  lasă fișierele neatinse. Existența se judecă după `.db`, ca la `USE` și `DROP` (pe Windows, fără
  majuscule). Remote e blocat oricum (§7.8), deci eroarea se vede doar în GUI.
  Test: `EngineTest.CreateExistingDatabaseThrows`.
- [x] **Bonus — `UPDATE t SET a = 1, a = 2`** *(găsit pe drum)* — ultima atribuire câștiga în
  tăcere. Acum dă `Column a specified more than once`, înainte de WAL, ca la INSERT `(a, a)`.
  Test: `EngineTest.UpdateWithRepeatedColumnIsRejected`.

Toate verificate prin mutații (lexer fără `_`, CREATE și SET fără verificarea de duplicate, `=` din
nou pe text, indexul din nou pe `stoi`, WHERE fără validarea valorii, CREATE DATABASE fără `throw`
→ fiecare pică cel puțin un test). `teste.md` re-rulat integral prin engine. Docs: `teste.md` 0,
1.9, 1.12, §3.2, 3.14, 3.16, 3.17, 3.26, 4.17 (nou), 7.2, §9; README (WHERE, Errors, Names,
CREATE DATABASE); `CLIENT_PROTOCOL.md` §5.2, §6.1, §7.1, §7.3, §7.4, §7.5, §7.8, §8, §10.
`PROTOCOL_VERSION` rămâne `1.0.0` (doar mesaje de eroare noi).

### Deschise — găsite pe drum
- [ ] **Caractere necunoscute ignorate în tăcere de lexer** — ramura `else { position++; }` din
  `Lexer::tokenize` aruncă orice nu recunoaște: `;`, `"`, `@`, `.` izolat, `- 5` (devine `5`).
  `teste.md` 2.11 și 8.3 depind de asta (`"Dan"` merge din întâmplare, `;` final e ignorat), deci
  un fix trebuie să păstreze `;` și să decidă ce face cu restul (eroare de parsare, probabil).
- [ ] **`<` / `>` pe STRING compară numeric** — `evaluateCondition` trece STRING-ul prin `stod`, deci
  `n < '9'` nu găsește `'10'`, iar `n > 'abc'` nu găsește nimic. Ar trebui comparat lexicografic
  (sau refuzat). La fel `<` / `>` pe BOOL, unde merg doar `1` / `0`.
- [ ] **FLOAT acceptă `.` fără nicio cifră** — `validateValueForType` verifică doar „cifre și cel
  mult un `.`”, deci `'.'` și `'-.'` se stochează într-o coloană FLOAT (și trec ca literal în WHERE,
  unde nu se potrivesc cu nimic). Ar trebui cerută cel puțin o cifră.

### Bug-uri adevărate în NetworkServer.cpp
   Fiindcă tot revii la fișierul ăsta — astea sunt problemele reale din el:
   
- ✅ **Query-uri pipeline-uite** *(rezolvat)* — `struct Session` nou în `NetworkServer.hpp`
  (`tcp::socket` + `asio::streambuf readBuffer`, ținut prin `shared_ptr<Session>`), creat o
  singură dată în `acceptConnections` și pasat atât lui `handleHandshake` (care nu mai are
  `streambuf` local) cât și lui `handleClient` (care nu mai creează buffer la fiecare apel).
  `async_read_until` consumă întâi ce a rămas în buffer, deci liniile în plus nu se mai pierd.
  Teste noi în `tests/test_network.cpp` (19: două statement-uri într-un `write`; 20: `AUTH` +
  query într-un `write`) cu helper `readLineWithTimeout`; ambele picau cu `<timeout>` pe codul vechi.
  Descrierea originală, pentru context:
  **Query-uri pipeline-uite se pierd silențios.** handleClient creează un streambuf nou la fiecare apel (NetworkServer.cpp:273). async_read_until citește frecvent dincolo de delimitator, deci dacă clientul trimite CREATE TABLE ...\nINSERT ...\n într-un singur send(), al doilea statement rămâne în buffer-ul vechi și dispare când shared_ptr moare la recursie (NetworkServer.cpp:288). Zero eroare, zero răspuns. Asta e direct relevant pentru întrebarea ta inițială. Fix: mută buffer-ul în afara recursiei și consumă liniile rămase înainte de a re-arma citirea.

- ✅ **Excepții care omoară procesul** *(rezolvat)* — trei niveluri:
  (1) toate `asio::write` din `acceptConnections` și `handleHandshake`, plus `remote_endpoint`,
  folosesc acum overload-ul cu `error_code`; o scriere eșuată = client plecat → conexiunea e
  abandonată (handshake-ul întoarce `false`), fără excepție. Ternarul cu virgulă de la bypass a
  devenit `if/else`. (2) `acceptConnections()` se re-armează **la începutul** handler-ului, imediat
  după verificarea `operation_aborted`, deci niciun `return`/throw de mai jos nu mai rupe lanțul
  de accept. (3) Plasă de siguranță: `NetworkServer::runWorker()` rulează `io_context.run()` într-o
  buclă cu `try/catch` (loghează și reia), folosită de toate thread-urile din `run()` — deci nici
  `std::terminate` din thread-urile pool, nici cel din destructorul `std::thread` ne-join-uit.
  Test nou în `tests/test_network.cpp` (21: 30 de clienți care închid cu RST în diverse etape ale
  handshake-ului, apoi un query normal); pe codul vechi dădea `terminate called after throwing
  ... write: An existing connection was forcibly closed by the remote host`.
  Descrierea originală, pentru context:
  **Excepții care omoară procesul.** run() pornește thread-uri de pool cu io_context.run() gol, fără try/catch (NetworkServer.cpp:171-174). Iar în handler-ul de accept:
   •
   socket.remote_endpoint() (:242) aruncă dacă peer-ul s-a deconectat între accept și apel
   •
   toate asio::write din :244, :247, :122, :132, :153, :163 sunt overload-uri care aruncă asio::system_error
   Orice client care închide socket-ul în timpul handshake-ului aruncă o excepție care iese din handler, iese din run() și, pe un thread de pool, dă std::terminate. Pe thread-ul principal e prinsă în GUI (GUI.cpp:61), dar acceptConnections() nu se mai re-armează niciodată — serverul încetează să accepte conexiuni fără să spună nimic.

- ✅ **Handshake fără timeout** *(rezolvat)* — `handleHandshake` (sincron, `bool`) a devenit
  `startHandshake(shared_ptr<Session>, clientIp)`: `CHALLENGE` se scrie tot sincron, dar `AUTH`
  se citește cu `async_read_until`, deci un client mut nu mai ține niciun thread de io. `Session`
  are acum un `asio::steady_timer handshakeTimer` (construit pe executorul socket-ului) și un flag
  `handshakeDone`; la expirare, timer-ul închide socket-ul, iar callback-ul de citire anulează
  timer-ul pe orice cale (altfel ar închide clienți deja autentificați). Fiecare socket e acceptat
  pe propriul strand (`async_accept(asio::make_strand(io_context), ...)`), iar handshake-ul pornește
  prin `asio::dispatch` pe acel strand → handler-ele de citire/scriere/timer ale unei conexiuni nu
  rulează niciodată în paralel. Decizii: timeout-ul **nu** contează la ban; durata e configurabilă
  (`handshake_timeout` în `config.json`, default 5, validat > 0, parametru nou în ctor-ul
  `NetworkServer`, pasat din `GUI.cpp`). README + `docs/CLIENT_PROTOCOL.md` (§1, §2.3, §9) actualizate.
  Teste noi: `tests/test_network.cpp` 22–25 (clienți muți > thread-uri nu blochează serverul;
  clientul mut e deconectat după timeout; clientul autentificat supraviețuiește timeout-ului;
  timeout-ul nu dă ban) + `ConfigTest.NonPositiveHandshakeTimeoutThrows`. Verificate prin mutații:
  fiecare test pică dacă scoți bucata de fix pe care o păzește.
  Descrierea originală, pentru context:
  **Comentariul de la linia 134 minte.** Zice "synchronous, with 5s timeout via deadline" — nu există niciun timeout. handleHandshake face I/O blocant sincron pe un thread de io_context, deci o conexiune care nu trimite nimic blochează thread-ul pe termen nelimitat. Cu thread_count: 4, patru conexiuni deschise și mute îngheață tot serverul, inclusiv accept-ul.

- ✅ **Token-ul auth generat nu ajungea în log** *(rezolvat odată cu noul GUI)* — `GUI::toggleServer`
  apelează acum `setLogCallback` **înainte** de `prepare()`, deci `Generated auth token: ...` din
  `loadOrCreateSecret()` ajunge în log.
  Descrierea originală, pentru context:
  **Token-ul auth generat nu ajunge niciodată în log.** prepare() e apelat la GUI.cpp:45, dar setLogCallback abia la :51. Toate log-urile din loadOrCreateSecret() — inclusiv "Generated auth token: ..." (NetworkServer.cpp:97) — se duc într-un logCallback gol. Token-ul ajunge doar în server_auth.conf.

- ✅ **Race pe logs** *(rezolvat odată cu noul GUI)* — toate scrierile în log trec prin
  `GUI::addLog` (sub `logsMutex`), inclusiv callback-ul serverului și butonul Clear; desenarea
  citește tot sub lock.
  Descrierea originală, pentru context:
  **Race pe logs.** GUI.cpp:48, :55 și :73 fac push_back fără logsMutex, în timp ce thread-urile de io scriu sub lock — deci lock-ul nu protejează nimic acolo.

- ✅ **GUI-ul folosea engine-ul fără lock** *(găsit și rezolvat la rescrierea GUI-ului)* —
  `GUI::executeQuery` apela `engine.query` direct, în timp ce thread-urile de io îl apelau sub
  `engineMutex`, deci un query din GUI putea rula în paralel cu unul de pe rețea pe același
  engine. Acum GUI-ul ia același mutex (`NetworkServer::getEngineMutex()`), și pentru query-uri,
  și pentru citirea catalogului în explorer.
- ✅ **`USE` din GUI lăsa un switch în așteptare** *(găsit și rezolvat)* — GUI-ul nu apela
  `Engine::consumePendingSwitch()`, deci următorul client de rețea primea `{"type":"switch"}` ca
  răspuns la query-ul **lui**. Acum GUI-ul îl consumă imediat după query.
- ✅ **Repornirea serverului după ce thread-ul lui murise** *(găsit și rezolvat)* — dacă
  `server.run()` arunca, `serverThread` rămânea joinable, iar următorul START atribuia un thread
  nou peste el → `std::terminate`. Acum se face `join` înainte. `serverRunning` e `std::atomic<bool>`
  (era scris și din thread-ul serverului).

- **Minore**: rateLimitMap crește nelimitat (o intrare per IP, niciodată curățată); async_read_until pe streambuf fără limită de mărime; comparația auth de la :149 nu e constant-time; stop() apelează acceptor.close() din thread-ul GUI cât timp thread-urile de io folosesc acceptor-ul, ceea ce nu e thread-safe în asio (ar trebui prin asio::post).

### ✅Opțional: să fie configurabil✅
Dacă vrei switch în config.json în loc de hardcodat:

- **Config/Config.hpp:13** — adaugi bool allow_remote_db_admin = false;

- **Config/Config.cpp** — parse + validare, în stilul celorlalte bool-uri (bypass_localhost)

- **NetworkServer.hpp:37** — parametru nou în ctor + membru privat lângă bypassLocalhost (linia 52)

- **GUI/GUI.cpp:6** — pasezi config.allow_remote_db_admin

- **NetworkServer.cpp:305** — engine.query(query, allowRemoteDbAdmin)
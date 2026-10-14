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
  prima coloană INT e permis, fără intrare în index. Pe fir: `null` JSON, nu `"NULL"`
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

- **Token-ul auth generat nu ajunge niciodată în log.** prepare() e apelat la GUI.cpp:45, dar setLogCallback abia la :51. Toate log-urile din loadOrCreateSecret() — inclusiv "Generated auth token: ..." (NetworkServer.cpp:97) — se duc într-un logCallback gol. Token-ul ajunge doar în server_auth.conf.

- **Race pe logs.** GUI.cpp:48, :55 și :73 fac push_back fără logsMutex, în timp ce thread-urile de io scriu sub lock — deci lock-ul nu protejează nimic acolo.

- **Minore**: rateLimitMap crește nelimitat (o intrare per IP, niciodată curățată); async_read_until pe streambuf fără limită de mărime; comparația auth de la :149 nu e constant-time; stop() apelează acceptor.close() din thread-ul GUI cât timp thread-urile de io folosesc acceptor-ul, ceea ce nu e thread-safe în asio (ar trebui prin asio::post).

### ✅Opțional: să fie configurabil✅
Dacă vrei switch în config.json în loc de hardcodat:

- **Config/Config.hpp:13** — adaugi bool allow_remote_db_admin = false;

- **Config/Config.cpp** — parse + validare, în stilul celorlalte bool-uri (bypass_localhost)

- **NetworkServer.hpp:37** — parametru nou în ctor + membru privat lângă bypassLocalhost (linia 52)

- **GUI/GUI.cpp:6** — pasezi config.allow_remote_db_admin

- **NetworkServer.cpp:305** — engine.query(query, allowRemoteDbAdmin)
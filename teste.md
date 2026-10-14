# NexDB — Catalog de teste manuale

Toate comenzile acceptate de engine, cu variațiile lor (WHERE, NULL, numere negative, string-uri
speciale, liste de coloane, erori). **Fiecare rezultat de aici a fost obținut rulând exact aceste
comenzi prin engine**, nu dedus din cod.

## Cum folosești fișierul

Comenzile sunt o **secvență**: rulează-le în ordine, de sus în jos, pe o bază de date goală.
Rezultatele de la `SELECT`, `UPDATE` și `DELETE` depind de ce au inserat pașii anteriori.

Pornește în GUI cu:

```sql
CREATE DATABASE teste_main
USE teste_main
```

Unde vezi rezultatele:

| Rezultat | În GUI | Prin rețea (o linie JSON) |
|---|---|---|
| `ok` | `Table created` / `Table dropped` / `Database created` / `Command executed` | `{"type": "ok"}` |
| `ok, affected N` | `Rows affected: N` | `{"type": "ok", "affected": N}` |
| rânduri | tabelul în RESULTS + `Rows returned: N` în log | `{"type": "rows", "columns": [...], "rows": [...]}` |
| `switch db` | `Switched to database ...` | `{"type": "switch", "db": "..."}` |
| eroare | mesajul cu roșu în RESULTS + `[ERROR] <mesaj>` în log | `{"type": "error", "message": "<mesaj>"}` |

În GUI, mesajul apare în secțiunea RESULTS, iar în log cu prefixul `[OK]` și timpul de execuție.

În tabelele de mai jos, `null` înseamnă SQL NULL (în GUI apare `NULL` italic și estompat, pe
rețea `null` JSON), iar `"NULL"` înseamnă textul de 4 litere (în GUI apare normal).

---

## 0. Reguli generale

- O singură comandă pe linie. `;` de la final e ignorat.
- **Keyword-urile** nu țin cont de majuscule: `select`, `SELECT`, `Select` sunt identice.
- **Numele de tabele și coloane țin cont de majuscule**: `Users` ≠ `users`, `ID` ≠ `id`.
- Numele încep cu o literă. `NULL`, `IS`, `NOT` sunt rezervate și nu pot fi nume.
- Tipuri: `INT`, `STRING`, `FLOAT`, `BOOL`; aliasuri `TEXT` → `STRING`, `BOOLEAN` → `BOOL`.
- Valori acceptate:

  | Formă | Exemplu | Ce e |
  |---|---|---|
  | număr | `42`, `9.5`, `-5`, `-1.5`, `+2.5` | semnul trebuie lipit de cifră; `+` se aruncă |
  | string | `'Alice'`, `'don''t'`, `''` | `''` în interior = un `'` literal |
  | cuvânt fără ghilimele | `Bob`, `true` | tratat ca text |
  | NULL | `NULL`, `null` | SQL NULL, valid în orice tip de coloană |

---

## 1. CREATE TABLE

| # | Comandă | Rezultat |
|---|---|---|
| 1.1 | `CREATE TABLE users (id INT, name STRING, age INT, score FLOAT, active BOOL)` | ok |
| 1.2 | `create table Notes (id int, note text, done boolean)` | ok — `note` devine `STRING`, `done` devine `BOOL` |
| 1.3 | `CREATE TABLE flags (text TEXT, boolean BOOLEAN);` | ok — `text`/`boolean` merg ca nume de coloane |
| 1.4 | `CREATE TABLE users (x INT)` | eroare `Table users already exists` (schema veche rămâne) |
| 1.5 | `CREATE TABLE t1 ()` | eroare `Parse error: Empty column or value list` |
| 1.6 | `CREATE TABLE t2 (a VARCHAR)` | eroare `Parse error: Invalid column type` |
| 1.7 | `CREATE TABLE t3 (a INT,)` | eroare `Parse error: Trailing comma` |
| 1.8 | `CREATE TABLE t4 (a INT b INT)` | eroare `Parse error: Unexpected token in query` |
| 1.9 | `CREATE TABLE t5 (a INT, a STRING)` | ok ⚠️ — coloanele duplicate **nu** sunt verificate (§9) |
| 1.10 | `CREATE TABLE null (a INT)` | eroare `Parse error: Invalid identifier` (`null` e rezervat) |
| 1.11 | `CREATE TABLE t6 (id INT) extra` | eroare `Parse error: Unexpected tokens after end of statement` |
| 1.12 | `CREATE TABLE t7 (id INT, _x INT)` | ok ⚠️ — `_` de la început e aruncat, coloana devine `x` (§9) |
| 1.13 | `CREATE TABLE 1abc (id INT)` | eroare `Parse error: Invalid identifier` |

---

## 2. INSERT

### 2.1 Inserări valide

| # | Comandă | Rezultat |
|---|---|---|
| 2.1 | `INSERT INTO users VALUES (1, 'Alice', 30, 9.5, true)` | ok, affected 1 |
| 2.2 | `INSERT INTO users VALUES (2, Bob, 25, 7, false)` | ok, affected 1 — `Bob` fără ghilimele = text; `7` e FLOAT valid |
| 2.3 | `INSERT INTO users VALUES (3, 'don''t', 40, -1.5, 1)` | ok, affected 1 — stochează `don't`, `-1.5`; `1` e BOOL valid |
| 2.4 | `INSERT INTO users VALUES (4, '', -5, +2.5, 0)` | ok, affected 1 — string gol; `-5`; `+2.5` se stochează `2.5` |
| 2.5 | `INSERT INTO users VALUES (5, NULL, NULL, NULL, NULL)` | ok, affected 1 — NULL în STRING, INT, FLOAT, BOOL |
| 2.6 | `INSERT INTO users VALUES (6, 'NULL', 20, 1.0, TRUE)` | ok, affected 1 — textul `"NULL"`, nu NULL; `1.0` rămâne `1.0` |
| 2.7 | `INSERT INTO users VALUES (NULL, 'ghost', 1, 1, FALSE)` | ok, affected 1 — NULL e permis și în prima coloană INT (cea indexată) |
| 2.8 | `INSERT INTO users (name, id) VALUES ('Carol', 7)` | ok, affected 1 — rând `7, Carol, null, null, null` (coloanele omise = NULL) |
| 2.9 | `INSERT INTO users VALUES (1, 'Dup', 50, 3, true)` | ok ⚠️ — id duplicat acceptat (§9) |
| 2.10 | `INSERT INTO users VALUES (9, 'a\|b~c%d', 1, 1, true)` | ok, affected 1 — `\|`, `~`, `%` se păstrează exact |
| 2.11 | `INSERT INTO users VALUES (10, "Dan", 1, 1, true)` | ok ⚠️ — ghilimelele duble sunt aruncate, se stochează `Dan` |
| 2.12 | `insert into users values (11, 'lower', 1, 1, true);` | ok, affected 1 — minuscule + `;` final |
| 2.13 | `INSERT INTO users VALUES (8, 'x', 1, - 5, true)` | ok ⚠️ — semn cu spațiu: `- 5` devine `5` (§9) |

### 2.2 Erori

| # | Comandă | Rezultat |
|---|---|---|
| 2.20 | `INSERT INTO users (id, nme) VALUES (8, 'x')` | `Column nme does not exist in table users` |
| 2.21 | `INSERT INTO users (id, id) VALUES (8, 9)` | `Column id specified more than once` |
| 2.22 | `INSERT INTO users (id, name) VALUES (8)` | `Parse error: Column count does not match value count` |
| 2.23 | `INSERT INTO users VALUES (8, 'x')` | `Insert failed for table users: Column count mismatch (expected 5 columns)` |
| 2.24 | `INSERT INTO users VALUES ('abc', 'x', 1, 1, true)` | `Insert failed for table users: Type validation failed (invalid value for column type)` — text în INT |
| 2.25 | `INSERT INTO users VALUES (8, 'x', 1.5, 1, true)` | aceeași eroare de tip — zecimal în INT |
| 2.26 | `INSERT INTO users VALUES (8, 'x', 1, 'abc', true)` | aceeași eroare de tip — text în FLOAT |
| 2.27 | `INSERT INTO users VALUES (8, 'x', 1, 1, True)` | aceeași eroare de tip — BOOL acceptă doar `true false TRUE FALSE 1 0` |
| 2.28 | `INSERT INTO users VALUES (8, 'x', 1, 1, yes)` | aceeași eroare de tip |
| 2.29 | `INSERT INTO users VALUES (8, 'x', 1, 1.2.3, true)` | `Parse error: Unexpected token in query` |
| 2.30 | `INSERT INTO users VALUES (8, 'x', 1, 1e5, true)` | `Parse error: Unexpected token in query` — fără notație exponențială |
| 2.31 | `INSERT INTO users VALUES (8, 'x', 1, 1, true,)` | `Parse error: Unexpected token in query` |
| 2.32 | `INSERT INTO users VALUES ()` | `Parse error: Unexpected token in query` |
| 2.33 | `INSERT INTO nosuch VALUES (1)` | `Table nosuch does not exist` |
| 2.34 | `INSERT users VALUES (1)` | `Parse error: Missing required keyword` |

Starea tabelului `users` după secțiunea 2 (13 rânduri, în ordinea inserării):

| id | name | age | score | active |
|---|---|---|---|---|
| 1 | Alice | 30 | 9.5 | true |
| 2 | Bob | 25 | 7 | false |
| 3 | don't | 40 | -1.5 | 1 |
| 4 | *(gol)* | -5 | 2.5 | 0 |
| 5 | null | null | null | null |
| 6 | "NULL" | 20 | 1.0 | TRUE |
| null | ghost | 1 | 1 | FALSE |
| 7 | Carol | null | null | null |
| 1 | Dup | 50 | 3 | true |
| 9 | a\|b~c%d | 1 | 1 | true |
| 10 | Dan | 1 | 1 | true |
| 11 | lower | 1 | 1 | true |
| 8 | x | 1 | 5 | true |

---

## 3. SELECT

### 3.1 Coloane

| # | Comandă | Rezultat |
|---|---|---|
| 3.1 | `SELECT * FROM users` | cele 13 rânduri de mai sus, coloane `id, name, age, score, active` |
| 3.2 | `SELECT name, age FROM users` | 13 rânduri, coloanele `name, age` |
| 3.3 | `SELECT age, name FROM users WHERE id = 3` | `[40, don't]` — ordinea coloanelor e cea cerută |
| 3.4 | `SELECT nosuch FROM users` | `Column nosuch does not exist in table users` |
| 3.5 | `SELECT * FROM nosuch` | `Table nosuch does not exist` |
| 3.6 | `SELECT * FROM Users` | `Table Users does not exist` — numele țin cont de majuscule |
| 3.7 | `select id from users where id = 11` | `[11]` |

### 3.2 WHERE cu `=` și `!=` (comparație de text)

| # | Comandă | Rezultat |
|---|---|---|
| 3.10 | `SELECT * FROM users WHERE id = 1` | ⚠️ **un singur rând**: `Dup` — prin index, deși există doi cu id 1 (§9) |
| 3.11 | `SELECT name FROM users WHERE id = -1` | 0 rânduri |
| 3.12 | `SELECT name FROM users WHERE name = 'Alice'` | `Alice` |
| 3.13 | `SELECT name FROM users WHERE name != 'Alice'` | 11 rânduri — toate, mai puțin `Alice` și rândul cu name NULL |
| 3.14 | `SELECT name FROM users WHERE score = 9.50` | 0 rânduri ⚠️ — `=` compară text, `9.50` ≠ `9.5` |
| 3.15 | `SELECT name FROM users WHERE score = 9.5` | `Alice` |
| 3.16 | `SELECT name FROM users WHERE active = TRUE` | doar `"NULL"` (rândul 6, stocat ca `TRUE`) ⚠️ |
| 3.17 | `SELECT name FROM users WHERE active = true` | `Alice, Dup, a\|b~c%d, Dan, lower, x` |
| 3.18 | `SELECT id FROM users WHERE name = ''` | `[4]` |

### 3.3 WHERE cu `<`, `>`, `<=`, `>=` (comparație numerică)

| # | Comandă | Rezultat |
|---|---|---|
| 3.20 | `SELECT name FROM users WHERE id <= 1` | `Alice, Dup` — scanare completă, găsește ambele id 1 |
| 3.21 | `SELECT name FROM users WHERE age > 25` | `Alice, don't, Dup` |
| 3.22 | `SELECT name FROM users WHERE age < 0` | `""` (rândul 4, age -5) |
| 3.23 | `SELECT name FROM users WHERE age >= 30` | `Alice, don't, Dup` |
| 3.24 | `SELECT name FROM users WHERE score <= 1` | `don't, "NULL", ghost, a\|b~c%d, Dan, lower` |
| 3.25 | `SELECT name FROM users WHERE score > -2` | 11 rânduri — toate cu score diferit de NULL |
| 3.26 | `SELECT id FROM users WHERE age > 'abc'` | 0 rânduri — operand nenumeric = nicio potrivire, fără eroare |

### 3.4 WHERE cu NULL

| # | Comandă | Rezultat |
|---|---|---|
| 3.30 | `SELECT id FROM users WHERE name IS NULL` | `[5]` — doar NULL-ul real |
| 3.31 | `SELECT id FROM users WHERE name IS NOT NULL` | 12 rânduri (inclusiv `"NULL"` și `""`) |
| 3.32 | `SELECT id FROM users WHERE name = NULL` | 0 rânduri — ca în SQL standard, folosește `IS NULL` |
| 3.33 | `SELECT id FROM users WHERE name = 'NULL'` | `[6]` — textul, nu NULL-ul |
| 3.34 | `SELECT id FROM users WHERE id IS NULL` | `[null]` — rândul `ghost` |

### 3.5 Erori în WHERE

| # | Comandă | Rezultat |
|---|---|---|
| 3.40 | `SELECT id FROM users WHERE nosuch = 1` | `Column nosuch does not exist in table users` |
| 3.41 | `SELECT id FROM users WHERE ID = 1` | `Column ID does not exist in table users` |
| 3.42 | `SELECT id FROM users WHERE name IS 5` | `Parse error: Unexpected token in query` |
| 3.43 | `SELECT id FROM users WHERE` | `Parse error: Invalid identifier` |
| 3.44 | `SELECT id FROM users WHERE age > 1 AND age < 5` | `Parse error: Unexpected tokens after end of statement` — fără `AND`/`OR` |
| 3.45 | `SELECT FROM users` | `Parse error: Invalid identifier` |
| 3.46 | `SELECT id, FROM users` | `Parse error: Unexpected token in query` |

---

## 4. UPDATE

| # | Comandă | Rezultat |
|---|---|---|
| 4.1 | `UPDATE users SET age = 31 WHERE id = 1` | ok, affected **2** — scanare completă, ambele id 1 |
| 4.2 | `SELECT name, age FROM users WHERE age = 31` | `[Alice, 31]`, `[Dup, 31]` |
| 4.3 | `UPDATE users SET age = 32, score = 8.0 WHERE name = 'Alice'` | ok, affected 1 — mai multe coloane deodată |
| 4.4 | `UPDATE users SET name = NULL WHERE id = 2` | ok, affected 1 — `Bob` devine NULL |
| 4.5 | `UPDATE users SET age = -10 WHERE name IS NULL` | ok, affected 2 — id 2 și id 5 |
| 4.6 | `UPDATE users SET name = 'Zoe' WHERE id = 7` | ok, affected 1 |
| 4.7 | `UPDATE users SET age = 1 WHERE id = 999` | ok, affected **0** — nimic potrivit nu e eroare |
| 4.8 | `UPDATE users SET active = false` | ok, affected 13 — fără WHERE = toate rândurile |
| 4.9 | `UPDATE users SET age = 'old'` | `Update failed for table users: Type validation failed (invalid value for column age of type INT)` |
| 4.10 | `UPDATE users SET agee = 1` | `Column agee does not exist in table users` |
| 4.11 | `UPDATE users SET age = 1 WHERE nosuch = 1` | `Column nosuch does not exist in table users` |
| 4.12 | `UPDATE nosuch SET a = 1` | `Table nosuch does not exist` |
| 4.13 | `UPDATE users SET age > 1` | `Parse error: Unexpected token in query` |
| 4.14 | `UPDATE users SET age = 1,` | `Parse error: Invalid identifier` |
| 4.15 | `UPDATE users age = 1` | `Parse error: Missing required keyword` |

Erorile 4.9–4.15 nu modifică nimic.

`SELECT id, name, age, active FROM users` după secțiunea 4:

| id | name | age | active |
|---|---|---|---|
| 1 | Alice | 32 | false |
| 2 | null | -10 | false |
| 3 | don't | 40 | false |
| 4 | *(gol)* | -5 | false |
| 5 | null | -10 | false |
| 6 | "NULL" | 20 | false |
| null | ghost | 1 | false |
| 7 | Zoe | null | false |
| 1 | Dup | 31 | false |
| 9 | a\|b~c%d | 1 | false |
| 10 | Dan | 1 | false |
| 11 | lower | 1 | false |
| 8 | x | 1 | false |

---

## 5. DELETE

| # | Comandă | Rezultat |
|---|---|---|
| 5.1 | `DELETE FROM users WHERE id = 10` | ok, affected 1 |
| 5.2 | `DELETE FROM users WHERE name IS NULL` | ok, affected 2 — id 2 și id 5 |
| 5.3 | `DELETE FROM users WHERE age < 0` | ok, affected 1 — id 4 (age -5) |
| 5.4 | `DELETE FROM users WHERE id = 999` | ok, affected 0 |
| 5.5 | `DELETE FROM users WHERE nosuch = 1` | `Column nosuch does not exist in table users` |
| 5.6 | `DELETE FROM nosuch` | `Table nosuch does not exist` |
| 5.7 | `DELETE users` | `Parse error: Missing required keyword` |
| 5.8 | `SELECT id, name FROM users` | 9 rânduri: `1 Alice, 3 don't, 6 "NULL", null ghost, 7 Zoe, 1 Dup, 9 a\|b~c%d, 11 lower, 8 x` |
| 5.9 | `DELETE FROM users` | ok, affected 9 — fără WHERE șterge tot |
| 5.10 | `SELECT * FROM users` | 0 rânduri, dar header-ul de coloane rămâne |

---

## 6. DROP TABLE

| # | Comandă | Rezultat |
|---|---|---|
| 6.1 | `DROP TABLE notes` | `Table notes does not exist` — tabelul se numește `Notes` |
| 6.2 | `DROP TABLE Notes` | ok |
| 6.3 | `DROP TABLE Notes` | `Table Notes does not exist` |
| 6.4 | `DROP TABLE` | `Parse error: Invalid identifier` |
| 6.5 | `DROP flags` | `Parse error: Missing required keyword` |

---

## 7. Baze de date

| # | Comandă | Rezultat |
|---|---|---|
| 7.1 | `CREATE DATABASE teste_tmp` | ok |
| 7.2 | `CREATE DATABASE teste_tmp` | ok ⚠️ — dacă există deja, nu face nimic și nu dă eroare |
| 7.3 | `USE teste_tmp` | switch `teste_tmp` |
| 7.4 | `CREATE TABLE inner_t (id INT)` | ok — creat în `teste_tmp` |
| 7.5 | `USE DATABASE teste_main` | switch `teste_main` — ambele forme (`USE x` și `USE DATABASE x`) merg |
| 7.6 | `SELECT * FROM inner_t` | `Table inner_t does not exist` — fiecare bază are tabelele ei |
| 7.7 | `USE nosuch` | `Database 'nosuch' does not exist (use CREATE DATABASE first)` |
| 7.8 | `DROP DATABASE teste_main` | `Cannot drop the currently active database 'teste_main' (USE another database first)` |
| 7.9 | `DROP DATABASE TESTE_MAIN` | aceeași eroare — verificarea ignoră majusculele (fișierele Windows la fel) |
| 7.10 | `DROP DATABASE teste_tmp` | ok |
| 7.11 | `DROP DATABASE teste_tmp` | `Database 'teste_tmp' does not exist` |

Prin rețea (orice client, inclusiv de pe localhost), cu `allow_remote_db_admin: false` (default):

| # | Comandă | Rezultat |
|---|---|---|
| 7.20 | `CREATE DATABASE teste_remote` | `CREATE DATABASE/DROP DATABASE is not permitted for remote clients` |
| 7.21 | `DROP DATABASE teste_main` | aceeași eroare |
| 7.22 | `SELECT * FROM flags` | merge normal — doar comenzile de administrare sunt blocate |

`USE` prin rețea schimbă baza pentru **toți** clienții conectați, nu doar pentru cel care a trimis-o.

---

## 8. Erori generale de parsare

| # | Comandă | Rezultat |
|---|---|---|
| 8.1 | `HELLO world` | `Parse error: Missing required keyword` |
| 8.2 | `SELECT` | `Parse error: Invalid identifier` |
| 8.3 | `SELECT * FROM users; DROP TABLE users` | prin rețea: `Parse error: Unexpected tokens after end of statement` — o singură comandă pe query. În editorul GUI, `;` separă comenzile, deci aici le-ar rula pe amândouă |

---

## 9. Capcane cunoscute (comportament actual, nu bug-uri reparate)

Marcate cu ⚠️ mai sus. Toate sunt comportamentul real al engine-ului la data scrierii:

| Capcană | Unde | Detaliu |
|---|---|---|
| Id-uri duplicate acceptate | 2.9, 3.10, 3.20, 4.1 | Prima coloană INT e indexată, dar nu e unică. `WHERE id = 1` prin index întoarce **un** rând, iar `id <= 1`, UPDATE și DELETE le găsesc pe toate. |
| Coloane duplicate în CREATE TABLE | 1.9 | `(a INT, a STRING)` e acceptat. |
| `_` la începutul unui nume | 1.12 | E aruncat în tăcere: `_x` devine `x`. |
| Semn cu spațiu | 2.13 | `- 5` devine `5`; doar `-5` lipit păstrează semnul. |
| Ghilimele duble | 2.11 | `"Dan"` devine `Dan`; merge din întâmplare. |
| `=` compară text | 3.14, 3.16 | `9.50` ≠ `9.5`, `TRUE` ≠ `true`. `<`, `>`, `<=`, `>=` compară numeric. |
| `CREATE DATABASE` pe nume existent | 7.2 | Nu dă eroare (spre deosebire de `CREATE TABLE`). |
| Fără `AND`/`OR` | 3.44 | Exact o condiție per `WHERE`. Nici `ORDER BY`, `LIMIT`, `JOIN`. |

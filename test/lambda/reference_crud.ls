// Tier 3: the CRUD statements, the write set, and the `open` transaction.
// Design: vibe/Lambda_Design_Reference.md (PTH55-PTH80).

"===== temp. DOCUMENTS (PTH44v2, PTH76) =====";

// Runtime data gains identity by being PLACED IN A DOCUMENT.
let t = temp('crud_a', {n: 1, m: {x: 1, y: 2}, rows: [{id: 1}, {id: 2}]});
&t.m;
&t.rows;
(t.rows === temp.'crud_a'#rows);
// `temp(name)` returns the existing head.
(temp('crud_a') === t);

"===== ONE STATEMENT, ONE TRANSACTION (PTH63v2) =====";

// Outside `open`, each CRUD statement autocommits at once -- the shell case.
put temp.'crud_a'#n = 2;
temp.'crud_a'#n;
// The binding keeps the version it was given; a commit advances what the NEXT
// force yields, and nothing already bound changes (PTH50v3, MVCC PTH64v2).
t.n;
// An untouched subtree keeps its stamp and IS the same node (PTH43v2)...
(temp.'crud_a'#rows === t.rows);
// ...while a replaced spine is new.
(temp.'crud_a'# === t);

"===== THE CRUD STATEMENTS (PTH60v3, PTH70v4) =====";

del temp.'crud_a'#m.x;
temp.'crud_a'#m;
// Comma-joined edits record in written order, exactly as separate statements.
put temp.'crud_a'#m.p = 7, temp.'crud_a'#m.q = 8;
temp.'crud_a'#m;
// `into` adds a member; it is what replaces `push` in Tier 3.
put {id: 3} into temp.'crud_a'#rows;
temp.'crud_a'#rows;
// `before`/`after` anchor to a head NODE, so they name it however the sequence
// has shifted by the time the set is applied (PTH71v2).
let rows = temp.'crud_a'#rows;
put {id: 0} before rows.0, {id: 4} after rows.2;
temp.'crud_a'#rows;
// `put t = v` at a POSITION replaces that position.
put temp.'crud_a'#rows[0] = {id: 99};
temp.'crud_a'#rows;
// `del` on a node removes it.
let current = temp.'crud_a'#rows;
del current.0;
temp.'crud_a'#rows;

"===== THE open TRANSACTION (PTH66v2, PTH68v3) =====";

let d = temp('crud_b', {n: 10});
// One bounded transaction: every edit lands at the explicit commit.
open b = temp.'crud_b' {
    put b.n = 20;
    put b.tag = 'x';
    commit
};
temp.'crud_b'#;
// A block with no commit commits at its end.
open b2 = temp.'crud_b' { put b2.n = 30 };
temp.'crud_b'#n;
// `rollback` discards the next version.
open b3 = temp.'crud_b' { put b3.n = 99; rollback };
temp.'crud_b'#n;

"===== NO READ-YOUR-WRITES (PTH61) =====";

// The next version is WRITE-ONLY: both right-hand sides read the head, so two
// increments in one write set leave head + 1. Read-your-writes is Tier 2's job.
open b4 = temp.'crud_b' {
    put b4.n = temp.'crud_b'#n + 1;
    put b4.n = temp.'crud_b'#n + 1;
    commit
};
temp.'crud_b'#n;

"===== MVCC: A LOOP KEEPS ITS VERSION (PTH64v2) =====";

// A commit during the iteration advances the head for later forces and leaves
// the iteration untouched, so every row is reached.
let e = temp('crud_c', {rows: [{id: 1}, {id: 2}, {id: 3}]});
for (v in temp.'crud_c'#rows) { put v.seen = true };
temp.'crud_c'#rows

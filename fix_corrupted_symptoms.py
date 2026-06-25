#!/usr/bin/env python3
"""
Scans all 7_symptoms entries in pages.db, validates each comma-separated token
against the taxonomy vocabulary, and deletes rows containing invalid tokens so
that the LauncherUpdate re-processes those pages.
"""

import sqlite3
import sys

PAGES_DB   = "/home/cedric/Dropbox/freelancers/projects/workingDirectory/WebsiteEmpire2/Health/pages.db"
TAXONOMY_DB = "/home/cedric/Dropbox/freelancers/projects/workingDirectory/WebsiteEmpire2/Health/taxonomy/taxonomy.db"
SYMPTOMS_KEY = "7_symptoms"

def load_vocabulary(taxonomy_db):
    con = sqlite3.connect(taxonomy_db)
    cur = con.execute("SELECT name FROM items WHERE type='symptoms'")
    vocab = {row[0].strip() for row in cur.fetchall()}
    con.close()
    return vocab

def fix(dry_run=True):
    vocab = load_vocabulary(TAXONOMY_DB)
    print(f"Taxonomy loaded: {len(vocab)} symptoms")

    con = sqlite3.connect(PAGES_DB)
    rows = con.execute(
        "SELECT page_id, value FROM page_data WHERE key=?", (SYMPTOMS_KEY,)
    ).fetchall()

    print(f"Total pages with symptoms set: {len(rows)}")

    to_delete = []
    for page_id, value in rows:
        if not value.strip():
            continue
        tokens = [t.strip() for t in value.split(",") if t.strip()]
        bad = [t for t in tokens if t not in vocab]
        if bad:
            to_delete.append((page_id, value, bad))

    print(f"Corrupted entries: {len(to_delete)}")

    for page_id, value, bad_tokens in to_delete:
        preview = value[:120] + ("..." if len(value) > 120 else "")
        print(f"  page_id={page_id}  bad={bad_tokens}  value={repr(preview)}")

    if not to_delete:
        print("Nothing to fix.")
        con.close()
        return

    if dry_run:
        print("\nDry-run — no changes made. Re-run with --fix to apply.")
    else:
        cur = con.cursor()
        for page_id, _, _ in to_delete:
            cur.execute(
                "DELETE FROM page_data WHERE page_id=? AND key=?",
                (page_id, SYMPTOMS_KEY)
            )
        con.commit()
        print(f"\nDeleted {len(to_delete)} corrupted rows. Re-run the update pipeline to refill them.")

    con.close()

if __name__ == "__main__":
    dry = "--fix" not in sys.argv
    fix(dry_run=dry)

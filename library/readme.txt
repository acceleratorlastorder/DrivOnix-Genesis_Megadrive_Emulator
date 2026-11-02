Local library index
===================

Library::Index owns one SQLite connection and one worker thread. Construction
starts that worker; filesystem scanning, archive inspection, hashing, database
updates and history writes run there. The UI submits commands and polls an
immutable, shared Snapshot containing the requested page of games, variants and
individual source copies. Query offsets and limits bound the rendered page.
Barrier is a completion fence for tests and headless callers, not a UI wait.

The application stores library.sqlite beneath Storage::DataDirectory:
  Linux:   absolute XDG_DATA_HOME/drivonix, or HOME/.local/share/drivonix
  Windows: LOCALAPPDATA/drivonix, with the OS LocalAppData lookup as fallback
  Portable: data/ beside the executable
Settings remain in the editable INI. The index contains metadata and user
library state, never ROM payloads. SQLite3.54.1 is pinned in
thirdparty/sqlite.lock.json; verify_sources.py checks its unmodified bytes.

Each configured root has a stable ID. Files use root-relative locations, so
relocating the root while keeping its ID retains source IDs, favorites and
history. Directory links and special files are skipped rather than followed.
File fingerprints include native file identity, size and high-resolution change
times. Unchanged files are reused; changed files are inspected again, with a
second fingerprint check before their metadata commits. Overlapping roots do
not display the same path/member locator twice.

Raw cartridge scans read bounded files to obtain CRC32 and their embedded
header. ZIP/7z scans list archive metadata through installed 7-Zip; they never
decompress ROM payloads. Unsupported sizes, encryption and unsafe/duplicate
members receive individual diagnostics while eligible siblings remain visible.
Archive directory failures remain archive-level errors. Cached records survive
failed or cancelled scans. Only a fully completed root scan marks unseen files
missing; those rows are retained with their user state.

Games are initially grouped by the filename before region/dump tags. Variants
retain their full names; copies with matching name, source size and CRC remain
separate sources and save locations. Such copies are candidates until confirmed
by a normalized SHA256 from actual loading. BIN/MD/GEN normalized CRC and size
are immediately available; archived SMD data is enriched only when loaded.
An index candidate is not a claim of full game or hardware compatibility.

RomInfo::Load is a pure preparation routine. It validates and normalizes the
selected cartridge, computes its metadata and hashes, and returns bytes without
touching the global CPU, VDP, audio or loader. The UI's loading worker can pass
that result into a staged Session; RecordLoaded enriches the index afterward.

Raw saves keep their original <rom>.srm/.eep identity. Archive members use the
existing Archive::SaveIdentity result, including its exact case-sensitive,
slash-normalized member hash. Library grouping never merges or relocates saves.
RecordExternal adds only the explicitly opened game to Recents without scanning
the surrounding directory. Session durations are supplied by the frontend as
active emulation time. Manual metadata overrides take precedence over catalog
updates. A rebuild invalidates derived file metadata, retaining persistent IDs,
favorites, preferred copies, overrides and session history.

Catalog ingestion matches all indexed games, including entries outside the
visible page, by normalized CRC32 and size. Ambiguous records stay unmatched.
The worker retains the catalog for enrichment after later scans and SMD loads.
The last selected variant and preferred source are persistent user choices.
Unsupported future database schemas are rejected before changing journal mode;
an unreadable/corrupt database is reported without replacing it with an empty
index. Parser-version changes invalidate derived fingerprints while preserving
user state.

Cancellation tokens terminate and reap an active 7-Zip child. Child stdin is
closed; runtime, output and memory limits remain enforced. The scanner uses a
single process at a time and can be stopped before gameplay begins.

librarytests.cpp uses generated cartridges and temporary directories. It checks
incremental reuse and invalidation, mixed archive members, grouping, filters,
paging, metadata precedence, history persistence, rebuilds, root relocation,
overlap deduplication, cancellation, corrupt databases and legacy save identity.
The slow external-archiver fixture is deliberately unsanitized so that a child
process's memory limit can coexist with the parent test's ASan shadow mapping.

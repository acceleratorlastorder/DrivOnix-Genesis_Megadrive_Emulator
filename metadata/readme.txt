Online metadata and artwork
===========================

DrivOnix downloads only the public Mega Drive Libretro DAT catalogs and PNG
box-art images. No account, API key, ROM payload or local filesystem path is
sent. The transport accepts only these fixed HTTPS providers and does not
follow redirects to other endpoints. Windows uses system WinHTTP; Linux uses
system libcurl. Certificate verification remains enabled.

The catalog combines No-Intro checksum/size records with optional Libretro
developer, publisher, genre, release-year and player-count data. A checksum
match is not proof of a byte-identical ROM or an emulator compatibility result.
Unmatched or ambiguous games remain playable through their original source.
Manual metadata and artwork preferences belong to the library, outside this
derived cache, and must take precedence over provider data.

Service performs disk and network I/O on one worker. The UI requests only visible
and nearby covers, polls events and creates textures on its own thread. Calling
RequestCovers with the same viewport does not trigger repeated I/O. Viewport
changes cancel obsolete cover requests independently of catalog refreshes.
Local PNG overrides have priority and work offline. Cached catalogs and covers
remain usable when online access is disabled or an update fails.

PNG envelope checks enforce signatures, chunk checksums, structure and bounded
dimensions. The graphics decoder must also reject an image if final decoding
fails; service events never contain an already-created GPU object.

Catalog cache is atomically replaced only after parsing succeeds. Artwork cache
uses lowercase opaque filenames with full-origin URL collision checks. Its size
is bounded by Options::cacheLimitBytes (256 MiB by default). No game artwork is
bundled in packages. ClearCache affects only this cache directory, never ROMs,
manual overrides outside it, preferences, favorites or play history.

Metadata source and attribution:
  Libretro Database (CC-BY-SA-4.0)
  https://github.com/libretro/libretro-database

Artwork source:
  https://github.com/libretro-thumbnails/Sega_-_Mega_Drive_-_Genesis
  Original game artwork belongs to its respective rights holders.

Naming rules:
  https://docs.libretro.com/guides/roms-playlists-thumbnails/

#!/usr/bin/env python3
"""Refresh ``data/sharpless.csv`` from VizieR catalog VII/20.

Sources Sharpless 2 H II regions (Sharpless 1959, ApJS 4, 257) from the
VizieR catalog server (``vizier.cds.unistra.fr``) and writes them in
OpenNGC-compatible semicolon-delimited form, matching the existing
schema in ``data/sharpless.csv`` exactly.

The native VII/20 catalog uses B1900 coordinates and lacks a
constellation field, so the script:

* Asks VizieR for the computed J2000 virtual columns
  (``_RAJ2000`` / ``_DEJ2000``).
* Resolves the constellation locally using the IAU boundary table
  already shipped in ``data/constellations.csv`` (Roman 1987 /
  Delporte 1930, J2000.0 epoch). Algorithm matches
  ``ConstellationDB::Identify`` in
  ``PropertyHandler/XISFPropertyHandler/src/ConstellationDB.cpp`` so
  parsed values agree with the runtime lookup.

Output is UTF-8 with LF line endings (required by ``.gitattributes``
``data/*.csv text eol=lf``); the SHA-256 of the bytes written here is
what gets pinned into ``CatalogSpec.h``.
"""

from __future__ import annotations

import argparse
import sys
import urllib.request
from pathlib import Path

# 31-column header — must match data/sharpless.csv exactly. Downstream
# parsers (DSOCatalog.cpp::ParseCSVLine, ComputedProperties.cpp) read
# columns by fixed index, so do not reorder, rename, or insert columns.
HEADER = (
    "Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;"
    "B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;"
    "Pax;Pm-RA;Pm-Dec;RadVel;Redshift;"
    "Cstar-U;Cstar-B;Cstar-V;M;NGC;IC;"
    "CstarNames;Identifiers;Common names;NED notes;OpenNGC notes"
)

# VizieR ASU TSV endpoint. Asks for the virtual J2000 columns plus the
# native Sh2 number, diameter, and Sharpless classification triple.
VIZIER_URL = (
    "https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
    "?-source=VII/20/catalog"
    "&-out.max=unlimited"
    "&-out.add=_RAJ2000,_DEJ2000"
    "&-out=Sh2,Diam,Form,Struct,Bright"
)

REPO_ROOT = Path(__file__).resolve().parents[1]
CONSTELLATIONS_CSV = REPO_ROOT / "data" / "constellations.csv"
SHARPLESS_CSV = REPO_ROOT / "data" / "sharpless.csv"

EXPECTED_ROW_COUNT = 313  # Sharpless 1959 has 313 H II regions.


def load_constellation_boundaries(path: Path):
    """Parse ``B`` records from ``data/constellations.csv``.

    Returns a list of ``(ra_lo_hours, ra_hi_hours, dec_lo_deg, abbrev)``
    sorted by descending ``dec_lo`` so the first match in
    :func:`identify_constellation` wins (same convention as
    ``ConstellationDB.cpp``).
    """
    boundaries: list[tuple[float, float, float, str]] = []
    with path.open("r", encoding="utf-8", newline="") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split(",")]
            if len(parts) < 5 or parts[0] != "B":
                continue
            try:
                ra_lo = float(parts[1])
                ra_hi = float(parts[2])
                dec_lo = float(parts[3])
            except ValueError:
                continue
            abbrev = parts[4]
            if not abbrev:
                continue
            boundaries.append((ra_lo, ra_hi, dec_lo, abbrev))
    boundaries.sort(key=lambda b: -b[2])
    return boundaries


def identify_constellation(ra_deg: float, dec_deg: float, boundaries) -> str:
    ra_hours = (ra_deg / 15.0) % 24.0
    if ra_hours < 0.0:
        ra_hours += 24.0
    for ra_lo, ra_hi, dec_lo, abbrev in boundaries:
        if dec_deg >= dec_lo and ra_lo <= ra_hours < ra_hi:
            return abbrev
    return ""


def deg_to_hms(ra_deg: float) -> str:
    """Format RA in degrees as ``HH:MM:SS.ss`` (matches existing schema)."""
    ra_deg = ra_deg % 360.0
    total_seconds = ra_deg / 15.0 * 3600.0
    h = int(total_seconds // 3600)
    rem = total_seconds - h * 3600.0
    m = int(rem // 60)
    s = rem - m * 60.0
    # Guard against rounding to 60.00.
    if s >= 59.995:
        s = 0.0
        m += 1
        if m == 60:
            m = 0
            h = (h + 1) % 24
    return f"{h:02d}:{m:02d}:{s:05.2f}"


def deg_to_dms(dec_deg: float) -> str:
    """Format Dec in degrees as ``+DD:MM:SS.s`` (matches existing schema)."""
    sign = "-" if dec_deg < 0 else "+"
    abs_dec = abs(dec_deg)
    total_seconds = abs_dec * 3600.0
    d = int(total_seconds // 3600)
    rem = total_seconds - d * 3600.0
    m = int(rem // 60)
    s = rem - m * 60.0
    if s >= 59.95:
        s = 0.0
        m += 1
        if m == 60:
            m = 0
            d += 1
    return f"{sign}{d:02d}:{m:02d}:{s:04.1f}"


def fetch_vizier_tsv(url: str) -> str:
    req = urllib.request.Request(
        url,
        headers={
            "Accept": "text/tab-separated-values",
            "User-Agent": "XISF-Shell-Extensions refresh-sharpless/1.0",
        },
    )
    with urllib.request.urlopen(req, timeout=120) as resp:  # noqa: S310 - fixed https URL
        return resp.read().decode("utf-8")


def parse_vizier_tsv(tsv: str):
    """Yield ``dict[column -> str]`` for each data row in a VizieR ASU TSV.

    The ASU TSV format is::

        # comment lines ...
        col1<TAB>col2<TAB>...   <- header
        unit1<TAB>unit2<TAB>... <- units
        ----<TAB>----<TAB>...   <- separator
        v1<TAB>v2<TAB>...       <- data
    """
    columns: list[str] | None = None
    state = "header"
    for raw in tsv.splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if state == "header":
            columns = [c.strip() for c in fields]
            state = "units"
            continue
        if state == "units":
            state = "separator"
            continue
        if state == "separator":
            if all(set(f.strip()) <= {"-"} and f.strip() for f in fields):
                state = "data"
                continue
            state = "data"
            # Fall through: treat current line as data.
        if state == "data":
            assert columns is not None
            if len(fields) != len(columns):
                continue
            yield dict(zip(columns, [f.strip() for f in fields]))


def build_rows(vizier_rows, boundaries):
    """Translate VizieR rows to 31-column OpenNGC-compatible CSV records."""
    rows: list[tuple[int, list[str]]] = []
    for r in vizier_rows:
        sh_num = r.get("Sh2", "").strip()
        ra_str = r.get("_RAJ2000", "").strip()
        de_str = r.get("_DEJ2000", "").strip()
        diam = r.get("Diam", "").strip()
        if not sh_num or not ra_str or not de_str:
            continue
        try:
            sh_n = int(sh_num)
            ra_deg = float(ra_str)
            dec_deg = float(de_str)
        except ValueError:
            continue
        name = f"Sh2-{sh_n}"
        fields = [
            name,                            # 0  Name
            "HII",                           # 1  Type
            deg_to_hms(ra_deg),              # 2  RA
            deg_to_dms(dec_deg),             # 3  Dec
            identify_constellation(ra_deg, dec_deg, boundaries),  # 4  Const
            diam,                            # 5  MajAx (arcmin, integer in VII/20)
            "",                              # 6  MinAx
            "",                              # 7  PosAng
            "",                              # 8  B-Mag
            "",                              # 9  V-Mag
            "",                              # 10 J-Mag
            "",                              # 11 H-Mag
            "",                              # 12 K-Mag
            "",                              # 13 SurfBr
            "",                              # 14 Hubble
            "",                              # 15 Pax
            "",                              # 16 Pm-RA
            "",                              # 17 Pm-Dec
            "",                              # 18 RadVel
            "",                              # 19 Redshift
            "",                              # 20 Cstar-U
            "",                              # 21 Cstar-B
            "",                              # 22 Cstar-V
            "",                              # 23 M
            "",                              # 24 NGC
            "",                              # 25 IC
            "",                              # 26 CstarNames
            name,                            # 27 Identifiers
            "",                              # 28 Common names
            "",                              # 29 NED notes
            "Sharpless 1959 (VizieR VII/20)",  # 30 OpenNGC notes
        ]
        if len(fields) != 31:
            raise RuntimeError(f"Internal column count mismatch: {len(fields)}")
        rows.append((sh_n, fields))
    rows.sort(key=lambda x: x[0])
    return [r[1] for r in rows]


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print header and the first 5 generated rows; do not write the file.",
    )
    parser.add_argument(
        "--url",
        default=VIZIER_URL,
        help="Override the VizieR query URL (advanced).",
    )
    parser.add_argument(
        "--input",
        default=None,
        help="Read VizieR TSV from a local file instead of HTTP (offline testing).",
    )
    args = parser.parse_args()

    if not CONSTELLATIONS_CSV.is_file():
        print(f"error: constellation boundary file not found: {CONSTELLATIONS_CSV}",
              file=sys.stderr)
        return 2
    boundaries = load_constellation_boundaries(CONSTELLATIONS_CSV)
    if not boundaries:
        print(f"error: no boundary records parsed from {CONSTELLATIONS_CSV}",
              file=sys.stderr)
        return 2

    if args.input:
        tsv = Path(args.input).read_text(encoding="utf-8")
    else:
        tsv = fetch_vizier_tsv(args.url)

    vizier_rows = list(parse_vizier_tsv(tsv))
    rows = build_rows(vizier_rows, boundaries)

    if not rows:
        print("error: no rows produced from VizieR response", file=sys.stderr)
        return 1

    if len(rows) != EXPECTED_ROW_COUNT:
        print(
            f"warning: expected {EXPECTED_ROW_COUNT} rows from VizieR VII/20, got "
            f"{len(rows)}. Investigate before merging.",
            file=sys.stderr,
        )

    if args.dry_run:
        print(HEADER)
        for fields in rows[:5]:
            print(";".join(fields))
        print(f"... (total {len(rows)} rows)")
        return 0

    text = HEADER + "\n" + "\n".join(";".join(f) for f in rows) + "\n"
    # write_bytes with explicit "\n" keeps LF endings on every platform,
    # regardless of the host's default text-mode newline translation.
    SHARPLESS_CSV.write_bytes(text.encode("utf-8"))
    print(f"wrote {SHARPLESS_CSV} ({len(rows)} rows, LF line endings)")
    return 0


if __name__ == "__main__":
    sys.exit(main())

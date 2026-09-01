#!/usr/bin/env python3
"""Convert every sheet of an .ods file into its own .csv file.

Usage:
    python3 ods_to_csv.py <file.ods> [outdir] [--mode 444] [-y]

Options:
    --mode  Octal permission bits applied to each generated .csv
            (default: 444, read-only for everyone).
    -y      Skip the overwrite confirmation prompt.

Uses only the standard library (zipfile + xml.etree), so it works
without LibreOffice, pandas or odfpy installed.
"""
import argparse
import csv
import stat
import sys
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

NS = {
    "table": "urn:oasis:names:tc:opendocument:xmlns:table:1.0",
    "text": "urn:oasis:names:tc:opendocument:xmlns:text:1.0",
}


def cell_text(cell):
    return "\n".join(
        "".join(p.itertext()) for p in cell.findall("text:p", NS)
    )


def sheet_rows(table):
    for row in table.findall("table:table-row", NS):
        cells = row.findall("table:table-cell", NS) + row.findall(
            "table:covered-table-cell", NS
        )
        values = []
        for cell in cells:
            repeat = int(
                cell.get("{%s}number-columns-repeated" % NS["table"], "1")
            )
            values.extend([cell_text(cell)] * repeat)
        while values and values[-1] == "":
            values.pop()
        yield values


def extract_sheets(ods_path: Path):
    """Return {sheet_name: rows} for every non-empty sheet in the ODS."""
    with zipfile.ZipFile(ods_path) as z, z.open("content.xml") as f:
        root = ET.parse(f).getroot()

    sheets = {}
    for table in root.iter("{%s}table" % NS["table"]):
        name = table.get("{%s}name" % NS["table"])
        rows = [r for r in sheet_rows(table) if r]
        if rows:
            sheets[name] = rows
    return sheets


def confirm_overwrite(existing: list[Path]) -> bool:
    print("Se sobreescribiran los siguientes archivos:")
    for path in existing:
        print(f"  - {path}")
    answer = input("Continuar? [y/N] ").strip().lower()
    return answer == "y"


def convert(ods_path: Path, outdir: Path, mode: int, assume_yes: bool):
    sheets = extract_sheets(ods_path)
    outdir.mkdir(parents=True, exist_ok=True)

    targets = {name: outdir / f"{name}.csv" for name in sheets}
    existing = [p for p in targets.values() if p.exists()]
    if existing and not assume_yes:
        if not confirm_overwrite(existing):
            print("Operacion cancelada.")
            sys.exit(1)

    written = []
    for name, rows in sheets.items():
        out_path = targets[name]
        if out_path.exists():
            out_path.chmod(stat.S_IWUSR | stat.S_IRUSR)
        with out_path.open("w", newline="", encoding="utf-8") as fh:
            csv.writer(fh).writerows(rows)
        out_path.chmod(mode)
        written.append(out_path)
    return written


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ods_path", type=Path)
    parser.add_argument("outdir", type=Path, nargs="?")
    parser.add_argument(
        "--mode",
        default="444",
        help="Permisos octales para los .csv generados (default: 444, solo lectura)",
    )
    parser.add_argument(
        "-y",
        "--yes",
        action="store_true",
        help="No pedir confirmacion antes de sobreescribir",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    outdir = args.outdir or args.ods_path.with_suffix("")
    mode = int(args.mode, 8)
    for path in convert(args.ods_path, outdir, mode, args.yes):
        print(path)

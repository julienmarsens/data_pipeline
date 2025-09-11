import gzip
import shutil
from pathlib import Path
from datetime import datetime

BASE_DIR = Path("/Volumes/disk_ext/data_okx")
OUTPUT_DIR = BASE_DIR / "unzipped"
OUTPUT_DIR.mkdir(exist_ok=True)

def extract_date_from_folder(folder_name: str) -> str:
    """Convert folder name (YYYY-MM-DD) to YYYYMMDD."""
    return datetime.strptime(folder_name, "%Y-%m-%d").strftime("%Y%m%d")

def normalize_symbol(filename: str) -> str:
    """
    Turn 'okx_link-usd-swap.csv' into 'LINKUSDSWAP'.
    """
    name = filename
    if name.startswith("okx_"):
        name = name[len("okx_"):]   # strip okx_
    name = name.replace(".csv", "") # remove .csv
    name = name.replace("-", "")    # remove dashes
    name = name.upper()
    return name

def process_folder(folder_path: Path):
    folder_date = extract_date_from_folder(folder_path.name)

    for gz_file in folder_path.glob("okx*.gz"):
        inner_name = gz_file.stem   # e.g. "okx_link-usd-swap.csv"
        symbol = normalize_symbol(inner_name)  # "LINKUSDSWAP"

        new_name = f"{symbol}_PERP_INVERSE_OKX_{folder_date}.csv"
        out_path = OUTPUT_DIR / new_name

        # Decompress into the new file
        with gzip.open(gz_file, "rb") as f_in:
            with open(out_path, "wb") as f_out:
                shutil.copyfileobj(f_in, f_out)

        print(f"Extracted: {gz_file.name} -> {out_path.name}")

def main():
    for folder in BASE_DIR.iterdir():
        if folder.is_dir():
            try:
                process_folder(folder)
            except Exception as e:
                print(f"Skipping {folder}: {e}")

if __name__ == "__main__":
    main()

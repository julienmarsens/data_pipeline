import shutil
from pathlib import Path
import pandas as pd

disk_path = Path("/Volumes/disk_ext")
data_path = disk_path / "market_data"

output_dir = data_path / "okx_data_renamed"
output_dir.mkdir(exist_ok=True)

for subfolder in data_path.iterdir():
    if subfolder.is_dir():
        try:
            folder_date = subfolder.name
            date_str = folder_date.replace("-", "")  # YYYYMMDD
        except Exception:
            continue

        for file in subfolder.iterdir():
            if file.is_file() and file.name.startswith("okx_"):
                base = file.stem
                if base.startswith("okx_"):
                    coin_part = base[len("okx_"):].split("-")[0]
                    symbol = coin_part.upper()

                    ext = file.suffix
                    new_name = f"{symbol}USD_PERP_INVERSE_OKX_{date_str}{ext}"
                    new_file = output_dir / new_name

                    # Copy the file
                    shutil.copy(file, new_file)
                    print(f"Copied {file} -> {new_file}")

                    # Load and transform
                    df = pd.read_csv(new_file)

                    # Detect timestamp unit (seconds vs ms)
                    ts = df["timestamp"].iloc[0]
                    if ts > 1e12:   # looks like nanoseconds
                        unit = "ns"
                    elif ts > 1e10: # looks like milliseconds
                        unit = "ms"
                    else:           # assume seconds
                        unit = "s"

                    # Create human-readable datetime column
                    df.insert(
                        0,
                        "timestamp_human",
                        pd.to_datetime(df["timestamp"], unit=unit)
                    )

                    # Rename columns
                    df = df.rename(columns={
                        "timestamp": "timestamp",   # keep raw timestamp, second column
                        "best_bid_price": "b1",
                        "best_ask_price": "a1"
                    })

                    # Save back
                    df.to_csv(new_file, index=False)
                    print(f"Transformed {new_file}")

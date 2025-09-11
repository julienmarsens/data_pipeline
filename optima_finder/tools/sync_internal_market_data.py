import os
import pandas as pd
from datetime import datetime, timedelta
from ruamel.yaml import YAML

# === Load configs ===
yaml = YAML(typ="rt")
yaml.preserve_quotes = True
yaml.width = 10 ** 6

with open('./common/config/local_path.yml', 'r') as f:
    local_config = yaml.load(f)

with open('./optima_finder/results/optima_finder_pairs.yml', 'r') as f:
    optima_finder_pairs_config = yaml.load(f)

# === CONFIGURATION ===
pairs_to_sync = optima_finder_pairs_config["pairs"]

# Input raw data folder (with subfolders per date)
RAW_DATA_DIR = local_config["paths"]["data_path"]
SYNC_DATA_DIR = os.path.join(RAW_DATA_DIR, "sync_market_data")


# === SYMBOL MAP (exchange filenames) ===
def to_filename_symbol(symbol: str) -> str:
    """Map USDT symbols to raw CSV filenames."""
    return symbol.lower().replace("usdt", "usd-perp")


def to_output_symbol(symbol: str) -> str:
    """Map USDT symbols to output filename convention."""
    return symbol.lower().replace("usdt", "usd-perp")


# === TIMESTAMP FIX ===
def normalize_timestamp(ts: pd.Series) -> pd.Series:
    """
    Ensure timestamps are in milliseconds since epoch.
    - If values look like seconds (10 digits), multiply by 1000
    - If already ms (13 digits), leave unchanged
    """
    if ts.max() < 1e11:  # definitely in seconds
        return ts * 1000
    return ts


# === MAIN SCRIPT ===
def sync_pairs(start_date, end_date):
    os.makedirs(SYNC_DATA_DIR, exist_ok=True)

    start = datetime.strptime(start_date, "%Y-%m-%d").date()
    end = datetime.strptime(end_date, "%Y-%m-%d").date()
    delta = timedelta(days=1)

    d = start
    while d <= end:
        date_str = d.strftime("%Y-%m-%d")
        out_date_str = d.strftime("%Y%m%d")

        for sym_a, sym_b in pairs_to_sync:
            try:
                file_a = os.path.join(
                    RAW_DATA_DIR,
                    date_str,
                    f"binance-coin-futures_{to_filename_symbol(sym_a)}.csv"
                )
                file_b = os.path.join(
                    RAW_DATA_DIR,
                    date_str,
                    f"binance-coin-futures_{to_filename_symbol(sym_b)}.csv"
                )

                if not os.path.exists(file_a) or not os.path.exists(file_b):
                    print(f"Missing data for {sym_a}-{sym_b} on {date_str}")
                    continue

                # Load CSVs
                df_a = pd.read_csv(file_a)
                df_b = pd.read_csv(file_b)

                # Normalize timestamps → milliseconds
                df_a["timestamp"] = normalize_timestamp(df_a["timestamp"])
                df_b["timestamp"] = normalize_timestamp(df_b["timestamp"])

                # Rename columns
                df_a = df_a.rename(columns={
                    "timestamp": "time_seconds",
                    "best_bid_price": "bid_a",
                    "best_ask_price": "ask_a"
                })
                df_b = df_b.rename(columns={
                    "timestamp": "time_seconds",
                    "best_bid_price": "bid_b",
                    "best_ask_price": "ask_b"
                })

                # Deduplicate timestamps before merging
                dups_a = df_a["time_seconds"].duplicated().sum()
                dups_b = df_b["time_seconds"].duplicated().sum()
                if dups_a > 0 or dups_b > 0:
                    print(f"⚠️  {sym_a}-{sym_b} on {date_str}: "
                          f"removed {dups_a} dupes in A, {dups_b} in B")
                df_a = df_a.drop_duplicates(subset=["time_seconds"], keep="last")
                df_b = df_b.drop_duplicates(subset=["time_seconds"], keep="last")

                # Merge on timestamp (outer join) and sort
                merged = pd.merge(df_a, df_b, on="time_seconds", how="outer")
                merged = merged.sort_values("time_seconds").reset_index(drop=True)

                # Drop duplicate timestamps after merge (just in case)
                merged = merged.drop_duplicates(subset=["time_seconds"], keep="last")

                # Forward fill missing values (use last available price)
                merged[["bid_a", "ask_a"]] = merged[["bid_a", "ask_a"]].ffill()
                merged[["bid_b", "ask_b"]] = merged[["bid_b", "ask_b"]].ffill()

                # Drop rows that are still missing (before first tick of either asset)
                merged = merged.dropna(subset=["bid_a", "ask_a", "bid_b", "ask_b"])

                # Save with correct filename
                out_file = os.path.join(
                    SYNC_DATA_DIR,
                    f"binance-coin-futures__binance-coin-futures__"
                    f"{to_output_symbol(sym_a)}__{to_output_symbol(sym_b)}__{out_date_str}.csv"
                )
                merged.to_csv(out_file, index=False)

                print(f"✅ Saved {out_file}, rows={len(merged)}")

            except Exception as e:
                print(f"❌ Error syncing {sym_a}-{sym_b} on {date_str}: {e}")

        d += delta


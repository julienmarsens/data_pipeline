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

# === CONFIGURATION ===

RAW_DATA_DIR = local_config["paths"]["data_path"]
SYNC_DATA_DIR = os.path.join(RAW_DATA_DIR, "sync_market_data")

# === SYMBOL MAP (exchange filenames) ===
def to_filename_symbol(symbol: str) -> str:
    return symbol.lower().replace("usdt", "usd-perp")


def to_output_symbol(symbol: str) -> str:
    return symbol.lower().replace("usdt", "usd-perp")


# === TIMESTAMP FIX ===
def normalize_timestamp(ts: pd.Series) -> pd.Series:
    if ts.max() < 1e11:  # seconds → ms
        return ts * 1000
    return ts


# === MAIN SCRIPT ===
def sync_pairs(
        pairs: list,
        start_date: str,
        end_date: str,
    ):
    os.makedirs(SYNC_DATA_DIR, exist_ok=True)

    start = datetime.strptime(start_date, "%Y%m%d").date()
    end = datetime.strptime(end_date, "%Y%m%d").date()
    delta = timedelta(days=1)

    d = start
    while d <= end:
        date_str = d.strftime("%Y-%m-%d")
        out_date_str = d.strftime("%Y%m%d")

        for sym_a, sym_b in pairs:
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

                # Deduplicate
                df_a = df_a.sort_values("timestamp").drop_duplicates("timestamp", keep="last")
                df_b = df_b.sort_values("timestamp").drop_duplicates("timestamp", keep="last")

                # Rename
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

                # Align using asof merge
                merged = pd.merge_asof(
                    df_a,
                    df_b,
                    on="time_seconds",
                    direction="backward",
                    tolerance=1000  # 1 second tolerance
                )

                # Forward fill missing values
                merged[["bid_a", "ask_a"]] = merged[["bid_a", "ask_a"]].ffill()
                merged[["bid_b", "ask_b"]] = merged[["bid_b", "ask_b"]].ffill()

                # Drop rows still missing (before both legs have data)
                merged = merged.dropna(subset=["bid_a", "ask_a", "bid_b", "ask_b"])

                # Save output
                out_file = os.path.join(
                    SYNC_DATA_DIR,
                    f"binance-coin-futures__binance-coin-futures__{to_output_symbol(sym_a)}__{to_output_symbol(sym_b)}__{out_date_str}.csv"
                )
                merged.to_csv(out_file, index=False)

                print(f"Saved {out_file}, rows={len(merged)}")

            except Exception as e:
                print(f"Error syncing {sym_a}-{sym_b} on {date_str}: {e}")

        d += delta



import os
import pandas as pd
import numpy as np
from datetime import datetime, timedelta

def sanity_check_data(pairs, start_date, end_date, data_folder,
                      exchange="binance-coin-futures", verbose=True):
    """
    Sanity-check CSV files for given pairs and date range.

    Args:
        pairs: list of [assetA, assetB] pairs (e.g. [["ADAUSDT", "SUIUSDT"], ...])
        start_date: str, "YYYY_MM_DD"
        end_date: str, "YYYY_MM_DD"
        data_folder: str, path to folder containing CSVs
        exchange: str, exchange name prefix (default "binance-coin-futures")
        verbose: if True, pretty-print summary

    Returns:
        dict with categorized file issues
    """
    required_cols = {"time_seconds", "bid_a", "ask_a", "bid_b", "ask_b"}

    # date range iterator
    start_dt = datetime.strptime(start_date, "%Y_%m_%d")
    end_dt = datetime.strptime(end_date, "%Y_%m_%d")

    results = {
        "found_files": [],
        "missing_files": [],
        "empty_files": [],
        "bad_columns": [],
        "bad_timestamps": [],
        "nan_or_inf": [],
        "bid_ask_errors": [],
        "duplicates": [],
        "negative_values": [],
        "zero_values": [],
        "outliers": [],
        "bad_types": []
    }

    current = start_dt
    while current <= end_dt:
        date_str = current.strftime("%Y%m%d")  # filenames use YYYYMMDD
        for a, b in pairs:
            a_fmt = a.lower().replace("usdt", "usd-perp")
            b_fmt = b.lower().replace("usdt", "usd-perp")

            fname = f"{exchange}__{exchange}__{a_fmt}__{b_fmt}__{date_str}.csv"
            fpath = os.path.join(data_folder, fname)

            if not os.path.exists(fpath):
                results["missing_files"].append(fname)
            else:
                results["found_files"].append(fname)
                try:
                    df = pd.read_csv(fpath)

                    if df.empty:
                        results["empty_files"].append(fname)
                        continue

                    if not required_cols.issubset(df.columns):
                        results["bad_columns"].append(fname)
                        continue

                    # enforce numeric dtype
                    for col in required_cols:
                        if not np.issubdtype(df[col].dtype, np.number):
                            results["bad_types"].append(fname)
                            continue

                    # NaN / inf check
                    if not np.all(np.isfinite(df[required_cols].to_numpy())):
                        results["nan_or_inf"].append(fname)

                    # timestamp monotonic + int check
                    if not df["time_seconds"].is_monotonic_increasing:
                        results["bad_timestamps"].append(fname)
                    if not np.issubdtype(df["time_seconds"].dtype, np.integer):
                        if not np.all(df["time_seconds"] == df["time_seconds"].astype(int)):
                            results["bad_timestamps"].append(fname)

                    # duplicate timestamps
                    if df["time_seconds"].duplicated().any():
                        results["duplicates"].append(fname)

                    # bid < ask validation
                    if ((df["bid_a"] >= df["ask_a"]).any() or
                        (df["bid_b"] >= df["ask_b"]).any()):
                        results["bid_ask_errors"].append(fname)

                    # negative values
                    if (df[["bid_a", "ask_a", "bid_b", "ask_b"]] < 0).any().any():
                        results["negative_values"].append(fname)

                    # zero values
                    if (df[["bid_a", "ask_a", "bid_b", "ask_b"]] == 0).any().any():
                        results["zero_values"].append(fname)

                    # outlier detection (relative to median)
                    numeric_cols = ["bid_a", "ask_a", "bid_b", "ask_b"]
                    med = df[numeric_cols].median()
                    mad = (df[numeric_cols] - med).abs().median()
                    if ((df[numeric_cols] - med).abs() > 10 * mad).any().any():
                        results["outliers"].append(fname)

                except Exception as e:
                    results["bad_columns"].append((fname, str(e)))
        current += timedelta(days=1)

    if verbose:
        print("\n===== SANITY CHECK SUMMARY =====")
        for category, files in results.items():
            print(f"\n{category.upper()} : {len(files)}")
            if files:
                for f in files[:5]:
                    print("   ", f)
                if len(files) > 5:
                    print(f"   ... {len(files)-5} more")

    return results



pairs = [['ADAUSDT', 'SUIUSDT'], ['SUIUSDT', 'LTCUSDT'], ['DOTUSDT', 'SUIUSDT'], ['UNIUSDT', 'TRXUSDT'], ['SUIUSDT', 'XRPUSDT']]
start_date = "2025_08_19"
end_date = "2025_09_03"
data_folder = "/Volumes/disk_ext/market_data/sync_market_data"

print(sanity_check_data(pairs, start_date, end_date, data_folder))

# python3 common/sanity_check.py

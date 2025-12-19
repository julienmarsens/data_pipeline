import pandas as pd
from sklearn.preprocessing import MinMaxScaler
import os
import os
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from ruamel.yaml import YAML

def find_matching_column(sig, df_columns, tol=1e-8):
    """
    Try to find a matching column for signature `sig` in df_columns.
    Exact match first, otherwise compare numerically within tolerance.
    """
    # Exact match
    if sig in df_columns:
        return sig

    # Parse numbers from signature
    try:
        sig_parts = [float(x) for x in sig.split('#')]
    except Exception:
        return None

    for col in df_columns:
        try:
            col_parts = [float(x) for x in col.split('#')]
        except Exception:
            continue
        if len(sig_parts) == len(col_parts):
            if all(abs(a - b) < tol for a, b in zip(sig_parts, col_parts)):
                return col
    return None

def select_best_params(config_version,
                       csv_path: str,
                       min_r2: float,
                       min_num_trades: int,
                       min_sharpe: float,
                        number_of_config_per_pair: int
                        ):
    # Check if file exists and not empty
    if not os.path.exists(csv_path) or os.stat(csv_path).st_size == 0:
        print("❌ ERROR: Path does not exist or csv is empty")
        return None, False

    df = pd.read_csv(csv_path)

    if df.empty:
        print("❌ DF empty - No results were saved at grid run time")
        return None, False

    print(f"\n📊 Loaded results from {csv_path}")
    print(f"   → Total rows: {len(df)}")
    print(f"   → Columns: {list(df.columns)}")
    print(f"   → PnL range: {df['pnl.oos'].min()} … {df['pnl.oos'].max()}")
    print(f"   → Sharpe OOS range: {df['sharpe.ratio.oos'].min()} … {df['sharpe.ratio.oos'].max()}")
    print(f"   → Crossings OOS range: {df['num.crossing.oos'].min()} … {df['num.crossing.oos'].max()}")
    print(f"   → R² range: {df['r2'].min()} … {df['r2'].max()}")

    print(df.head())
    # Keep profitable runs
    df = df[df["pnl.oos"] > 0]
    print(f"\n✔️ Profitability filter: kept {len(df)} rows")

    # Apply thresholds
    df = df[
        (df["r2"] >= min_r2) &
        (df["num.crossing.oos"] >= min_num_trades) &
        (df["sharpe.ratio.oos"] >= min_sharpe)
    ]
    print(f"✔️ Threshold filters (r2 ≥ {min_r2}, crossings ≥ {min_num_trades}, sharpe.oos ≥ {min_sharpe})")
    print(f"   → Remaining candidates: {len(df)}")

    if df.empty:
        print("❌ DF empty - no optima after filtering")
        return None, False

    # Normalize metrics
    scaler = MinMaxScaler()
    df_norm = df.copy()
    metrics = ["sharpe.ratio", "pnl", "sharpe.ratio.oos", "pnl.oos", "r2"]
    df_norm[metrics] = scaler.fit_transform(df[metrics])

    # Cap extreme values for stability
    df_norm["num.crossing.oos"] = df["num.crossing.oos"].clip(upper=1000)
    df_norm["num.crossing.oos"] = scaler.fit_transform(df_norm[["num.crossing.oos"]])

    # Stability penalty
    df_norm["sharpe_drop"] = (df["sharpe.ratio"] - df["sharpe.ratio.oos"]).abs()
    df_norm["sharpe_drop"] = scaler.fit_transform(df_norm[["sharpe_drop"]])

    # Load weights
    yaml_rt = YAML()
    with open(f'./optima_finder/config/{config_version}.yml', 'r') as f:
        optima_finder_config = yaml_rt.load(f)

    weights = optima_finder_config["selection_utility_function"]
    print("\n⚖️ Using weights from config:")
    for k, v in weights.items():
        print(f"   {k}: {v}")

    # Composite score
    df_norm["score"] = (
        weights["sharpe_ratio_out_of_sample"] * df_norm["sharpe.ratio.oos"] +
        weights["sharpe_ratio_in_sample"] * df_norm["sharpe.ratio"] +
        weights["num_crossing_out_of_sample"] * df_norm["num.crossing.oos"] +
        weights["r2_out_of_sample"] * df_norm["r2"] +
        weights["pnl_out_of_sample"] * df_norm["pnl.oos"] -
        weights["sharpe_drop_out_sample_to_in_sample"] * df_norm["sharpe_drop"]
    )

    # Select best row
    best_idx = df_norm["score"].idxmax()
    best_row = df.loc[best_idx]
    best_score = df_norm.loc[best_idx, "score"]

    print("\n🏆 Best parameter set selected:")
    print(best_row)
    print(f"\n   Composite score: {best_score:.4f}")
    print(f"   IS Sharpe: {best_row['sharpe.ratio']:.3f}, "
          f"OOS Sharpe: {best_row['sharpe.ratio.oos']:.3f}")
    print(f"   IS PnL: {best_row['pnl']:.2f}, "
          f"OOS PnL: {best_row['pnl.oos']:.2f}")
    print(f"   Crossings OOS: {best_row['num.crossing.oos']} | R²: {best_row['r2']:.3f}")

    return best_row.to_dict(), True

import os
from pathlib import Path
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from ruamel.yaml import YAML


def load_pnl_csv(path):
    """
    Load pnl CSV, handling both '#'-separated format and standard CSV.
    Returns a DataFrame indexed by datetime.
    """
    df = pd.read_csv(path)

    # Case 1: malformed single column with '#'
    if df.shape[1] == 1 and df.columns[0].startswith("time_seconds"):
        # Split on commas
        df = df.iloc[:, 0].str.split(",", expand=True)

        # First col = time
        df[0] = pd.to_numeric(df[0], errors="coerce")

        # Split rest of values on '#'
        extra = df[1].str.split("#", expand=True)
        df = pd.concat([df[[0]], extra], axis=1)

        df.columns = ["time_seconds"] + [f"col{i}" for i in range(1, df.shape[1])]
        df = df.dropna().reset_index(drop=True)

    # Convert time_seconds → datetime index
    if "time_seconds" in df.columns:
        df["time"] = pd.to_datetime(df["time_seconds"], unit="ms", errors="coerce")
        df = df.set_index("time")

    return df


def plot_and_save_pnls(yaml_path, folder, output_pdf_path):
    """
    yaml_path: path to optima_finder_pairs.yml containing 'signature' and 'parameters'
    folder: folder where pnl_*.csv files are stored
    output_pdf_path: path to output PDF
    """

    yaml_rt = YAML()
    with open(yaml_path, "r") as f:
        yml = yaml_rt.load(f)

    signatures = yml["signature"]
    parameters = yml["parameters"]

    folder_path = Path(folder)
    gs_files = sorted(
        [f.name for f in folder_path.iterdir() if f.is_file() and f.name.startswith("gs_")]
    )

    if len(signatures) != len(gs_files):
        raise ValueError(f"Length mismatch: {len(signatures)} signatures vs {len(gs_files)} gs_files")

    all_series = {}

    pdf = PdfPages(output_pdf_path)
    try:
        # --- Individual plots ---
        for gs_file, sig, rel_param in zip(gs_files, signatures, parameters):
            pair_name = gs_file.replace("gs_", "pnl_")
            pnl_path = os.path.join(folder, pair_name)

            if not os.path.exists(pnl_path):
                print(f"⚠️ Missing pnl file for {gs_file}")
                continue

            df = load_pnl_csv(pnl_path)

            """if sig not in df.columns:
                print(f"⚠️ Signature {sig} not found in {pnl_path}")
                continue
            
            raw_series = df[col]"""

            # print("Columns in CSV:", list(df.columns))

            col = find_matching_column(sig, df.columns)
            if col is None:
                print(f"⚠️ Signature {sig} not found in {pnl_path}")
                continue

            # Use the matched column name
            print(f"Matched signature {sig} → column {col}")
            raw_series = df[col]

            all_series[f"{gs_file}\nparam={rel_param}"] = raw_series

            plt.figure(figsize=(10, 6))
            plt.plot(raw_series.index, raw_series, label=f"{gs_file}\nparam={rel_param}", linewidth=1.2)
            plt.title(f"Raw PnL series for {gs_file}")
            plt.xlabel("Timestamp")
            plt.ylabel("PnL")
            plt.legend()
            plt.grid(True)
            plt.tight_layout()
            pdf.savefig()
            plt.close()

            # --- Multi-series overlay plot with forward-fill ---
            if all_series:
                # Build union of all timestamps
                all_index = pd.Index(sorted(set().union(*[s.index for s in all_series.values()])))

                # Reindex and forward-fill
                aligned = [s.reindex(all_index).ffill() for s in all_series.values()]
                aligned_df = pd.concat(aligned, axis=1)
                aligned_df.columns = list(all_series.keys())

                # Multi-series overlay
                plt.figure(figsize=(12, 7))
                for col in aligned_df.columns:
                    plt.plot(aligned_df.index, aligned_df[col], label=col, linewidth=1.2)

                plt.title("Aggregate PnL series (all signatures, forward-filled)")
                plt.xlabel("Timestamp")
                plt.ylabel("PnL")
                plt.legend()
                plt.grid(True)
                plt.tight_layout()
                pdf.savefig()
                plt.close()

            # --- Sum plot with forward-fill alignment ---
            # Build union of all timestamps
            all_index = pd.Index(sorted(set().union(*[s.index for s in all_series.values()])))

            # Reindex each series on union and forward-fill
            aligned = [s.reindex(all_index).ffill() for s in all_series.values()]

            # Concatenate back
            aligned_df = pd.concat(aligned, axis=1)

            # Sum row-wise
            sum_series = aligned_df.sum(axis=1)

            plt.figure(figsize=(12, 7))
            plt.plot(sum_series.index, sum_series, color="black", linewidth=1.5)
            plt.title("Total Aggregate PnL (sum of all series, forward-filled)")
            plt.xlabel("Timestamp")
            plt.ylabel("PnL (sum)")
            plt.grid(True)
            plt.tight_layout()
            pdf.savefig()
            plt.close()

    finally:
        pdf.close()
        print(f"✅ Saved all plots into {output_pdf_path}")

# pyhton3 optima_finder/tools/results_analysis.py


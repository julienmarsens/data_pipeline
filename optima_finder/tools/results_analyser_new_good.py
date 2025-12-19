import os
import numpy as np
import pandas as pd
from pathlib import Path
from sklearn.preprocessing import MinMaxScaler
from ruamel.yaml import YAML
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages

# ============================================================
# Utilities
# ============================================================

def find_matching_column(sig, df_columns, tol=1e-8):
    if sig in df_columns:
        return sig

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


def load_pnl_csv(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)

    if df.shape[1] == 1 and df.columns[0].startswith("time_seconds"):
        df = df.iloc[:, 0].str.split(",", expand=True)
        df[0] = pd.to_numeric(df[0], errors="coerce")
        extra = df[1].str.split("#", expand=True)
        df = pd.concat([df[[0]], extra], axis=1)
        df.columns = ["time_seconds"] + [f"col{i}" for i in range(1, df.shape[1])]
        df = df.dropna().reset_index(drop=True)

    if "time_seconds" in df.columns:
        df["time"] = pd.to_datetime(df["time_seconds"], unit="ms", errors="coerce")
        df = df.set_index("time")

    return df


# ============================================================
# Core: multi-parameter robust selector
# ============================================================

def select_best_params(
    config_version: str,
    csv_path: str,
    min_r2: float,
    min_num_trades: int,
    min_sharpe: float,
    number_of_config_per_pair: int,
    max_pnl_corr: float = 0.7
):
    """
    Returns:
        best_rows: list[dict]
        success: bool
    """

    if not os.path.exists(csv_path) or os.stat(csv_path).st_size == 0:
        return None, False

    df = pd.read_csv(csv_path)
    if df.empty:
        return None, False

    # =========================================================
    # DEBUG: raw inputs + dataframe shape
    # =========================================================
    print("\n" + "=" * 120)
    print("[select_best_params] csv_path:", csv_path)
    print("[select_best_params] df.shape:", df.shape)
    print("[select_best_params] raw inputs:")
    for name, val in [
        ("min_r2", min_r2),
        ("min_num_trades", min_num_trades),
        ("min_sharpe", min_sharpe),
        ("number_of_config_per_pair", number_of_config_per_pair),
        ("max_pnl_corr", max_pnl_corr),
    ]:
        t = type(val).__name__
        try:
            l = len(val)
        except Exception:
            l = None
        try:
            shp = np.asarray(val).shape
        except Exception:
            shp = None
        print(f"  - {name}: type={t}, len={l}, np.shape={shp}, value_preview={str(val)[:200]}")

    def _ensure_scalar(x, name, cast=None):
        # DEBUG: what comes in
        print(
            f"[ensure_scalar] {name} incoming: type={type(x).__name__}, "
            f"len={len(x) if hasattr(x,'__len__') else None}, "
            f"np.shape={(np.asarray(x).shape if not np.isscalar(x) else None)}"
        )

        # Accept Python scalars
        if np.isscalar(x):
            out = cast(x) if cast else x
            print(f"[ensure_scalar] {name} scalar -> {out} ({type(out).__name__})")
            return out

        # Accept 1-element containers (list/ndarray/Series/etc.)
        if isinstance(x, (list, tuple, np.ndarray, pd.Series, pd.Index)):
            arr = np.asarray(x)
            print(f"[ensure_scalar] {name} arraylike size={arr.size}, shape={arr.shape}, dtype={arr.dtype}")
            if arr.size != 1:
                preview = arr.ravel()[:10]
                print(f"[ensure_scalar] {name} preview first10={preview}")
                raise ValueError(
                    f"{name} must be a scalar (single value). Got {type(x).__name__} with size={arr.size}."
                )
            out = arr.item()
            out = cast(out) if cast else out
            print(f"[ensure_scalar] {name} 1-elem -> {out} ({type(out).__name__})")
            return out

        # Fall back (e.g., strings)
        out = cast(x) if cast else x
        print(f"[ensure_scalar] {name} fallback -> {out} ({type(out).__name__})")
        return out

    min_r2 = _ensure_scalar(min_r2, "min_r2", float)
    min_num_trades = _ensure_scalar(min_num_trades, "min_num_trades", int)
    min_sharpe = _ensure_scalar(min_sharpe, "min_sharpe", float)
    number_of_config_per_pair = _ensure_scalar(number_of_config_per_pair, "number_of_config_per_pair", int)

    # =========================================================
    # DEBUG: dataframe columns needed for filtering
    # =========================================================
    needed = ["pnl.oos", "r2", "num.crossing.oos", "sharpe.ratio.oos"]
    missing = [c for c in needed if c not in df.columns]
    if missing:
        print("[select_best_params] MISSING columns:", missing)
        print("[select_best_params] available columns (first 30):", list(df.columns)[:30])
        return None, False

    print("[select_best_params] filter columns dtypes/stats:")
    for c in needed:
        s = pd.to_numeric(df[c], errors="coerce")
        print(
            f"  - {c}: dtype={df[c].dtype}, nan_count={df[c].isna().sum()}, "
            f"num_nan_after_coerce={s.isna().sum()}, min={s.min()}, max={s.max()}"
        )

    for c in needed:
        if df[c].dtype == "object":
            print(f"[select_best_params] WARNING: column {c} is object. sample values:",
                  df[c].dropna().astype(str).head(5).tolist())

    # =========================================================
    # DEBUG: show what the comparisons will do (shapes)
    # =========================================================
    print("[select_best_params] comparison operands:")
    print("  - df['num.crossing.oos']:", type(df["num.crossing.oos"]).__name__,
          "shape=", df["num.crossing.oos"].shape, "dtype=", df["num.crossing.oos"].dtype)
    print("  - min_num_trades:", type(min_num_trades).__name__, "value=", min_num_trades)
    print("  - df['r2']:", type(df["r2"]).__name__,
          "shape=", df["r2"].shape, "dtype=", df["r2"].dtype)
    print("  - min_r2:", type(min_r2).__name__, "value=", min_r2)
    print("  - df['sharpe.ratio.oos']:", type(df["sharpe.ratio.oos"]).__name__,
          "shape=", df["sharpe.ratio.oos"].shape, "dtype=", df["sharpe.ratio.oos"].dtype)
    print("  - min_sharpe:", type(min_sharpe).__name__, "value=", min_sharpe)

    # --------------------------------------------------------
    # Hard filters
    # --------------------------------------------------------
    pre = len(df)
    df = df[df["pnl.oos"] > 0]
    print(f"[select_best_params] after pnl.oos>0: {pre} -> {len(df)}")

    # Build masks separately to pinpoint failure
    m1 = df["r2"] >= min_r2
    print("[select_best_params] mask r2>=min_r2: true_count=", int(m1.sum()), "len=", len(m1))

    m2 = df["num.crossing.oos"] >= min_num_trades
    print("[select_best_params] mask num.crossing.oos>=min_num_trades: true_count=", int(m2.sum()), "len=", len(m2))

    m3 = df["sharpe.ratio.oos"] >= min_sharpe
    print("[select_best_params] mask sharpe.ratio.oos>=min_sharpe: true_count=", int(m3.sum()), "len=", len(m3))

    df = df[m1 & m2 & m3]
    print("[select_best_params] after all filters:", df.shape)

    if df.empty:
        print("[select_best_params] EMPTY after filters.")
        return None, False

    # --------------------------------------------------------
    # Normalize metrics for utility
    # --------------------------------------------------------
    scaler = MinMaxScaler()
    df_norm = df.copy()

    metrics = [
        "sharpe.ratio",
        "sharpe.ratio.oos",
        "pnl",
        "pnl.oos",
        "r2",
    ]

    # If any metric is non-numeric, coerce explicitly (useful debug)
    for c in metrics:
        if df_norm[c].dtype == "object":
            print(f"[select_best_params] WARNING: metric {c} is object; coercing to numeric.")
            df_norm[c] = pd.to_numeric(df_norm[c], errors="coerce")

    df_norm[metrics] = scaler.fit_transform(df_norm[metrics])

    df_norm["num.crossing.oos"] = pd.to_numeric(df["num.crossing.oos"], errors="coerce").clip(upper=1000)
    df_norm["num.crossing.oos"] = scaler.fit_transform(df_norm[["num.crossing.oos"]])

    df_norm["sharpe_drop"] = (
        pd.to_numeric(df["sharpe.ratio"], errors="coerce")
        - pd.to_numeric(df["sharpe.ratio.oos"], errors="coerce")
    ).abs()
    df_norm["sharpe_drop"] = scaler.fit_transform(df_norm[["sharpe_drop"]])

    # --------------------------------------------------------
    # Load weights
    # --------------------------------------------------------
    yaml_rt = YAML()
    with open(f"./optima_finder/config/{config_version}.yml", "r") as f:
        cfg = yaml_rt.load(f)

    w = cfg["selection_utility_function"]

    df_norm["score"] = (
        w["sharpe_ratio_out_of_sample"] * df_norm["sharpe.ratio.oos"]
        + w["sharpe_ratio_in_sample"] * df_norm["sharpe.ratio"]
        + w["num_crossing_out_of_sample"] * df_norm["num.crossing.oos"]
        + w["r2_out_of_sample"] * df_norm["r2"]
        + w["pnl_out_of_sample"] * df_norm["pnl.oos"]
        - w["sharpe_drop_out_sample_to_in_sample"] * df_norm["sharpe_drop"]
    )

    df_norm = df_norm.sort_values("score", ascending=False)

    # --------------------------------------------------------
    # Load corresponding PnL series
    # --------------------------------------------------------
    pnl_file = csv_path.replace("gs_", "pnl_")
    if not os.path.exists(pnl_file):
        print("[select_best_params] missing pnl_file:", pnl_file)
        return None, False

    pnl_df = load_pnl_csv(pnl_file)
    print("[select_best_params] pnl_df.shape:", pnl_df.shape)

    pnl_series = {}
    missing_cols = 0
    for idx, row in df.iterrows():
        sig = row["absolute.parameters"]
        col = find_matching_column(sig, pnl_df.columns)
        if col is not None:
            pnl_series[idx] = pnl_df[col].dropna()
        else:
            missing_cols += 1

    print("[select_best_params] pnl_series count:", len(pnl_series), "missing cols:", missing_cols)

    if not pnl_series:
        return None, False

    # --------------------------------------------------------
    # Greedy diversified selection
    # --------------------------------------------------------
    selected = []

    for idx in df_norm.index:
        if idx not in pnl_series:
            continue

        if not selected:
            selected.append(idx)
            continue

        ok = True
        for sel in selected:
            s1 = pnl_series[idx]
            s2 = pnl_series[sel]
            corr = s1.corr(s2)

            if corr is not None and corr > max_pnl_corr:
                ok = False
                break

        if ok:
            selected.append(idx)

        if len(selected) >= number_of_config_per_pair:
            break

    print("[select_best_params] selected count:", len(selected), "selected idxs:", selected[:10])

    if not selected:
        return None, False

    best_rows = df.loc[selected].to_dict(orient="records")
    return best_rows, True



# ============================================================
# Plotting (unchanged, works with multiple configs)
# ============================================================

def plot_and_save_pnls(yaml_path, folder, output_pdf_path):
    yaml_rt = YAML()
    with open(yaml_path, "r") as f:
        yml = yaml_rt.load(f)

    signatures = yml["signature"]
    parameters = yml["parameters"]

    folder_path = Path(folder)
    gs_files = sorted(
        [f.name for f in folder_path.iterdir()
         if f.is_file() and f.name.startswith("gs_")]
    )

    global_series = {}
    pdf = PdfPages(output_pdf_path)

    try:
        for gs_file, sig_list, param_list in zip(gs_files, signatures, parameters):

            pnl_path = os.path.join(folder, gs_file.replace("gs_", "pnl_"))
            if not os.path.exists(pnl_path):
                continue

            df = load_pnl_csv(pnl_path)

            pair_series = {}

            # --------------------------------------------------
            # Individual parameter plots
            # --------------------------------------------------
            for sig, param in zip(sig_list, param_list):
                col = find_matching_column(sig, df.columns)
                if col is None:
                    continue

                series = df[col]
                label = f"{gs_file}\n{param}"

                pair_series[label] = series
                global_series[label] = series

                plt.figure(figsize=(10, 6))
                plt.plot(series.index, series, linewidth=1.2)
                plt.title(f"{gs_file} — single parameter\n{param}")
                plt.xlabel("Time")
                plt.ylabel("PnL")
                plt.grid(True)
                plt.tight_layout()
                pdf.savefig()
                plt.close()

            if not pair_series:
                continue

            # --------------------------------------------------
            # NEW: Pair-level diversification plot
            # --------------------------------------------------
            idx = sorted(set().union(*[s.index for s in pair_series.values()]))
            aligned = [s.reindex(idx).ffill() for s in pair_series.values()]
            aligned_df = pd.concat(aligned, axis=1)
            aligned_df.columns = pair_series.keys()

            pair_sum = aligned_df.sum(axis=1)

            plt.figure(figsize=(12, 7))

            # overlay individual configs (thin)
            for col in aligned_df.columns:
                plt.plot(aligned_df.index, aligned_df[col],
                         linewidth=1.0, alpha=0.7)

            # aggregated pair pnl (thick)
            plt.plot(pair_sum.index, pair_sum,
                     color="black", linewidth=2.5,
                     label="Pair aggregate")

            plt.title(f"{gs_file} — parameter diversification")
            plt.xlabel("Time")
            plt.ylabel("PnL")
            plt.legend()
            plt.grid(True)
            plt.tight_layout()
            pdf.savefig()
            plt.close()

        # --------------------------------------------------
        # Final: Global aggregation across all pairs
        # --------------------------------------------------
        if global_series:
            idx = sorted(set().union(*[s.index for s in global_series.values()]))
            aligned = [s.reindex(idx).ffill() for s in global_series.values()]
            aligned_df = pd.concat(aligned, axis=1)

            total_sum = aligned_df.sum(axis=1)

            plt.figure(figsize=(12, 7))
            plt.plot(total_sum.index, total_sum,
                     color="black", linewidth=2.5)
            plt.title("Total Aggregate PnL (all pairs, all parameters)")
            plt.xlabel("Time")
            plt.ylabel("PnL")
            plt.grid(True)
            plt.tight_layout()
            pdf.savefig()
            plt.close()

    finally:
        pdf.close()

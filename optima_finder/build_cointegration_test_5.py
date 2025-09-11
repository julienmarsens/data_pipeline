#!/usr/bin/env python3
# rolling_half_life_eg_adf_sorted.py

import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import statsmodels.api as sm
from statsmodels.tsa.stattools import adfuller, coint
from PyPDF2 import PdfMerger


# ----------------------------------------------------------------------
# 1)  HALF-LIFE ESTIMATOR  (unchanged as requested)
# ----------------------------------------------------------------------
def estimate_half_life(spread: pd.Series) -> float:
    spread = spread.dropna()
    if len(spread) < 10:
        return np.nan

    spread_lag = spread.shift(1)
    spread_ret = spread - spread_lag
    spread_lag.iloc[0] = spread_lag.iloc[1]

    df = pd.DataFrame({"x": spread_lag, "y": spread_ret}).dropna()
    if len(df) < 10 or df["x"].std() == 0 or df["y"].std() == 0:
        return np.nan

    try:
        model = sm.OLS(df["y"], sm.add_constant(df["x"])).fit()
        lam = model.params.iloc[1]
        half_life = -np.log(2) / lam
        if lam >= 0 or np.isnan(half_life) or half_life > 5_000:
            return np.nan
        return half_life
    except Exception:
        return np.nan


# ----------------------------------------------------------------------
# 2)  P-VALUE HELPERS (EG cointegration + ADF residual with 'nc')
# ----------------------------------------------------------------------
def eg_pvalue(y: pd.Series, x: pd.Series) -> float:
    """Engle–Granger cointegration test p-value for y ~ x."""
    try:
        df = pd.DataFrame({"y": pd.Series(y).astype(float),
                           "x": pd.Series(x).astype(float)}).dropna()
        if len(df) < 30:
            return np.nan
        # returns (stat, pvalue, crit)
        return coint(df["y"], df["x"])[1]
    except Exception:
        return np.nan


def adf_resid_p(series: pd.Series) -> float:
    """
    ADF on residuals with:
      - no constant (residuals are ~mean-zero) -> regression="nc"
      - Schwert rule-of-thumb for max lag
    """
    try:
        s = pd.Series(series).astype(float).replace([np.inf, -np.inf], np.nan).dropna()
        n = len(s)
        if n < 50:
            return np.nan
        maxlag = int(np.floor(12.0 * (n / 100.0) ** 0.25))
        maxlag = max(0, min(maxlag, n // 2 - 1))
        # autolag=None to use the explicit maxlag; 'nc' => no constant
        return adfuller(s, regression="nc", maxlag=maxlag, autolag=None)[1]
    except Exception:
        return np.nan


# ----------------------------------------------------------------------
# 3)  ROLLING CALCULATION  – date-sorted, returns HL + EG p + ADF(nc) p
# ----------------------------------------------------------------------
def compute_half_life_series(
    df: pd.DataFrame,
    col1: str = "close_1",
    col2: str = "close_2",
    window: int = 1_000,
    step: int = 100,
):
    df = df.sort_index().loc[~df.index.duplicated()]

    half_lives, eg_vals, adf_vals, timestamps = [], [], [], []

    for start in range(0, len(df) - window + 1, step):
        sub = df.iloc[start : start + window]
        y, x = sub[col1].values, sub[col2].values

        if np.std(x) == 0 or np.std(y) == 0:
            half_lives.append(np.nan)
            eg_vals.append(np.nan)
            adf_vals.append(np.nan)
        else:
            try:
                model = sm.OLS(y, sm.add_constant(x)).fit()
                resid = pd.Series(model.resid, index=sub.index)
                half_lives.append(estimate_half_life(resid))
                eg_vals.append(eg_pvalue(sub[col1], sub[col2]))
                adf_vals.append(adf_resid_p(resid))
            except Exception:
                half_lives.append(np.nan)
                eg_vals.append(np.nan)
                adf_vals.append(np.nan)

        timestamps.append(df.index[start + window - 1])

    hl_series   = pd.Series(half_lives, index=timestamps)
    eg_series   = pd.Series(eg_vals,   index=timestamps)
    adf_series  = pd.Series(adf_vals,  index=timestamps)
    print("Rolling half-life, EG p-values, and ADF(nc) p-values computed.")
    return hl_series, eg_series, adf_series


# ----------------------------------------------------------------------
# 4)  PLOTTER – plots HL + ( -log10(EG p) ) + optional ADF(nc) overlay
#       Implements fix #4: clip p, transform, then smooth.
# ----------------------------------------------------------------------
def plot_half_life_series(
    half_life_series: pd.Series,
    eg_series: pd.Series,
    adf_series: pd.Series | None = None,
    threshold: int = 100,
    title: str = "Rolling Half-Life & Cointegration",
    return_fig: bool = False,
    *,
    asset_1: str | None = None,
    asset_2: str | None = None,
    span_hl: int = 15,
    span_p: int = 15,
    threshold_p: float = 0.03,
    clip_hl: int | None = 500,
):
    pair_lbl = (f"{asset_1.replace('_','/')}"
                f" – {asset_2.replace('_','/')}"
                if asset_1 and asset_2 else "")
    full_title = f"{title} {pair_lbl}".strip()

    # Half-life smoothing (as before)
    hl_smooth = half_life_series.sort_index().ewm(span=span_hl, adjust=False).mean()
    if clip_hl is not None:
        hl_smooth = hl_smooth.clip(upper=clip_hl)

    # ---- Fix #4: clip -> transform -> smooth (for p-values) ----
    eg = eg_series.sort_index().clip(lower=1e-12, upper=1.0)
    eg_log = -np.log10(eg)
    eg_log_smooth = eg_log.ewm(span=span_p, adjust=False).mean()

    adf_log_smooth = None
    if adf_series is not None:
        adf = adf_series.sort_index().clip(lower=1e-12, upper=1.0)
        adf_log = -np.log10(adf)
        adf_log_smooth = adf_log.ewm(span=span_p, adjust=False).mean()

    stationary = eg < threshold_p
    pct_pass   = stationary.mean() * 100

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(14, 10), sharex=True)

    # --- Panel 1: Half-life ---
    ax1.plot(hl_smooth, lw=1.6, label="Smoothed Half-Life", color="#1f77b4")
    ax1.axhline(threshold, color="red", ls="--", lw=1, label=f"{threshold}-bar threshold")
    ax1.fill_between(hl_smooth.index, 0, hl_smooth.values,
                     where=hl_smooth < threshold,
                     color="green", alpha=0.2, label="Below threshold")
    ax1.set_ylabel("Half-Life (bars)")
    ax1.set_title(full_title, fontsize=13)
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    # --- Panel 2: Cointegration / Stationarity ---
    ax2.plot(eg_log_smooth, lw=1.6, color="darkorange",
             label=r"$-\log_{10}$ EG p-value")
    if adf_log_smooth is not None:
        ax2.plot(adf_log_smooth, lw=1.2, color="#1f77b4", alpha=0.8,
                 label=r"$-\log_{10}$ ADF(nc) p-value")

    ax2.axhline(-np.log10(threshold_p), color="red", ls="--", lw=1,
                label=f"p<{threshold_p:g} cut-off")

    # Shade where EG indicates cointegration (primary signal)
    ax2.fill_between(eg_log.index, 0, eg_log.values,
                     where=stationary.values,
                     color="green", alpha=0.2, label=f"EG p < {threshold_p:g}")

    ax2.set_ylabel(r"$-\log_{10}(p)$")
    ax2.set_xlabel("Time")
    ax2.set_title(f"Cointegration & ADF(nc) – {pct_pass:.1f}% windows pass (EG)", fontsize=12)
    ax2.legend()
    ax2.grid(True, alpha=0.3)

    fig.tight_layout()
    return fig if return_fig else plt.show()


# ----------------------------------------------------------------------
# 5)  LOOP OVER PAIRS  – sorted, writes per-pair PDF and merged PDF
# ----------------------------------------------------------------------
def loop_over_pairs(_pairs, half_life_window, half_life_step):
    data_path      = "./common/data/binance/spread/"
    pdf_output_dir = "./common/data/binance/spread/reports/"
    os.makedirs(pdf_output_dir, exist_ok=True)

    individual_pdfs = []
    for asset_1, asset_2 in _pairs:
        file_name = f"{asset_1}_{asset_2}_data.pkl"
        file_path = os.path.join(data_path, file_name)
        try:
            df = pd.read_pickle(file_path)
            df.index = pd.to_datetime(
                df["open_time"] if "open_time" in df.columns else df.index
            )
            df = df.sort_index().loc[~df.index.duplicated()]

            hl, eg, adf_nc = compute_half_life_series(
                df,
                col1="close_1",
                col2="close_2",
                window=half_life_window,
                step=half_life_step,
            )

            fig = plot_half_life_series(
                hl, eg, adf_nc,
                threshold=100,
                title="Rolling Half-Life & Cointegration",
                return_fig=True,
                asset_1=asset_1, asset_2=asset_2,
                clip_hl=500
            )

            pdf_file = os.path.join(
                pdf_output_dir, f"{asset_1}_{asset_2}_half_life_eg_adf.pdf"
            )
            fig.savefig(pdf_file)
            plt.close(fig)
            individual_pdfs.append(pdf_file)
            print(f"✅ Saved: {pdf_file}")

        except Exception as e:
            print(f"⚠️  Error on {asset_1}-{asset_2}: {repr(e)}")

    if individual_pdfs:
        merged = PdfMerger()
        for pdf in individual_pdfs:
            merged.append(pdf)
        merged_path = os.path.join(
            pdf_output_dir, "all_pairs_half_life_eg_adf_report.pdf"
        )
        merged.write(merged_path)
        merged.close()
        print(f"\n📄  Merged PDF saved to: {merged_path}")


# ----------------------------------------------------------------------
# 6)  QUICK DEMO
# ----------------------------------------------------------------------
if __name__ == "__main__":

    demo_path = "./common/data/binance/spread/ADAUSDT_DOTUSDT_data.pkl"
    demo_df   = pd.read_pickle(demo_path)
    demo_df.index = pd.to_datetime(
        demo_df["open_time"] if "open_time" in demo_df.columns else demo_df.index
    )
    demo_df = demo_df.sort_index().loc[~demo_df.index.duplicated()]

    # --- Use only half the data (most recent half) ---
    half = len(demo_df) // 2
    demo_df = demo_df.iloc[-half:].copy()

    hl_demo, eg_demo, adf_demo = compute_half_life_series(demo_df, window=1000, step=100)
    plot_half_life_series(
        hl_demo, eg_demo, adf_demo,
        threshold=100,
        title="Rolling Half-Life & Cointegration",
        asset_1="ADAUSDT", asset_2="DOTUSDT",
        clip_hl=500
    )

    # Batch example
    # pairs = [("BTCUSDT", "ETHUSDT"), ("DOTUSDT", "DOGEUSDT")]
    # loop_over_pairs(pairs, half_life_window=1000, half_life_step=100)

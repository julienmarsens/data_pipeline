#!/usr/bin/env python3
"""
Signal-angle report generator (Binance spread pairs)

• Reads *.pkl* dataframes in ./common/data/binance/spread
• Draws a three-panel figure for each pair
• Collects all figures into ONE multi-page PDF
• Ranks pairs by a composite score that favours
      – high goodness-of-fit (R² overall AND per-period)
      – small dispersion of the three regression angles
"""

from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb
import warnings
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

# ──────────────────────────────────────────────────────────────────────────────
#  Helper utilities
# ──────────────────────────────────────────────────────────────────────────────
def _lighten(color: str, amount: float = 0.55) -> tuple[float, float, float]:
    """Blend *color* with white by *amount* (0→orig, 1→white)."""
    c = np.array(to_rgb(color))
    return tuple(c + (1.0 - c) * amount)


def _linreg_stats(x: np.ndarray, y: np.ndarray) -> tuple[float, float, float]:
    """
    Simple OLS of *y* on *x*.

    Returns
    -------
    slope, intercept, R²
    """
    slope, intercept = np.polyfit(x, y, 1)
    y_hat = intercept + slope * x
    ss_res = np.sum((y - y_hat) ** 2)
    ss_tot = np.sum((y - y.mean()) ** 2)
    r2 = 1.0 - ss_res / ss_tot if ss_tot else np.nan
    return float(slope), float(intercept), float(r2)


def _period_stats(df: pd.DataFrame):
    """
    Split *df* into three contiguous thirds and return:

        angles_deg   – list[float]   (θ₁, θ₂, θ₃)
        r2_list      – list[float]   (R²₁, R²₂, R²₃)
    """
    x = df['bid.a'].values
    y = df['bid.b'].values
    thirds = np.array_split(np.arange(len(df)), 3)

    angles, r2s = [], []
    for idxs in thirds:
        xs, ys = x[idxs], y[idxs]
        slope, _, r2 = _linreg_stats(xs, ys)
        angles.append(np.degrees(np.arctan(slope)))
        r2s.append(r2)
    return angles, r2s


# ──────────────────────────────────────────────────────────────────────────────
#  Figure builder (legend now shows R² as well)
# ──────────────────────────────────────────────────────────────────────────────
import matplotlib.dates as mdates

# ──────────────────────────────────────────────────────────────────────
#  Figure builder
# ──────────────────────────────────────────────────────────────────────
import matplotlib.dates as mdates

# ──────────────────────────────────────────────────────────────────────
#  Figure builder
# ──────────────────────────────────────────────────────────────────────
import matplotlib.dates as mdates

def make_signal_angle_figure(df: pd.DataFrame, pair_lbl: str) -> plt.Figure:
    """
    4-panel figure

      1️⃣ Cumulative % return of each leg (t₀ = 0 %)
      2️⃣ Symmetric % spread = 2·(A−B)/(A+B)
      3️⃣ Rolling 10-day regression angle  (robust)
      4️⃣ Scatter + period regressions      (price space)
    """
    # 1. DatetimeIndex from open_time ---------------------------------
    if 'open_time' not in df.columns:
        raise KeyError("'open_time' column missing")
    if not pd.api.types.is_datetime64_any_dtype(df['open_time']):
        df = df.copy()
        df['open_time'] = pd.to_datetime(df['open_time'], errors='raise')
    df = df.set_index('open_time').sort_index()
    t = df.index

    # 2. price series -------------------------------------------------
    mid_a = (df['bid.a'] + df['ask.a']) / 2
    mid_b = (df['bid.b'] + df['ask.b']) / 2

    perf_a = (mid_a / mid_a.iloc[0] - 1.0) * 100.0   # cumulative %
    perf_b = (mid_b / mid_b.iloc[0] - 1.0) * 100.0
    sym_spread = 2 * (mid_a - mid_b) / (mid_a + mid_b)

    x_price = df['bid.a'].to_numpy(float)
    y_price = df['bid.b'].to_numpy(float)
    g_slope, g_intercept, g_r2 = _linreg_stats(x_price, y_price)
    g_angle_deg = np.degrees(np.arctan(g_slope))

    thirds      = np.array_split(np.arange(len(df)), 3)
    scat_cols   = ['tab:blue', 'tab:red', 'tab:green']
    bg_cols     = [_lighten(c, 0.85) for c in scat_cols]

    # 3. rolling 10-day regression angle (robust to flat windows) ----
    win = 10
    roll_angle = np.full(len(df), np.nan)

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", np.RankWarning)  # silence polyfit

        for i in range(win - 1, len(df)):
            xs = x_price[i - win + 1 : i + 1]
            ys = y_price[i - win + 1 : i + 1]

            if np.ptp(xs) < 1e-8:          # leg-A almost flat → skip
                continue
            slope, _, _ = _linreg_stats(xs, ys)
            roll_angle[i] = np.degrees(np.arctan(slope))

    # 4. figure scaffold ---------------------------------------------
    fig, (ax0, ax1, ax2, ax3) = plt.subplots(
        4, 1, figsize=(11, 16),
        gridspec_kw={'hspace': 0.25},
        constrained_layout=True     # avoids tight_layout warning
    )
    ax1.sharex(ax0);  ax2.sharex(ax0)

    fig.suptitle(
        f"{pair_lbl} — overall θ = {g_angle_deg:.2f}°  (R² = {g_r2:.3f})",
        fontsize=14, fontweight='bold'
    )

    # 1️⃣ cumulative % returns ---------------------------------------
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=0.35)
    ln1, = ax0.plot(t, perf_a, color='steelblue',  label='Leg-A')
    ln2, = ax0.plot(t, perf_b, color='darkorange', label='Leg-B')
    ax0.set_ylabel('Cum. return  (%)')
    ax0.set_title('Cumulative % return (t₀ = 0 %)')
    ax0.legend(
        handles=[ln1, ln2],
        labels=['Leg-A', 'Leg-B'],
        frameon=False,
        loc='upper left'
    )
    ax0.grid(True)

    # 2️⃣ symmetric % spread -----------------------------------------
    for i, idx in enumerate(thirds):
        ax1.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=0.35)
    ax1.plot(t, sym_spread, color='purple')
    ax1.axhline(0, ls='--', lw=1, color='grey')
    lo, hi = sym_spread.min(), sym_spread.max()
    pad = 0.02 * (hi - lo) if hi != lo else 1e-4
    ax1.set_ylim(lo - pad, hi + pad)
    ax1.set_ylabel('2·(A−B)/(A+B)')
    ax1.set_title('Symmetric % spread')
    ax1.grid(True)

    # 3️⃣ rolling 10-day angle ---------------------------------------
    ax2.plot(t, roll_angle, color='teal')
    ax2.set_ylabel('θ (deg)')
    ax2.set_title('Rolling 10-day regression angle')
    ax2.grid(True)

    # 4️⃣ scatter + regressions --------------------------------------
    order = np.argsort(x_price)
    ax3.scatter(x_price[order], y_price[order], s=3, alpha=0.15, color='grey')
    ax3.plot(
        x_price[order], g_intercept + g_slope * x_price[order],
        color='black', lw=2.5,
        label=f"Total: θ={g_angle_deg:.2f}°, R²={g_r2:.3f}"
    )
    line_cols = [_lighten(c, 0.55) for c in scat_cols]
    labels    = ['Period 1', 'Period 2', 'Period 3']
    for i, idx in enumerate(thirds):
        xs, ys = x_price[idx], y_price[idx]
        if np.ptp(xs) < 1e-8:       # skip degenerate window
            continue
        slope, inter, r2 = _linreg_stats(xs, ys)
        sidx = np.argsort(xs)
        ax3.plot(xs[sidx], inter + slope * xs[sidx],
                 color=line_cols[i], lw=2,
                 label=f"{labels[i]}: θ={np.degrees(np.arctan(slope)):.2f}°, R²={r2:.3f}")
        ax3.scatter(xs, ys, s=6, alpha=0.65, color=scat_cols[i])
    ax3.set_xlabel('bid.a');  ax3.set_ylabel('bid.b')
    ax3.grid(True);  ax3.legend(frameon=False)

    # 5. neat date ticks on first 3 panels ---------------------------
    locator   = mdates.AutoDateLocator()
    formatter = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2):
        a.xaxis.set_major_locator(locator)
        a.xaxis.set_major_formatter(formatter)
    plt.setp(ax2.get_xticklabels(), rotation=45, ha='right')

    return fig

# ──────────────────────────────────────────────────────────────────────────────
#  Main driver
# ──────────────────────────────────────────────────────────────────────────────
def loop_over_pairs(_pairs: list[tuple[str, str]]) -> None:
    """
    Rank *_pairs* by a composite **score** combining:

        • σθ        – std dev of the three angles   ↓ better
        • R²_overall
        • mean R²_periods

    score = (σθ / 90) + (1 − R²_overall) + (1 − mean_R²_periods)
    Lower score ⇒ earlier in PDF.
    """
    data_path      = Path('./common/data/binance/spread')
    pdf_output_dir = data_path / 'reports'
    pdf_output_dir.mkdir(parents=True, exist_ok=True)

    out_pdf = pdf_output_dir / 'signal_angles_report_ranked.pdf'

    records = []   # (score, σθ, R²_overall, pair, df)

    for asset1, asset2 in _pairs:
        file_path = data_path / f"{asset1}_{asset2}_data.pkl"
        try:
            df = pd.read_pickle(file_path)

            # tolerate ‘close_1/2’ naming
            if {'bid.a', 'bid.b'}.issubset(df.columns) is False:
                df = df.rename(columns={'close_1': 'bid.a',
                                        'close_2': 'bid.b'})
                for col in ('ask.a', 'ask.b'):
                    if col not in df.columns:
                        df[col] = df[col.replace('ask', 'bid')]

            # metrics ---------------------------------------------------
            period_angles, period_r2s = _period_stats(df)
            sigma_theta  = float(np.std(period_angles))
            r2_overall   = _linreg_stats(df['bid.a'].values,
                                         df['bid.b'].values)[2]
            mean_r2_per  = float(np.mean(period_r2s))

            if np.isnan(r2_overall) or np.isnan(mean_r2_per):
                print(f"⚠️  insufficient data for {asset1}-{asset2}")
                continue

            score = (sigma_theta / 90.0) + (1.0 - r2_overall) + (1.0 - mean_r2_per)
            records.append((score, sigma_theta, r2_overall,
                            (asset1, asset2), df))

        except FileNotFoundError:
            print(f"⚠️  data file not found for {asset1}-{asset2}")
        except Exception as e:
            print(f"⚠️  error processing {asset1}-{asset2}: {repr(e)}")

    if not records:
        print("No valid pairs to process.")
        return

    records.sort(key=lambda rec: rec[0])   # lower score = better

    # build the PDF ---------------------------------------------------------
    with PdfPages(out_pdf) as pdf:
        for score, sigma_theta, r2_overall, (a1, a2), df in records:
            fig = make_signal_angle_figure(df, pair_lbl=f"{a1} – {a2}")
            pdf.savefig(fig)
            plt.close(fig)
            print(f"✓ {a1}-{a2}  "
                  f"(score = {score:.3f}, σθ = {sigma_theta:.2f}°, "
                  f"R² = {r2_overall:.3f})")

    print(f"\n📄  Multi-page PDF saved to: {out_pdf.resolve()}")


# ──────────────────────────────────────────────────────────────────────────────
#  Example usage
# ──────────────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    # Example list of tuples – replace or generate as you like
    example_pairs = [
        ('LINKUSDT', 'LTCUSDT'),
    ]
    loop_over_pairs(example_pairs)

#!/usr/bin/env python3
"""
Signal-angle report generator (Binance spread pairs)

• Reads *.pkl dataframes in ./common/data/binance/spread
• Builds a 4-panel figure for each pair
• Collects all figures into one multi-page PDF
• Ranks pairs by a composite score that favours
      – high goodness-of-fit (R² overall and per-period)
      – small dispersion of the three regression angles
"""

from pathlib import Path
import warnings
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb


# ───────────────────────── helper utilities ─────────────────────────
def _lighten(color: str, amount: float = 0.55):
    c = np.array(to_rgb(color))
    return tuple(c + (1.0 - c) * amount)


def _linreg_stats(x: np.ndarray, y: np.ndarray):
    slope, intercept = np.polyfit(x, y, 1)
    y_hat = intercept + slope * x
    ss_res = np.sum((y - y_hat) ** 2)
    ss_tot = np.sum((y - y.mean()) ** 2)
    r2 = 1.0 - ss_res / ss_tot if ss_tot else np.nan
    return float(slope), float(intercept), float(r2)


def _period_stats(df: pd.DataFrame):
    x, y = df['bid.a'].values, df['bid.b'].values
    thirds = np.array_split(np.arange(len(df)), 3)
    ang, r2s = [], []
    for idx in thirds:
        slope, _, r2 = _linreg_stats(x[idx], y[idx])
        ang.append(np.degrees(np.arctan(slope)))
        r2s.append(r2)
    return ang, r2s


# ─────────────────────── figure builder ─────────────────────────────
def make_signal_angle_figure(df: pd.DataFrame, pair_lbl: str) -> plt.Figure:
    """Return a 4-panel matplotlib Figure."""
    # 1. ensure DatetimeIndex
    if 'open_time' not in df.columns:
        raise KeyError("'open_time' column missing")
    if not pd.api.types.is_datetime64_any_dtype(df['open_time']):
        df = df.copy()
        df['open_time'] = pd.to_datetime(df['open_time'], errors='raise')
    df = df.set_index('open_time').sort_index()
    t = df.index

    # 2. price series & derived metrics
    mid_a = (df['bid.a'] + df['ask.a']) / 2
    mid_b = (df['bid.b'] + df['ask.b']) / 2
    perf_a = (mid_a / mid_a.iloc[0] - 1) * 100
    perf_b = (mid_b / mid_b.iloc[0] - 1) * 100
    sym_spread = 2 * (mid_a - mid_b) / (mid_a + mid_b)

    x_price, y_price = df['bid.a'].values, df['bid.b'].values
    g_slope, g_inter, g_r2 = _linreg_stats(x_price, y_price)
    g_angle = np.degrees(np.arctan(g_slope))

    thirds = np.array_split(np.arange(len(df)), 3)
    scat_cols = ['tab:blue', 'tab:red', 'tab:green']
    bg_cols = [_lighten(c, 0.85) for c in scat_cols]

    # 3. robust 10-day rolling angle (corr-based)
    windows_in_days = 7                    # KEEP your chosen horizon
    win = windows_in_days * 60 * 24        # minute data → rows
    roll_angle = np.full(len(df), np.nan)
    for i in range(win - 1, len(df)):
        xs = x_price[i - win + 1 : i + 1]
        ys = y_price[i - win + 1 : i + 1]
        sx, sy = xs.std(ddof=0), ys.std(ddof=0)
        if sx < 1e-6 or sy < 1e-6:
            continue
        r = np.corrcoef(xs, ys)[0, 1]
        beta = r * (sy / sx)
        roll_angle[i] = np.degrees(np.arctan(beta))

    # 4. figure scaffold
    fig, (ax0, ax1, ax2, ax3) = plt.subplots(
        4, 1, figsize=(11, 16),
        gridspec_kw={'hspace': .25},
        constrained_layout=True
    )
    ax1.sharex(ax0); ax2.sharex(ax0)

    fig.suptitle(f"{pair_lbl} — overall θ = {g_angle:.2f}°  (R² = {g_r2:.3f})",
                 fontsize=14, fontweight='bold')

    # Panel 1: cumulative returns
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    h1, = ax0.plot(t, perf_a, color='steelblue')
    h2, = ax0.plot(t, perf_b, color='darkorange')
    ax0.set_ylabel('Cum. return (%)')
    ax0.set_title('Cumulative % return (t₀ = 0 %)')
    ax0.legend(handles=[h1, h2], labels=['Leg-A', 'Leg-B'],
               frameon=False, loc='upper left')
    ax0.grid(True)

    # Panel 2: symmetric spread
    for i, idx in enumerate(thirds):
        ax1.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    ax1.plot(t, sym_spread, color='purple'); ax1.axhline(0, ls='--', lw=1, c='grey')
    pad = .02 * (sym_spread.max() - sym_spread.min())
    ax1.set_ylim(sym_spread.min() - pad, sym_spread.max() + pad)
    ax1.set_ylabel('2·(A−B)/(A+B)')
    ax1.set_title('Symmetric % spread'); ax1.grid(True)

    # Panel 3: rolling angle  + overall reference line (RED)
    ax2.plot(t, roll_angle, color='teal', label=f'{windows_in_days}-day angle')
    ax2.axhline(g_angle, color='red', ls='--', lw=1.5,
                label=f'overall θ = {g_angle:.2f}°')    # ← NEW LINE
    ax2.set_ylabel('θ (deg)')
    ax2.set_title(f'Rolling {windows_in_days}-day regression angle')
    ax2.grid(True)
    ax2.legend(frameon=False)

    # Panel 4: scatter + regressions
    order = np.argsort(x_price)
    ax3.scatter(x_price[order], y_price[order], s=3, alpha=.15, c='grey')
    ax3.plot(x_price[order], g_inter + g_slope * x_price[order],
             c='black', lw=2.5, label=f"Total θ={g_angle:.2f}°, R²={g_r2:.3f}")
    line_cols = [_lighten(c, .55) for c in scat_cols]
    lbls = ['Period 1', 'Period 2', 'Period 3']
    for i, idx in enumerate(thirds):
        xs, ys = x_price[idx], y_price[idx]
        if xs.std() < 1e-6:
            continue
        slope, inter, r2 = _linreg_stats(xs, ys)
        sidx = np.argsort(xs)
        ax3.plot(xs[sidx], inter + slope * xs[sidx],
                 c=line_cols[i], lw=2,
                 label=f"{lbls[i]}: θ={np.degrees(np.arctan(slope)):.2f}°, R²={r2:.3f}")
        ax3.scatter(xs, ys, s=6, alpha=.65, c=scat_cols[i])
    ax3.set_xlabel('bid.a'); ax3.set_ylabel('bid.b'); ax3.grid(True)
    ax3.legend(frameon=False)

    # date ticks
    locator = mdates.AutoDateLocator()
    fmt = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2):
        a.xaxis.set_major_locator(locator); a.xaxis.set_major_formatter(fmt)
    plt.setp(ax2.get_xticklabels(), rotation=45, ha='right')
    return fig


# ─────────────────────────── main driver ────────────────────────────
def loop_over_pairs(pairs):
    data_path = Path('./common/data/binance/spread')
    pdf_dir = data_path / 'reports'; pdf_dir.mkdir(parents=True, exist_ok=True)
    out_pdf = pdf_dir / 'signal_angles_report_ranked.pdf'

    records = []
    for a1, a2 in pairs:
        f = data_path / f'{a1}_{a2}_data.pkl'
        try:
            df = pd.read_pickle(f)
            if {'bid.a', 'bid.b'}.issubset(df.columns) is False:
                df = df.rename(columns={'close_1': 'bid.a', 'close_2': 'bid.b'})
                for col in ('ask.a', 'ask.b'):
                    if col not in df.columns:
                        df[col] = df[col.replace('ask', 'bid')]

            ang, r2s = _period_stats(df)
            sigma = np.std(ang); r2_o = _linreg_stats(df['bid.a'], df['bid.b'])[2]
            score = sigma / 90 + (1 - r2_o) + (1 - np.mean(r2s))
            records.append((score, sigma, r2_o, (a1, a2), df))
        except FileNotFoundError:
            print(f'⚠️  data file not found for {a1}-{a2}')

    if not records:
        print('No valid pairs to process.'); return
    records.sort(key=lambda r: r[0])

    with PdfPages(out_pdf) as pdf:
        for _, sigma, r2_o, (a1, a2), df in records:
            fig = make_signal_angle_figure(df, f'{a1} – {a2}')
            pdf.savefig(fig); plt.close(fig)
            print(f'✓ {a1}-{a2}  (σθ={sigma:.2f}°, R²={r2_o:.3f})')

    print(f'\n📄  PDF saved to: {out_pdf.resolve()}')


# ───────────────────────────── example ──────────────────────────────
if __name__ == '__main__':
    loop_over_pairs([('LINKUSDT', 'LTCUSDT')])

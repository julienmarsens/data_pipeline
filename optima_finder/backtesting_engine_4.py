#!/usr/bin/env python3
"""
Signal-angle report generator (Binance spread pairs)

• Reads *.pkl dataframes in ./common/data/binance/spread
• Builds a 4-panel figure for each pair
• Writes all figures into a multi-page PDF ranked by a composite score
"""

from pathlib import Path
import numpy as np, pandas as pd, matplotlib.pyplot as plt, matplotlib.dates as mdates
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb

# ───────────────── helper utilities ────────────────────────────────
def _lighten(c, amt=.55):
    rgb = np.array(to_rgb(c)); return tuple(rgb + (1-rgb)*amt)

def _linreg_stats(x, y):
    m, b = np.polyfit(x, y, 1)
    ss_res = ((y - (b + m*x))**2).sum(); ss_tot = ((y - y.mean())**2).sum()
    r2 = 1 - ss_res/ss_tot if ss_tot else np.nan
    return float(m), float(b), float(r2)

def _period_stats(df):
    x, y = df['bid.a'].values, df['bid.b'].values
    thirds = np.array_split(np.arange(len(df)), 3)
    ang, r2s = [], []
    for idx in thirds:
        m, _, r2 = _linreg_stats(x[idx], y[idx])
        ang.append(np.degrees(np.arctan(m))); r2s.append(r2)
    return ang, r2s

# ───────────────── figure builder ───────────────────────────────────
def make_signal_angle_figure(df: pd.DataFrame, pair_lbl: str,
                             thresh_deg: float = 10.0) -> plt.Figure:
    # 1 ── Date index
    if 'open_time' not in df.columns:
        raise KeyError("'open_time' column missing")
    if not pd.api.types.is_datetime64_any_dtype(df['open_time']):
        df = df.copy(); df['open_time'] = pd.to_datetime(df['open_time'])
    df = df.set_index('open_time').sort_index(); t = df.index

    # 2 ── Price series & stats
    mid_a = (df['bid.a'] + df['ask.a']) / 2
    mid_b = (df['bid.b'] + df['ask.b']) / 2
    perf_a = (mid_a / mid_a.iloc[0] - 1) * 100
    perf_b = (mid_b / mid_b.iloc[0] - 1) * 100
    sym_spread = 2 * (mid_a - mid_b) / (mid_a + mid_b)

    x, y = df['bid.a'].values, df['bid.b'].values
    g_m, g_b, g_r2 = _linreg_stats(x, y)
    g_theta = np.degrees(np.arctan(g_m))

    thirds = np.array_split(np.arange(len(df)), 3)
    scat_cols = ['tab:blue', 'tab:red', 'tab:green']
    bg_cols   = [_lighten(c, .85) for c in scat_cols]

    # 3 ── 10-day (≈14 400 min) rolling angle
    win = 10 * 24 * 60                      # minute bars
    roll_angle = np.full(len(df), np.nan)
    for i in range(win - 1, len(df)):
        xs, ys = x[i - win + 1:i + 1], y[i - win + 1:i + 1]
        sx, sy = xs.std(ddof=0), ys.std(ddof=0)
        if sx < 1e-6 or sy < 1e-6:    # skip flat window
            continue
        r = np.corrcoef(xs, ys)[0, 1]; beta = r * sy / sx
        roll_angle[i] = np.degrees(np.arctan(beta))

    upper = g_theta + thresh_deg
    lower = g_theta - thresh_deg

    # Masks for shading
    mask_in  = (~np.isnan(roll_angle)) & (roll_angle >= lower) & (roll_angle <= upper)
    mask_out = (~np.isnan(roll_angle)) & ~mask_in

    # 4 ── Plot scaffold
    fig, (ax0, ax1, ax2, ax3) = plt.subplots(
        4, 1, figsize=(11, 16),
        gridspec_kw={'hspace': .25},
        constrained_layout=True
    )
    ax1.sharex(ax0); ax2.sharex(ax0)

    fig.suptitle(f"{pair_lbl} — overall θ = {g_theta:.2f}°  (R² = {g_r2:.3f})",
                 fontweight='bold')

    # Panel 1 — Cumulative return
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    h1, = ax0.plot(t, perf_a, c='steelblue')
    h2, = ax0.plot(t, perf_b, c='darkorange')
    ax0.set_ylabel('Cum. return (%)')
    ax0.set_title('Cumulative return (t₀ = 0 %)')
    ax0.legend([h1, h2], ['Leg-A', 'Leg-B'], frameon=False, loc='upper left')
    ax0.grid(True)

    # Panel 2 — Symmetric spread
    for i, idx in enumerate(thirds):
        ax1.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    ax1.plot(t, sym_spread, c='purple'); ax1.axhline(0, ls='--', lw=1, c='grey')
    pad = .02 * (sym_spread.max() - sym_spread.min())
    ax1.set_ylim(sym_spread.min() - pad, sym_spread.max() + pad)
    ax1.set_ylabel('2·(A−B)/(A+B)')
    ax1.set_title('Symmetric % spread'); ax1.grid(True)

    # Panel 3 — Rolling angle + absolute ±bands
    ax2.plot(t, roll_angle, c='teal', label='10-day angle')
    ax2.axhline(g_theta, c='red', ls='--', lw=1.5,
                label=f'overall θ = {g_theta:.2f}°')
    ax2.axhline(upper, c='red', ls=':', lw=1)
    ax2.axhline(lower, c='red', ls=':', lw=1)

    # Green = inside band, Red = outside
    for mask, color in ((mask_in, 'green'), (mask_out, 'red')):
        if mask.any():
            idx = np.where(mask)[0]
            for blk in np.split(idx, np.where(np.diff(idx) != 1)[0] + 1):
                ax2.axvspan(t[blk[0]], t[blk[-1]],
                            facecolor=color, alpha=.15, zorder=0)

    ax2.set_ylabel('θ (deg)')
    ax2.set_title(f'Rolling 10-day angle ±{thresh_deg}° band')
    ax2.grid(True); ax2.legend(frameon=False)

    # Panel 4 — Scatter & regressions
    ord_ = np.argsort(x)
    ax3.scatter(x[ord_], y[ord_], s=3, alpha=.15, c='grey')
    ax3.plot(x[ord_], g_b + g_m * x[ord_], c='black', lw=2.5,
             label=f'Total θ = {g_theta:.2f}°')
    line_cols = [_lighten(c, .55) for c in scat_cols]
    lbls = ['P1', 'P2', 'P3']
    for i, idx in enumerate(thirds):
        xs, ys = x[idx], y[idx]
        if xs.std() < 1e-6:
            continue
        m, b, _ = _linreg_stats(xs, ys); sidx = np.argsort(xs)
        ax3.plot(xs[sidx], b + m * xs[sidx], c=line_cols[i], lw=2,
                 label=f'{lbls[i]} θ = {np.degrees(np.arctan(m)):.2f}°')
        ax3.scatter(xs, ys, s=6, alpha=.6, c=scat_cols[i])
    ax3.set_xlabel('bid.a'); ax3.set_ylabel('bid.b')
    ax3.grid(True); ax3.legend(frameon=False)

    # Date ticks
    locator = mdates.AutoDateLocator(); fmt = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2):
        a.xaxis.set_major_locator(locator); a.xaxis.set_major_formatter(fmt)
    plt.setp(ax2.get_xticklabels(), rotation=45, ha='right')
    return fig

# ───────────────── main driver ─────────────────────────────────────
def loop_over_pairs(pairs):
    data_path = Path('./common/data/binance/spread')
    pdf_dir   = data_path / 'reports'; pdf_dir.mkdir(parents=True, exist_ok=True)
    out_pdf   = pdf_dir / 'signal_angles_report_ranked.pdf'

    rec = []
    for a1, a2 in pairs:
        f = data_path / f'{a1}_{a2}_data.pkl'
        try:
            df = pd.read_pickle(f)
            if {'bid.a', 'bid.b'}.issubset(df.columns) is False:
                df = df.rename(columns={'close_1': 'bid.a',
                                        'close_2': 'bid.b'})
                for col in ('ask.a', 'ask.b'):
                    if col not in df.columns:
                        df[col] = df[col.replace('ask', 'bid')]
            ang, r2s = _period_stats(df)
            sigma = np.std(ang); r2o = _linreg_stats(df['bid.a'], df['bid.b'])[2]
            score = sigma / 90 + (1 - r2o) + (1 - np.mean(r2s))
            rec.append((score, sigma, r2o, (a1, a2), df))
        except FileNotFoundError:
            print(f'⚠️  missing {a1}-{a2}')

    if not rec:
        print('No valid pairs'); return
    rec.sort(key=lambda r: r[0])

    with PdfPages(out_pdf) as pdf:
        for _, sigma, r2o, (a1, a2), df in rec:
            pdf.savefig(make_signal_angle_figure(df, f'{a1} – {a2}', thresh_deg=10))
            plt.close()
            print(f'✓ {a1}-{a2}  (σθ={sigma:.2f}°, R²={r2o:.3f})')
    print(f'\n📄  PDF saved to: {out_pdf.resolve()}')

if __name__ == '__main__':
    loop_over_pairs([('ADAUSDT', 'DOGEUSDT')])

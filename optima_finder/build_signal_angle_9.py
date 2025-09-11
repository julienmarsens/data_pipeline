#!/usr/bin/env python3

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

def _parse_date_like(x):
    if x is None or (isinstance(x, float) and np.isnan(x)):
        return pd.NaT
    if isinstance(x, (pd.Timestamp, np.datetime64)):
        return pd.to_datetime(x)
    s = str(x).strip().replace('_', '-').replace('/', '-')
    if len(s) == 8 and s.isdigit():
        s = f"{s[:4]}-{s[4:6]}-{s[6:]}"
    return pd.to_datetime(s, errors='coerce')

def _parse_fit_range(fr):
    if fr is None:
        return (pd.NaT, pd.NaT)
    try:
        a, b = fr
    except Exception:
        return (pd.NaT, pd.NaT)
    return _parse_date_like(a), _parse_date_like(b)

def _robust_volatility(series: pd.Series, halflife: int = 240) -> pd.Series:
    r = series.diff()
    return np.sqrt(np.pi / 2.0) * r.abs().ewm(halflife=int(halflife), adjust=False).mean()

# ───────────────── figure builder ───────────────────────────────────
def make_signal_angle_figure(
    df: pd.DataFrame,
    pair_lbl: str,
    rolling_angle_windows_in_days: int = 10,
    vol_window_minutes: int = 60,
    thresh_deg: float = 4.0,
    highlight=(pd.NaT, pd.NaT)
) -> plt.Figure:
    if 'open_time' not in df.columns:
        raise KeyError("'open_time' column missing")
    if not pd.api.types.is_datetime64_any_dtype(df['open_time']):
        df = df.copy(); df['open_time'] = pd.to_datetime(df['open_time'])
    df = df.set_index('open_time').sort_index(); t = df.index

    mid_a = (df['bid.a'] + df['ask.a']) / 2
    mid_b = (df['bid.b'] + df['ask.b']) / 2
    perf_a = (mid_a / mid_a.iloc[0] - 1) * 100
    perf_b = (mid_b / mid_b.iloc[0] - 1) * 100
    sym_spread = 2 * (mid_a - mid_b) / (mid_a + mid_b)

    spread_vol = _robust_volatility(sym_spread, halflife=vol_window_minutes)

    x, y = df['bid.a'].values, df['bid.b'].values
    g_m, g_b, g_r2 = _linreg_stats(x, y)
    g_theta = np.degrees(np.arctan(g_m))

    thirds = np.array_split(np.arange(len(df)), 3)
    scat_cols = ['tab:blue', 'tab:red', 'tab:green']
    bg_cols   = [_lighten(c, .85) for c in scat_cols]

    win = rolling_angle_windows_in_days * 24 * 60
    roll_angle = np.full(len(df), np.nan)
    for i in range(win - 1, len(df)):
        xs, ys = x[i - win + 1:i + 1], y[i - win + 1:i + 1]
        sx, sy = xs.std(ddof=0), ys.std(ddof=0)
        if sx < 1e-6 or sy < 1e-6:
            continue
        r = np.corrcoef(xs, ys)[0, 1]; beta = r * sy / sx
        roll_angle[i] = np.degrees(np.arctan(beta))

    valid_angles = roll_angle[~np.isnan(roll_angle)]
    if valid_angles.size:
        q10, q90 = np.percentile(valid_angles, [10, 90])
    else:
        q10, q90 = np.nan, np.nan

    if np.isnan(q10) or np.isnan(q90):
        mask_in  = np.zeros(len(roll_angle), dtype=bool)
        mask_out = np.zeros(len(roll_angle), dtype=bool)
    else:
        mask_in  = (~np.isnan(roll_angle)) & (roll_angle >= q10) & (roll_angle <= q90)
        mask_out = (~np.isnan(roll_angle)) & ~mask_in

    fig, (ax0, ax1, ax2, ax3) = plt.subplots(
        4, 1, figsize=(11, 16),
        gridspec_kw={'hspace': .25},
        constrained_layout=True
    )
    ax1.sharex(ax0); ax2.sharex(ax0)

    fig.suptitle(f"{pair_lbl} — overall θ = {g_theta:.2f}°  (R² = {g_r2:.3f})",
                 fontweight='bold')

    # Panel 1
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    ax0.plot(t, perf_a, c='steelblue', label="Leg-A")
    ax0.plot(t, perf_b, c='darkorange', label="Leg-B")
    ax0.set_ylabel('Cum. return (%)')
    ax0.set_title('Cumulative return (t₀ = 0 %)')
    ax0.grid(True); ax0.legend(frameon=False, loc="upper left")

    # Panel 2
    for i, idx in enumerate(thirds):
        ax1.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)

    l1, = ax1.plot(t, sym_spread, c='purple', label='Symmetric % spread')
    ax1.axhline(0, ls='--', lw=1, c='grey')

    pad = .02 * (sym_spread.max() - sym_spread.min()) if len(sym_spread) else 0
    ax1.set_ylim(sym_spread.min() - pad, sym_spread.max() + pad)

    ax1.set_ylabel('2·(A−B)/(A+B)')
    ax1.set_title('Symmetric % spread + robust volatility'); ax1.grid(True)

    ax1b = ax1.twinx()
    l2, = ax1b.plot(t, spread_vol, c='black', alpha=.75,
                    label=f'Volatility (EWMA |Δspread|, halflife={vol_window_minutes}m)')
    ax1b.set_ylabel('Spread σ (robust EWMA)')
    ax1.legend([l1, l2], [l1.get_label(), l2.get_label()], frameon=False, loc='upper left')

    # Panel 3
    ax2.plot(t, roll_angle, c='teal', label=f'{rolling_angle_windows_in_days}-day angle')
    ax2.axhline(g_theta, c='red', ls='--', lw=1.5, label=f'overall θ={g_theta:.2f}°')
    if not np.isnan(q10): ax2.axhline(q10, c='red', ls=':', lw=1, label=f'P10={q10:.2f}°')
    if not np.isnan(q90): ax2.axhline(q90, c='red', ls=':', lw=1, label=f'P90={q90:.2f}°')
    for mask, color in ((mask_in, 'green'), (mask_out, 'red')):
        if mask.any():
            idx = np.where(mask)[0]
            for blk in np.split(idx, np.where(np.diff(idx) != 1)[0] + 1):
                ax2.axvspan(t[blk[0]], t[blk[-1]], facecolor=color, alpha=.15, zorder=0)
    ax2.set_ylabel('θ (deg)')
    ax2.set_title(f'Rolling {rolling_angle_windows_in_days}-day angle with P10/P90 bands')
    ax2.grid(True); ax2.legend(frameon=False)

    # Panel 4
    ord_ = np.argsort(x)
    ax3.scatter(x[ord_], y[ord_], s=3, alpha=.15, c='grey')
    ax3.plot(x[ord_], g_b + g_m * x[ord_], c='black', lw=2.5,
             label=f'Total θ={g_theta:.2f}°')

    line_cols = [_lighten(c, .55) for c in scat_cols]
    lbls = ['P1', 'P2', 'P3']
    for i, idx in enumerate(thirds):
        xs, ys = x[idx], y[idx]
        if xs.std() < 1e-6: continue
        m, b, _ = _linreg_stats(xs, ys); sidx = np.argsort(xs)
        ax3.plot(xs[sidx], b + m * xs[sidx], c=line_cols[i], lw=2,
                 label=f'{lbls[i]} θ={np.degrees(np.arctan(m)):.2f}°')
        ax3.scatter(xs, ys, s=6, alpha=.6, c=scat_cols[i])
    ax3.set_xlabel('bid.a'); ax3.set_ylabel('bid.b')
    ax3.grid(True); ax3.legend(frameon=False)

    locator = mdates.AutoDateLocator(); fmt = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2):
        a.xaxis.set_major_locator(locator); a.xaxis.set_major_formatter(fmt)
    plt.setp(ax2.get_xticklabels(), rotation=45, ha='right')
    return fig

# ───────────────── main driver ─────────────────────────────────────
def loop_over_pairs(pairs,
                    rolling_angle_windows_in_days: int = 10,
                    vol_window_minutes: int = 60,
                    fitting_dates=None,
                    top_n: int = 30):
    data_path = Path('./optima_finder/local_data/spread_data')
    pdf_dir = Path('./optima_finder/results')
    out_pdf   = pdf_dir / 'signal_angles_report_ranked.pdf'
    out_yml   = pdf_dir / 'optima_finder_pairs.yml'

    fit_map = {}
    if fitting_dates is not None:
        for i, p in enumerate(pairs):
            if i < len(fitting_dates):
                h0, h1 = _parse_fit_range(fitting_dates[i])
                a1, a2 = p
                fit_map[(str(a1), str(a2))] = (h0, h1)

    rec = []
    for a1, a2 in pairs:
        a1, a2 = str(a1), str(a2)
        f = data_path / f'{a1}_{a2}_data.pkl'
        try:
            df = pd.read_pickle(f)
            if {'bid.a', 'bid.b'}.issubset(df.columns) is False:
                df = df.rename(columns={'close_1': 'bid.a','close_2': 'bid.b'})
                for col in ('ask.a', 'ask.b'):
                    if col not in df.columns:
                        df[col] = df[col.replace('ask', 'bid')]
            ang, r2s = _period_stats(df)
            sigma = np.std(ang); r2o = _linreg_stats(df['bid.a'], df['bid.b'])[2]
            score = sigma / 90 + (1 - r2o) + (1 - np.mean(r2s))
            highlight = fit_map.get((a1, a2), (pd.NaT, pd.NaT))
            rec.append((score, (a1, a2), df, highlight))
        except FileNotFoundError:
            print(f'⚠️  missing {a1}-{a2}')

    if not rec:
        print('No valid pairs'); return
    rec.sort(key=lambda r: r[0])
    rec = rec[:top_n]

    pairs_out = []
    with PdfPages(out_pdf) as pdf:
        for score, (a1, a2), df, highlight in rec:
            fig = make_signal_angle_figure(
                df,
                f'{a1} – {a2}',
                rolling_angle_windows_in_days=rolling_angle_windows_in_days,
                vol_window_minutes=vol_window_minutes,
                thresh_deg=10,
                highlight=highlight
            )
            pdf.savefig(fig); plt.close(fig)

            pairs_out.append([a1, a2])
            print(f'✓ {a1}-{a2}')

    with open(out_yml, "w") as f:
        f.write("pairs: " + str(pairs_out) + "\n")

    print(f'\n📄  PDF saved to: {out_pdf.resolve()}')
    print(f'📊  Pairs YAML saved to: {out_yml.resolve()}')

# Example direct run
if __name__ == '__main__':
    loop_over_pairs([('ADAUSDT', 'DOGEUSDT')], rolling_angle_windows_in_days=10, vol_window_minutes=120)

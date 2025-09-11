#!/usr/bin/env python3
"""
Signal-angle report generator (Binance spread pairs)

• Reads *.pkl dataframes in ./common/data/binance/spread
• Builds a multi-panel figure for each pair
• Writes all figures into a multi-page PDF ranked by a composite score
• Optionally highlights a per-pair fitting date range on time-based panels
• Plots the angle computed on the fitting period as a blue line on the rolling-angle chart
• Overlays event-based backtest PnL (from CSV) on the same chart as live PnL
"""

from pathlib import Path
import numpy as np, pandas as pd, matplotlib.pyplot as plt, matplotlib.dates as mdates
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb


# ───────────────── helper utilities ────────────────────────────────
def _lighten(c, amt=.55):
    rgb = np.array(to_rgb(c))
    return tuple(rgb + (1 - rgb) * amt)


def _linreg_stats(x, y):
    m, b = np.polyfit(x, y, 1)
    ss_res = ((y - (b + m * x)) ** 2).sum()
    ss_tot = ((y - y.mean()) ** 2).sum()
    r2 = 1 - ss_res / ss_tot if ss_tot else np.nan
    return float(m), float(b), float(r2)


def _period_stats(df):
    x, y = df['bid.a'].values, df['bid.b'].values
    thirds = np.array_split(np.arange(len(df)), 3)
    ang, r2s = [], []
    for idx in thirds:
        m, _, r2 = _linreg_stats(x[idx], y[idx])
        ang.append(np.degrees(np.arctan(m)))
        r2s.append(r2)
    return ang, r2s


def _parse_date_like(x):
    if x is None or (isinstance(x, float) and np.isnan(x)):
        return pd.NaT
    if isinstance(x, (pd.Timestamp, np.datetime64)):
        return pd.to_datetime(x, utc=True)
    s = str(x).strip().replace('_', '-').replace('/', '-')
    if len(s) == 8 and s.isdigit():  # YYYYMMDD
        s = f"{s[:4]}-{s[4:6]}-{s[6:]}"
    return pd.to_datetime(s, errors='coerce', utc=True)


def _parse_fit_range(fr):
    if fr is None:
        return (pd.NaT, pd.NaT)
    try:
        a, b = fr
    except Exception:
        return (pd.NaT, pd.NaT)
    return _parse_date_like(a), _parse_date_like(b)


def _load_backtest_csv(backtest_dir: str, trader_id: str) -> pd.DataFrame | None:
    """Load event-based backtest PnL CSV for a trader.

    Expected CSV columns: 'time_iso', 'cum_pnl'.
    Returns a DataFrame indexed by UTC datetime with column 'pnl'.
    """
    cand = [
        Path(backtest_dir) / f"{trader_id}_backtest_OOS.csv",
        Path(backtest_dir) / f"{trader_id}_backtest_OOS",
    ]
    cand.extend(sorted(Path(backtest_dir).glob(f"*{trader_id}*backtest*events*.csv")))

    path = next((p for p in cand if p.exists()), None)
    if path is None:
        return None

    try:
        df = pd.read_csv(path)
    except Exception:
        return None

    required_cols = {"time_iso", "cum_pnl"}
    if not required_cols.issubset(df.columns):
        return None

    df = df.copy()
    df["time_iso"] = pd.to_datetime(df["time_iso"], utc=True)
    df = df.sort_values("time_iso").set_index("time_iso")

    df["cum_pnl"] = pd.to_numeric(df["cum_pnl"], errors="coerce")
    df = df.dropna(subset=["cum_pnl"])

    df["pnl"] = df["cum_pnl"]
    return df


# ───────────────── figure builder ───────────────────────────────────
def make_signal_angle_figure(
    df: pd.DataFrame,
    pair_lbl: str,
    rolling_angle_windows_in_days: int,
    thresh_deg: float = 4.0,
    highlight=(pd.NaT, pd.NaT),
    pnl_df: pd.DataFrame | None = None,
    inv_df: pd.DataFrame | None = None,
    trader_id: str | None = None,
    backtest_df: pd.DataFrame | None = None,
) -> plt.Figure:

    # 1 ── Market data
    if not pd.api.types.is_datetime64_any_dtype(df['open_time']):
        df = df.copy()
        df['open_time'] = pd.to_datetime(df['open_time'], utc=True)
    else:
        df = df.copy()
        df['open_time'] = pd.to_datetime(df['open_time'])
        if df['open_time'].dt.tz is None:
            df['open_time'] = df['open_time'].dt.tz_localize('UTC')
        else:
            df['open_time'] = df['open_time'].dt.tz_convert('UTC')

    df = df.set_index('open_time').sort_index()
    t = df.index

    # 2 ── Price series
    mid_a = (df['bid.a'] + df['ask.a']) / 2
    mid_b = (df['bid.b'] + df['ask.b']) / 2
    perf_a = (mid_a / mid_a.iloc[0] - 1) * 100
    perf_b = (mid_b / mid_b.iloc[0] - 1) * 100

    # Only log spread (bps)
    log_spread = np.log(mid_a) - np.log(mid_b)
    log_spread_bps = log_spread * 1e4

    # Global regression
    x, y = df['bid.a'].values, df['bid.b'].values
    g_m, g_b, g_r2 = _linreg_stats(x, y)
    g_theta = np.degrees(np.arctan(g_m))

    thirds = np.array_split(np.arange(len(df)), 3)
    scat_cols = ['tab:blue', 'tab:red', 'tab:green']
    bg_cols = [_lighten(c, .85) for c in scat_cols]

    # 3 ── Rolling angle
    win = rolling_angle_windows_in_days * 24 * 60
    roll_angle = np.full(len(df), np.nan)
    for i in range(win - 1, len(df)):
        xs, ys = x[i - win + 1:i + 1], y[i - win + 1:i + 1]
        sx, sy = xs.std(ddof=0), ys.std(ddof=0)
        if sx < 1e-6 or sy < 1e-6:
            continue
        r = np.corrcoef(xs, ys)[0, 1]
        beta = r * sy / sx
        roll_angle[i] = np.degrees(np.arctan(beta))

    valid_angles = roll_angle[~np.isnan(roll_angle)]
    q10, q90 = (np.percentile(valid_angles, [10, 90]) if valid_angles.size else (np.nan, np.nan))
    mask_in = (~np.isnan(roll_angle)) & (roll_angle >= q10) & (roll_angle <= q90) if not np.isnan(q10) else np.zeros(len(roll_angle), bool)
    mask_out = (~np.isnan(roll_angle)) & ~mask_in if not np.isnan(q10) else np.zeros(len(roll_angle), bool)

    # 4 ── Plot scaffold
    fig, (ax0, ax1, ax2_pnl, ax3_inv, ax4, ax5) = plt.subplots(
        6, 1, figsize=(11, 22), gridspec_kw={'hspace': .26}, constrained_layout=True
    )
    ax1.sharex(ax0); ax2_pnl.sharex(ax0); ax3_inv.sharex(ax0); ax4.sharex(ax0)

    fig.suptitle(f"{pair_lbl} — overall θ = {g_theta:.2f}°  (R² = {g_r2:.3f})", fontweight='bold')

    # Panel 1 — Cumulative return
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    ax0.plot(t, perf_a, c='steelblue', label='Leg-A')
    ax0.plot(t, perf_b, c='darkorange', label='Leg-B')
    ax0.set_ylabel('Cum. return (%)'); ax0.set_title('Cumulative return (t₀ = 0 %)'); ax0.legend(frameon=False, loc='upper left'); ax0.grid(True)

    # Panel 2 — Log spread + volatility (bps)
    for i, idx in enumerate(thirds):
        ax1.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)

    ax1.plot(t, log_spread_bps, c='purple', label='Log spread (bps)')
    ax1.axhline(0, ls='--', lw=1, c='grey')
    ax1.set_ylabel('Spread (bps)'); ax1.set_title('Log spread + rolling volatility (bps)'); ax1.grid(True)

    roll_vol_bps = pd.Series(log_spread_bps).rolling(min(len(df), max(1, win))).std(ddof=0)
    ax1b = ax1.twinx()
    ax1b.plot(t, roll_vol_bps, ls='--', label='Spread vol (rolling std, bps)')
    ax1b.set_ylabel('Vol (bps)')

    # Auto-fit y-limits
    spread_series = pd.Series(log_spread_bps).replace([np.inf, -np.inf], np.nan).dropna()
    if len(spread_series):
        smin, smax = float(spread_series.min()), float(spread_series.max())
        if smin == smax:
            pad = max(1.0, abs(smin) * 0.05)
            ax1.set_ylim(smin - pad, smax + pad)
        else:
            pad = (smax - smin) * 0.07
            ax1.set_ylim(smin - pad, smax + pad)

    vol_series = pd.Series(roll_vol_bps).replace([np.inf, -np.inf], np.nan).dropna()
    if len(vol_series):
        vmin, vmax = float(vol_series.min()), float(vol_series.max())
        if vmin == vmax:
            vpad = max(0.5, abs(vmin) * 0.1)
            ax1b.set_ylim(vmin - vpad, vmax + vpad)
        else:
            vpad = (vmax - vmin) * 0.10
            ax1b.set_ylim(vmin - vpad, vmax + vpad)

    lines, labels = ax1.get_legend_handles_labels()
    lines2, labels2 = ax1b.get_legend_handles_labels()
    ax1b.legend(lines + lines2, labels + labels2, frameon=False, loc='upper left')


    # Panel 3 — Trader PnL
    live_plotted = False
    if pnl_df is not None and {'time', 'PnL_usd'}.issubset(pnl_df.columns):
        pnl = pnl_df.copy()
        pnl['time'] = pd.to_datetime(pnl['time'], utc=True)
        pnl = pnl.sort_values('time').set_index('time')
        ax2_pnl.plot(pnl.index, pnl['PnL_usd'], lw=1.8, label='Live PnL (USD)')
        live_plotted = True

    if backtest_df is not None and not backtest_df.empty:
        series = backtest_df["pnl"].copy()
        series.index = series.index.floor('s')
        ax2_pnl.plot(series.index, series.values, lw=2.0, linestyle='--', c='black', label="Backtest PnL (event-based)")

    if not (live_plotted or (backtest_df is not None and not backtest_df.empty)):
        ax2_pnl.text(0.5, 0.5, 'PnL data not available', ha='center', va='center', transform=ax2_pnl.transAxes)

    ax2_pnl.set_title(f"Trader PnL{f' — {trader_id}' if trader_id else ''}")
    ax2_pnl.set_ylabel('PnL (USD)')
    ax2_pnl.grid(True)
    ax2_pnl.legend(frameon=False, loc='upper left')

    # Panel 4 — Trader Inventory
    if inv_df is not None and {'time', 'QuoteBalanceA_usd', 'QuoteBalanceB_usd'}.issubset(inv_df.columns):
        inv = inv_df.copy()
        inv['time'] = pd.to_datetime(inv['time'], utc=True)
        inv = inv.sort_values('time').set_index('time')
        ax3_inv.plot(inv.index, inv['QuoteBalanceA_usd'], lw=1.2, label='QuoteBalanceA (USD)')
        ax3_inv.plot(inv.index, inv['QuoteBalanceB_usd'], lw=1.2, label='QuoteBalanceB (USD)')
        ax3_inv.set_ylabel('Inventory (USD)')
        ax3_inv.set_title(f"Trader Inventory{f' — {trader_id}' if trader_id else ''}")
        ax3_inv.grid(True)
        ax3_inv.legend(frameon=False, loc='upper left')
    else:
        ax3_inv.text(0.5, 0.5, 'Inventory data not available', ha='center', va='center', transform=ax3_inv.transAxes)
        ax3_inv.set_title('Trader Inventory')
        ax3_inv.set_ylabel('Inventory (USD)')
        ax3_inv.grid(True)

    # Panel 5 — Rolling angle
    ax4.plot(t, roll_angle, c='teal', label='10-day angle')
    ax4.axhline(g_theta, c='red', ls='--', lw=1.5, label=f'overall θ = {g_theta:.2f}°')
    if not np.isnan(q10):
        ax4.axhline(q10, c='red', ls=':', lw=1, label=f'P10 = {q10:.2f}°')
    if not np.isnan(q90):
        ax4.axhline(q90, c='red', ls=':', lw=1, label=f'P90 = {q90:.2f}°')
    for mask, color in ((mask_in, 'green'), (mask_out, 'red')):
        if mask.any():
            idx = np.where(mask)[0]
            for blk in np.split(idx, np.where(np.diff(idx) != 1)[0] + 1):
                ax4.axvspan(t[blk[0]], t[blk[-1]], facecolor=color, alpha=.15, zorder=0)
    if pd.notna(highlight[0]) and pd.notna(highlight[1]):
        start, end = max(pd.Timestamp(highlight[0]), t.min()), min(pd.Timestamp(highlight[1]), t.max())
        if start < end:
            for ax in (ax0, ax1, ax2_pnl, ax3_inv, ax4):
                ax.axvspan(start, end, facecolor='gold', alpha=.32, zorder=0, ec='goldenrod', lw=0.8)
    ax4.set_ylabel('θ (deg)')
    ax4.set_title(f'Rolling {rolling_angle_windows_in_days}-day angle with P10/P90 bands')
    ax4.grid(True)
    ax4.legend(frameon=False)

    # Panel 6 — Scatter
    ord_ = np.argsort(x)
    ax5.scatter(x[ord_], y[ord_], s=3, alpha=.15, c='grey')
    ax5.plot(x[ord_], g_b + g_m * x[ord_], c='black', lw=2.5, label=f'Total θ = {g_theta:.2f}°')
    for i, idx in enumerate(thirds):
        xs, ys = x[idx], y[idx]
        if xs.std() < 1e-6:
            continue
        m, b, _ = _linreg_stats(xs, ys)
        sidx = np.argsort(xs)
        ax5.plot(xs[sidx], b + m * xs[sidx], c=_lighten(scat_cols[i], .55), lw=2,
                 label=f'P{i+1} θ = {np.degrees(np.arctan(m)):.2f}°')
        ax5.scatter(xs, ys, s=6, alpha=.6, c=scat_cols[i])
    ax5.set_xlabel('bid.a'); ax5.set_ylabel('bid.b'); ax5.grid(True); ax5.legend(frameon=False)

    # Date ticks
    locator = mdates.AutoDateLocator()
    fmt = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2_pnl, ax3_inv, ax4):
        a.xaxis.set_major_locator(locator)
        a.xaxis.set_major_formatter(fmt)
    plt.setp(ax4.get_xticklabels(), rotation=45, ha='right')

    return fig


# ───────────────── main driver ─────────────────────────────────────
def loop_over_pairs(pairs, trader_ids, rolling_angle_windows_in_days, fitting_dates=None):
    sync_data_path = './prod_report_module/local_data/sync_market_data'
    pnl_data_path = './prod_report_module/local_data/pnl'
    backtest_data_path = './prod_report_module/local_data/backtest'
    out_pdf_path   = './prod_report_module/reports/prod_report.pdf'

    fit_map = {}
    if fitting_dates is not None:
        for i, p in enumerate(pairs):
            if i < len(fitting_dates):
                h0, h1 = _parse_fit_range(fitting_dates[i]); a1, a2 = p
                fit_map[(str(a1), str(a2))] = (h0, h1)

    rec = []
    for i, (a1, a2) in enumerate(pairs):
        a1, a2 = str(a1), str(a2); f = f'{sync_data_path}/{a1}_{a2}_data.pkl'
        pnl_df, inv_df, backtest_df, trader_id = None, None, None, None
        if trader_ids is not None and i < len(trader_ids):
            trader_id = str(trader_ids[i]); pnl_path = f'{pnl_data_path}/{trader_id}_pnl_data.pkl'; inv_path = f'{pnl_data_path}/{trader_id}_inventory_data.pkl'
            try: pnl_df = pd.read_pickle(pnl_path)
            except FileNotFoundError: print(f'⚠️ missing PnL for {trader_id}')
            try: inv_df = pd.read_pickle(inv_path)
            except FileNotFoundError: print(f'⚠️ missing Inventory for {trader_id}')
            backtest_df = _load_backtest_csv(backtest_data_path, trader_id)
            if backtest_df is None: print(f'⚠️ missing Backtest CSV for {trader_id}')
        try:
            df = pd.read_pickle(f)
            if {'bid.a','bid.b'}.issubset(df.columns) is False:
                df = df.rename(columns={'close_1':'bid.a','close_2':'bid.b'})
                for col in ('ask.a','ask.b'):
                    if col not in df.columns: df[col]=df[col.replace('ask','bid')]
            ang, r2s = _period_stats(df); sigma = np.std(ang); r2o = _linreg_stats(df['bid.a'], df['bid.b'])[2]
            score = sigma/90 + (1-r2o) + (1-np.mean(r2s)); highlight = fit_map.get((a1,a2),(pd.NaT,pd.NaT))
            rec.append((score, sigma, r2o, (a1,a2), df, highlight, pnl_df, inv_df, trader_id, backtest_df))
        except FileNotFoundError: print(f'⚠️ missing {a1}-{a2}')

    if not rec:
        print('No valid pairs')
        return
    rec.sort(key=lambda r: r[0])

    with PdfPages(out_pdf_path) as pdf:
        for _, sigma, r2o, (a1,a2), df, highlight, pnl_df, inv_df, trader_id, backtest_df in rec:
            fig = make_signal_angle_figure(df, f'{a1} – {a2}', rolling_angle_windows_in_days,
                                           highlight=highlight, pnl_df=pnl_df, inv_df=inv_df,
                                           trader_id=trader_id, backtest_df=backtest_df)
            pdf.savefig(fig); plt.close(fig)
            print(f'✓ {a1}-{a2}  (σθ={sigma:.2f}°, R²={r2o:.3f})')
    print(f'\n📄  PDF saved to: {out_pdf_path}')


# Example run
if __name__ == '__main__':
    loop_over_pairs([('ADAUSDT','DOGEUSDT')], trader_ids=['trader_623'], rolling_angle_windows_in_days=10)

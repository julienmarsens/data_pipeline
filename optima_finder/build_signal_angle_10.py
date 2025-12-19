#!/usr/bin/env python3

import numpy as np, pandas as pd, matplotlib.pyplot as plt, matplotlib.dates as mdates

import os
from ruamel.yaml import YAML
from pathlib import Path


yaml = YAML(typ="rt")
yaml.preserve_quotes = True
yaml.width = 10 ** 6

def atomic_write_yaml(data, out_yml: Path):
    tmp_file = out_yml.with_suffix(out_yml.suffix + ".tmp")
    with open(tmp_file, "w") as f:
        yaml.dump(data, f)
        f.flush()             # flush Python buffer
        os.fsync(f.fileno())  # flush OS buffer
    tmp_file.replace(out_yml)  # atomic rename

# ───────────────── helper utilities ────────────────────────────────
def _lighten(c, amt=.55):
    rgb = np.array(to_rgb(c)); return tuple(rgb + (1-rgb)*amt)

def _linreg_stats(x, y):
    m, b = np.polyfit(x, y, 1)
    ss_res = ((y - (b + m*x))**2).sum(); ss_tot = ((y - y.mean())**2).sum()
    r2 = 1 - ss_res/ss_tot if ss_tot else np.nan
    return float(m), float(b), float(r2)

def _period_stats(df):
    x, y = df.iloc[:, 0].values, df.iloc[:, 1].values
    thirds = np.array_split(np.arange(len(df)), 3)
    ang, r2s = [], []
    for idx in thirds:
        if len(idx) < 10:
            continue
        m, _, r2 = _linreg_stats(x[idx], y[idx])
        ang.append(np.degrees(np.arctan(m)))
        r2s.append(r2)
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

    x = ((df['bid.a'] + df['ask.a']) / 2).values
    y = ((df['bid.b'] + df['ask.b']) / 2).values
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

def _angle_drift_penalty(roll_angle):
    ra = roll_angle[np.isfinite(roll_angle)]
    if len(ra) < 10:
        return 1.0
    dtheta = np.diff(ra)
    return np.median(np.abs(dtheta)) / 5.0  # degrees, normalized

def _spread_trend_penalty(sym_spread):
    if len(sym_spread) < 10:
        return 1.0

    t = np.arange(len(sym_spread))
    _, _, r2 = _linreg_stats(t, sym_spread.values)
    return abs(r2)

def _build_signal_vector_from_regression(mid_a: pd.Series, mid_b: pd.Series):
    # Fit mid_b ~ mid_a
    m, b, r2 = _linreg_stats(mid_a.values, mid_b.values)

    # Match R definitions
    slope_regression = m
    if not np.isfinite(slope_regression) or abs(slope_regression) < 1e-12:
        return None, (np.nan, np.nan, np.nan)

    base_direction = 1.0 / slope_regression
    theta = np.arctan(slope_regression)  # radians

    signal = np.array([np.cos(theta), -base_direction * np.sin(theta)], dtype=float)
    nrm = np.linalg.norm(signal)
    if not np.isfinite(nrm) or nrm < 1e-12:
        return None, (m, b, r2)

    nsv = signal / nrm  # normalized.signal.vector
    return nsv, (m, b, r2)


def _project_price(mid_a: pd.Series, mid_b: pd.Series, nsv: np.ndarray) -> pd.Series:
    # Mid-based proxy of your signal price projection
    return mid_a * nsv[0] + mid_b * nsv[1]


def _proj_trend_penalty(proj: pd.Series) -> float:
    # Penalize directional drift in projected price (this matches your failure mode)
    p = proj.values
    p = p[np.isfinite(p)]
    if len(p) < 10:
        return 1.0
    t = np.arange(len(p), dtype=float)
    _, _, r2 = _linreg_stats(t, p)
    # r2 close to 1 => strong linear drift => bad
    return float(abs(r2))


def fast_oscillation_score(p: pd.Series) -> float:
    # remove linear trend
    t = np.arange(len(p))
    m, b, _ = _linreg_stats(t, p.values)
    detrended = p.values - (m * t + b)

    s = np.sign(detrended)
    s[s == 0] = np.nan
    s = pd.Series(s).ffill().values

    crosses = np.sum(s[1:] * s[:-1] < 0)
    return crosses / max(1, len(p))


# ───────────────── main driver ─────────────────────────────────────
#!/usr/bin/env python3

from pathlib import Path
import numpy as np, pandas as pd, matplotlib.pyplot as plt, matplotlib.dates as mdates
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb


# ───────────────── main driver ─────────────────────────────────────
def loop_over_pairs(pairs,
                    rolling_angle_windows_in_days: int = 10,
                    vol_window_minutes: int = 60,
                    fitting_dates=None,
                    top_n: int = 30
                    ):

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
            # --- mid prices ---
            mid_a = (df['bid.a'] + df.get('ask.a', df['bid.a'])) / 2
            mid_b = (df['bid.b'] + df.get('ask.b', df['bid.b'])) / 2

            # --- build engine-aligned signal vector ---
            nsv, (m, b, r2o) = _build_signal_vector_from_regression(mid_a, mid_b)
            if nsv is None:
                continue  # unusable pair

            # --- angle stability (keep your thirds logic) ---
            ang, r2s = _period_stats(pd.DataFrame({'bid.a': mid_a, 'bid.b': mid_b}))
            sigma = np.std(ang) if len(ang) else 90.0

            # --- rolling angle (your current version is OK as a proxy) ---
            # (keep your roll_angle computation)
            # drift_penalty = _angle_drift_penalty(roll_angle)

            # --- projected price proxy ---
            proj = _project_price(mid_a, mid_b, nsv)

            # --- projected drift penalty (REPLACES spread trend) ---
            proj_trend_penalty = _proj_trend_penalty(proj)

            # --- oscillation reward (optional but recommended) ---
            osc = fast_oscillation_score(proj)

            """# --- rolling angle for drift ---
            win = rolling_angle_windows_in_days * 24 * 60
            roll_angle = np.full(len(df), np.nan)
            x, y = mid_a.values, mid_b.values

            for i in range(win - 1, len(df)):
                xs, ys = x[i - win + 1:i + 1], y[i - win + 1:i + 1]
                sx, sy = xs.std(ddof=0), ys.std(ddof=0)
                if sx < 1e-6 or sy < 1e-6:
                    continue
                r = np.corrcoef(xs, ys)[0, 1]
                beta = r * sy / sx
                roll_angle[i] = np.degrees(np.arctan(beta))

            drift_penalty = _angle_drift_penalty(roll_angle)"""

            # --- FINAL SCORE (lower is better) ---
            score = (
                    2.0 * sigma / 90.0 +  # dominant: angle stability
                    1.0 * (1 - np.nanmean(r2s)) +  # local stability
                    0.5 * (1 - r2o) +  # global fit quality
                    # 1.0 * drift_penalty +  # angle drift speed
                    1.0 * proj_trend_penalty -  # projected drift is bad
                    2.0 * osc  # oscillation is good (subtract)
            )

            highlight = fit_map.get((a1, a2), (pd.NaT, pd.NaT))
            rec.append((score, (a1, a2), df, highlight))
        except FileNotFoundError:
            print(f'⚠️  missing {a1}-{a2}')

    if not rec:
        print('No valid pairs'); return

    # sort by stability score
    rec.sort(key=lambda r: r[0])

    # Attach ranking index (1-based)
    rec = [(i+1, *r) for i, r in enumerate(rec)]

    # ── NEW FILTER: enforce uniqueness but guarantee top_n ─────────────
    selected = []
    asset_counts = {}

    # Pass 1: take unique pairs (each asset only once)
    for rank, score, (a1, a2), df, highlight in rec:
        if asset_counts.get(a1, 0) == 0 and asset_counts.get(a2, 0) == 0:
            selected.append((rank, score, (a1, a2), df, highlight))
            asset_counts[a1] = asset_counts.get(a1, 0) + 1
            asset_counts[a2] = asset_counts.get(a2, 0) + 1
        if len(selected) >= top_n:
            break

    # Pass 2: relax restriction if fewer than top_n
    if len(selected) < top_n:
        for rank, score, (a1, a2), df, highlight in rec:
            if (rank, score, (a1, a2), df, highlight) in selected:
                continue
            if asset_counts.get(a1, 0) < 2 and asset_counts.get(a2, 0) < 2:
                selected.append((rank, score, (a1, a2), df, highlight))
                asset_counts[a1] = asset_counts.get(a1, 0) + 1
                asset_counts[a2] = asset_counts.get(a2, 0) + 1
            if len(selected) >= top_n:
                break
        print(f"⚠️ Not enough unique pairs, filled up to {top_n} with duplicates (max 2 uses per asset).")

    rec = selected
    # ───────────────────────────────────────────────────────────────────

    pairs_out = []
    with PdfPages(out_pdf) as pdf:
        for rank, score, (a1, a2), df, highlight in rec:
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
            print(f'#{rank:02d} (score={score:.4f}) ✓ {a1}-{a2}')

    atomic_write_yaml({"pairs": pairs_out}, out_yml)
    print(f"\n📄 PDF saved to: {out_pdf.resolve()}")
    print(f"📊 Pairs YAML saved to: {out_yml.resolve()}")

# Example direct run
if __name__ == '__main__':
    loop_over_pairs([('ADAUSDT', 'DOGEUSDT')], rolling_angle_windows_in_days=10, vol_window_minutes=120)

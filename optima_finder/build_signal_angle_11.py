#!/usr/bin/env python3

from pathlib import Path
import numpy as np, pandas as pd, matplotlib.pyplot as plt, matplotlib.dates as mdates
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb

import os
from ruamel.yaml import YAML

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
    if fr is None: return (pd.NaT, pd.NaT)
    try: a, b = fr
    except Exception: return (pd.NaT, pd.NaT)
    return _parse_date_like(a), _parse_date_like(b)

def _robust_volatility(series: pd.Series, halflife: int = 240) -> pd.Series:
    r = series.diff()
    return np.sqrt(np.pi / 2.0) * r.abs().ewm(halflife=int(halflife), adjust=False).mean()


# ───── regression-based mean reversion helpers ─────
def _ols_residuals_beta(x: pd.Series, y: pd.Series):
    X = np.vstack([np.ones(len(x)), x]).T
    beta = np.linalg.lstsq(X, y, rcond=None)[0]  # [alpha, beta]
    resid = y - X @ beta
    return resid, beta[1]

def _ar1_on_resid(resid: np.ndarray):
    r = pd.Series(resid).dropna()
    if len(r) < 200:
        return np.nan, np.nan, np.nan, np.nan
    r0, r1 = r.shift(1).dropna(), r.dropna()
    r1, r0 = r1.align(r0, join="inner")
    phi = np.corrcoef(r1.values, r0.values)[0,1]
    phi = float(np.clip(phi, -0.999999, 0.999999))
    lam = -np.log(phi if phi > 0 else 1e-6)
    t_half = np.log(2)/lam if lam > 0 else np.inf
    sigma_eta = np.std(r1.values - phi*r0.values, ddof=1)
    dw = np.sum(np.diff(r.values)**2) / np.sum(r.values**2)
    return phi, lam, t_half, sigma_eta, dw

def _rolling_beta_R2(x: pd.Series, y: pd.Series, win=1440):
    betas, r2s = [], []
    if len(x) < win*2: return np.nan, np.nan
    for i in range(win, len(x)+1, win):
        Xw, Yw = x.iloc[i-win:i].values, y.iloc[i-win:i].values
        X = np.vstack([np.ones(win), Xw]).T
        coef = np.linalg.lstsq(X, Yw, rcond=None)[0]
        yhat = X @ coef
        ss_res = ((Yw - yhat)**2).sum()
        ss_tot = ((Yw - Yw.mean())**2).sum()
        r2 = 1 - ss_res/ss_tot if ss_tot > 0 else np.nan
        betas.append(coef[1]); r2s.append(r2)
    betas, r2s = np.array(betas, float), np.array(r2s, float)
    return np.nanvar(betas), np.nanmedian(r2s)

def _banded_crossings_per_day(resid: np.ndarray, day_len=1440, band_mult=0.5):
    s = pd.Series(resid).dropna()
    if len(s) < day_len: return np.nan
    center = s.rolling(day_len, min_periods=max(30, day_len//10)).median()
    band = band_mult * (s - center).abs().rolling(day_len, min_periods=max(30, day_len//10)).median()
    band = band.replace(0, np.nan).fillna(band.median() if not np.isnan(band.median()) else 1e-6)
    sign_banded = np.where(s > band, 1, np.where(s < -band, -1, 0))
    sb = pd.Series(sign_banded)
    crosses = ((sb.shift(1) != sb) & (sb != 0) & (sb.shift(1) != 0)).sum()
    days = max(1, len(s)//day_len)
    return crosses / days

def regression_mean_reversion_score(df: pd.DataFrame, target_thalf=120, target_cross=10):
    x, y = df['bid.a'], df['bid.b']
    resid, beta = _ols_residuals_beta(x, y)
    phi, lam, t_half, sigma_eta, dw = _ar1_on_resid(resid)

    # Mean Absolute Deviation (manual, no deprecation warning)
    mad_resid = np.mean(np.abs(resid - np.mean(resid)))

    noise_cleanliness = sigma_eta / (mad_resid if mad_resid > 0 else 1e-6)
    beta_var, r2_med = _rolling_beta_R2(x, y, win=1440)
    cross_per_day = _banded_crossings_per_day(resid, day_len=1440)

    th_pen = np.log1p(abs((t_half - target_thalf) / max(1.0, target_thalf))) if not np.isnan(t_half) else 1.0
    cr_pen = np.log1p(abs((cross_per_day - target_cross) / max(1.0, target_cross))) if not np.isnan(cross_per_day) else 1.0

    score = (
        1.0*th_pen +
        1.0*cr_pen +
        1.0*noise_cleanliness +
        1.0*(beta_var if not np.isnan(beta_var) else 1.0) -
        1.0*(r2_med if not np.isnan(r2_med) else 0.0)
    )
    return {
        "score": float(score),
        "beta": beta,
        "phi": phi,
        "t_half": t_half,
        "dw": dw,
        "noise_cleanliness": noise_cleanliness,
        "beta_var": beta_var,
        "r2_med": r2_med,
        "cross_per_day": cross_per_day
    }


# ───────────────── figure builder ───────────────────────────────────
def make_signal_angle_figure(
    df: pd.DataFrame,
    pair_lbl: str,
    metrics: dict,
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
        if sx < 1e-6 or sy < 1e-6: continue
        r = np.corrcoef(xs, ys)[0, 1]; beta = r * sy / sx
        roll_angle[i] = np.degrees(np.arctan(beta))

    valid_angles = roll_angle[~np.isnan(roll_angle)]
    if valid_angles.size:
        q10, q90 = np.percentile(valid_angles, [10, 90])
    else:
        q10, q90 = np.nan, np.nan

    fig, axes = plt.subplots(
        5, 1, figsize=(11, 18),
        gridspec_kw={'hspace': .35},
        constrained_layout=True
    )
    ax0, ax1, ax2, ax3, ax4 = axes
    ax1.sharex(ax0); ax2.sharex(ax0)

    fig.suptitle(f"{pair_lbl}\nOverall θ = {g_theta:.2f}° (R²={g_r2:.3f})",
                 fontweight='bold')

    # Panel 1 – perf
    for i, idx in enumerate(thirds):
        ax0.axvspan(t[idx[0]], t[idx[-1]], facecolor=bg_cols[i], alpha=.35)
    ax0.plot(t, perf_a, c='steelblue', label="Leg-A")
    ax0.plot(t, perf_b, c='darkorange', label="Leg-B")
    ax0.set_ylabel('Cum. return (%)')
    ax0.set_title('Cumulative return (t₀ = 0%)')
    ax0.grid(True); ax0.legend(frameon=False, loc="upper left")

    # Panel 2 – spread + vol
    ax1.plot(t, sym_spread, c='purple', label='Symmetric % spread')
    ax1.axhline(0, ls='--', lw=1, c='grey')
    ax1b = ax1.twinx()
    l2, = ax1b.plot(t, spread_vol, c='black', alpha=.75,
                    label=f'Volatility (EWMA |Δspread|, hl={vol_window_minutes}m)')
    ax1.set_ylabel('2·(A−B)/(A+B)'); ax1b.set_ylabel('Spread σ')
    ax1.set_title('Spread + robust volatility'); ax1.grid(True)
    ax1.legend([l2], [l2.get_label()], frameon=False, loc='upper left')

    # Panel 3 – rolling angle
    ax2.plot(t, roll_angle, c='teal', label=f'{rolling_angle_windows_in_days}-day angle')
    ax2.axhline(g_theta, c='red', ls='--', lw=1.5, label=f'overall θ={g_theta:.2f}°')
    if not np.isnan(q10): ax2.axhline(q10, c='red', ls=':', lw=1, label=f'P10={q10:.2f}°')
    if not np.isnan(q90): ax2.axhline(q90, c='red', ls=':', lw=1, label=f'P90={q90:.2f}°')
    ax2.set_ylabel('θ (deg)'); ax2.set_title('Rolling regression angle'); ax2.grid(True)
    ax2.legend(frameon=False)

    # Panel 4 – scatter + regression lines
    ord_ = np.argsort(x)
    ax3.scatter(x[ord_], y[ord_], s=3, alpha=.15, c='grey')
    ax3.plot(x[ord_], g_b + g_m * x[ord_], c='black', lw=2.5,
             label=f'Total θ={g_theta:.2f}°')
    ax3.set_xlabel('bid.a'); ax3.set_ylabel('bid.b'); ax3.grid(True)
    ax3.legend(frameon=False)

    # Panel 5 – metrics text
    ax4.axis("off")
    txt = (
        f"β = {metrics['beta']:.4f}\n"
        f"Half-life = {metrics['t_half']:.1f} min\n"
        f"Median R² = {metrics['r2_med']:.3f}\n"
        f"Crossings/day = {metrics['cross_per_day']:.1f}\n"
        f"Noise/cleanliness = {metrics['noise_cleanliness']:.3f}\n"
        f"Score = {metrics['score']:.4f}"
    )
    ax4.text(0, 0.5, txt, fontsize=12, va="center", ha="left")

    locator = mdates.AutoDateLocator(); fmt = mdates.ConciseDateFormatter(locator)
    for a in (ax0, ax1, ax2): a.xaxis.set_major_locator(locator); a.xaxis.set_major_formatter(fmt)
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
    out_pdf = pdf_dir / 'signal_angles_report_ranked.pdf'
    out_yml = pdf_dir / 'optima_finder_pairs.yml'

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
                    if col not in df.columns: df[col] = df[col.replace('ask', 'bid')]

            metrics = regression_mean_reversion_score(df)
            score = metrics["score"]
            highlight = fit_map.get((a1, a2), (pd.NaT, pd.NaT))
            rec.append((score, (a1, a2), df, highlight, metrics))

            print(f"[{a1}-{a2}] β={metrics['beta']:.4f}, t½={metrics['t_half']:.1f}m, "
                  f"R²~{metrics['r2_med']:.3f}, cross/day={metrics['cross_per_day']:.1f}, "
                  f"noise={metrics['noise_cleanliness']:.3f}, score={score:.4f}")

        except FileNotFoundError:
            print(f"⚠️ missing {a1}-{a2}")

    if not rec:
        print("No valid pairs"); return

    rec.sort(key=lambda r: r[0])  # sort by score
    rec = [(i+1, *r) for i, r in enumerate(rec)]

    # uniqueness filter
    selected, asset_counts = [], {}
    for rank, score, (a1, a2), df, highlight, metrics in rec:
        if asset_counts.get(a1,0)==0 and asset_counts.get(a2,0)==0:
            selected.append((rank, score, (a1, a2), df, highlight, metrics))
            asset_counts[a1] = asset_counts.get(a1,0)+1
            asset_counts[a2] = asset_counts.get(a2,0)+1
        if len(selected) >= top_n: break
    if len(selected) < top_n:
        for rank, score, (a1,a2), df, highlight, metrics in rec:
            if (rank,score,(a1,a2),df,highlight,metrics) in selected: continue
            if asset_counts.get(a1,0)<2 and asset_counts.get(a2,0)<2:
                selected.append((rank,score,(a1,a2),df,highlight,metrics))
                asset_counts[a1]=asset_counts.get(a1,0)+1
                asset_counts[a2]=asset_counts.get(a2,0)+1
            if len(selected)>=top_n: break
        print(f"⚠️ Not enough unique pairs, filled up to {top_n}")

    rec = selected

    # export results
    pairs_out=[]
    with PdfPages(out_pdf) as pdf:
        for rank, score, (a1,a2), df, highlight, metrics in rec:
            fig = make_signal_angle_figure(
                df,
                f"{a1} – {a2}",
                metrics,
                rolling_angle_windows_in_days=rolling_angle_windows_in_days,
                vol_window_minutes=vol_window_minutes,
                thresh_deg=10,
                highlight=highlight
            )
            pdf.savefig(fig); plt.close(fig)
            pairs_out.append([a1,a2])
            print(f"#{rank:02d} ✓ {a1}-{a2} (score={score:.4f})")

    atomic_write_yaml({"pairs": pairs_out}, out_yml)
    print(f"\n📄 PDF saved to: {out_pdf.resolve()}")
    print(f"📊 Pairs YAML saved to: {out_yml.resolve()}")

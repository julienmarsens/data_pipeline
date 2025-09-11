import os
from pathlib import Path
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.backends.backend_pdf import PdfPages
from matplotlib.colors import to_rgb


def loop_over_pairs(_pairs):
    """
    Iterate over *_pairs*, plotting the trading-angle figure for each and
    collecting every page into ONE multi-page PDF.
    """
    # ------------------------------------------------------------------ #
    data_path       = Path('./common/data/binance/spread')
    pdf_output_dir  = data_path / 'reports'
    pdf_output_dir.mkdir(parents=True, exist_ok=True)

    out_pdf = pdf_output_dir / 'signal_angles_report.pdf'

    # ------------------------------------------------------------------ #
    def _lighten(color, amount=0.55):
        """
        Blend *color* with white by *amount*.
        amount=0 → original colour, amount=1 → pure white.
        """
        c = np.array(to_rgb(color))
        return tuple(c + (1.0 - c) * amount)

    # ------------------------------------------------------------------ #
    def make_signal_angle_figure(df: pd.DataFrame, pair_lbl: str):
        """
        Return a 3-panel matplotlib Figure.
        Panel-3 shows:
            • overall regression (black)
            • three periodised regressions (pastel lines)
            • matching darker scatter clouds per period
        """
        # ---------- pre-work -------------------------------------------
        x = df['bid.a'].values
        y = df['bid.b'].values

        # overall regression (title + black line)
        g_slope, g_intercept = np.polyfit(x, y, 1)
        g_angle_deg = np.degrees(np.arctan(g_slope))

        mid_a = (df['bid.a'] + df['ask.a']) / 2
        mid_b = (df['bid.b'] + df['ask.b']) / 2

        # ---------- layout ---------------------------------------------
        fig, ax = plt.subplots(3, 1, figsize=(10, 12))
        fig.suptitle(f"{pair_lbl}   —   overall θ = {g_angle_deg:.2f}°",
                     fontsize=14, fontweight='bold')

        ax[0].plot(mid_a, color='steelblue')
        ax[0].set_title('Leg-A mid price')

        ax[1].plot(mid_b, color='darkorange')
        ax[1].set_title('Leg-B mid price')

        # ---------- regressions ----------------------------------------
        n       = len(df)
        thirds  = np.array_split(np.arange(n), 3)       # contiguous thirds

        scatter_cols = ['tab:blue', 'tab:red', 'tab:green']
        line_cols    = [_lighten(c, 0.55) for c in scatter_cols]
        labels       = ['Period 1', 'Period 2', 'Period 3']

        # background scatter for context
        order = np.argsort(x)
        ax[2].scatter(x[order], y[order], s=3, alpha=0.15, color='grey')

        # ---- overall regression in black ------------------------------
        ax[2].plot(x[order],
                   g_intercept + g_slope * x[order],
                   color='black', lw=2.5,
                   label=f"Total: θ = {g_angle_deg:.2f}°")

        # ---- periodised regressions -----------------------------------
        for i, idxs in enumerate(thirds):
            xs, ys = x[idxs], y[idxs]
            slope, intercept = np.polyfit(xs, ys, 1)
            angle_deg = np.degrees(np.arctan(slope))

            sort_idx = np.argsort(xs)
            ax[2].plot(xs[sort_idx],
                       intercept + slope * xs[sort_idx],
                       color=line_cols[i], lw=2,
                       label=f"{labels[i]}: θ = {angle_deg:.2f}°")

            ax[2].scatter(xs, ys, s=6, alpha=0.65, color=scatter_cols[i])

        # ---- cosmetics -------------------------------------------------
        ax[2].set_xlabel('bid.a')
        ax[2].set_ylabel('bid.b')
        ax[2].legend(frameon=False)
        ax[2].grid(True)

        plt.tight_layout(rect=[0, 0, 1, 0.96])
        return fig

    # ------------------------------------------------------------------ #
    with PdfPages(out_pdf) as pdf:
        for asset1, asset2 in _pairs:
            file_name = f"{asset1}_{asset2}_data.pkl"
            file_path = data_path / file_name

            try:
                df = pd.read_pickle(file_path)

                # tolerate ‘close_1/2’ naming
                if {'bid.a', 'bid.b'}.issubset(df.columns) is False:
                    df = df.rename(columns={'close_1': 'bid.a',
                                            'close_2': 'bid.b'})
                    # if asks are missing, clone bids (visual only)
                    for col in ('ask.a', 'ask.b'):
                        if col not in df.columns:
                            df[col] = df[col.replace('ask', 'bid')]

                fig = make_signal_angle_figure(df,
                                                pair_lbl=f"{asset1} – {asset2}")
                pdf.savefig(fig)
                plt.close(fig)
                print(f"✓ added {asset1}-{asset2}")

            except FileNotFoundError:
                print(f"⚠️  data file not found for {asset1}-{asset2}")
            except Exception as e:
                print(f"⚠️  error processing {asset1}-{asset2}: {repr(e)}")

    print(f"\n📄  Multi-page PDF saved to: {out_pdf.resolve()}")

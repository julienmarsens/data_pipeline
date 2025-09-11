import pandas as pd
import numpy as np
import statsmodels.api as sm
import matplotlib.pyplot as plt
import os
from PyPDF2 import PdfMerger

def estimate_half_life(spread):
    spread = spread.dropna()
    if len(spread) < 10:
        return np.nan

    spread_lag = spread.shift(1)
    spread_ret = spread - spread_lag
    spread_lag.iloc[0] = spread_lag.iloc[1]

    df = pd.DataFrame({'x': spread_lag, 'y': spread_ret}).dropna()
    if len(df) < 10 or df['x'].std() == 0 or df['y'].std() == 0:
        print("HL skipped: flat or invalid residual series")
        return np.nan

    try:
        model = sm.OLS(df['y'], sm.add_constant(df['x'])).fit()
        lambda_ = model.params.iloc[1]  # ✅ FIXED
        half_life = -np.log(2) / lambda_

        print(f"lambda: {lambda_}, half_life: {half_life}")
        if lambda_ >= 0 or np.isnan(half_life) or half_life > 5000:
            return np.nan
        return half_life

    except Exception as e:
        print("HL estimation failed:", repr(e))
        return np.nan


def compute_half_life_series(df, col1='close_1', col2='close_2', window=1000, step=100):
    """Compute rolling half-life over a sliding window between two asset price series."""
    half_lives = []
    timestamps = []

    DEBUG = False  # Set to True for debugging one window

    for start in range(0, len(df) - window + 1, step):
        sub_df = df.iloc[start:start + window]
        y = sub_df[col1].values
        x = sub_df[col2].values

        if np.std(x) == 0 or np.std(y) == 0:
            half_lives.append(np.nan)
            timestamps.append(df.index[start + window - 1])
            continue

        try:
            x_with_const = sm.add_constant(x)
            model = sm.OLS(y, x_with_const).fit()
            residuals = model.resid
            hl = estimate_half_life(pd.Series(residuals))

            if DEBUG:
                print(f"\nWindow starting at: {df.index[start]}")
                print("Regression params:", model.params)
                print("Residual std:", np.std(residuals))
                print("Estimated half-life:", hl)
                pd.Series(residuals, index=sub_df.index).plot(title="Residuals (Spread)")
                plt.grid(True)
                plt.tight_layout()
                plt.show()
                break

        except:
            hl = np.nan

        half_lives.append(hl)
        timestamps.append(df.index[start + window - 1])

    hl_series = pd.Series(half_lives, index=timestamps)
    print("Rolling half-life computed.")
    print(hl_series.describe())
    return hl_series


def plot_half_life_series(half_life_series, threshold=100, title='Rolling Half-Life of Mean Reversion', return_fig=False):
    smoothed = half_life_series.ewm(span=5).mean()
    smoothed = smoothed.sort_index()

    below_threshold = smoothed < threshold

    fig, ax = plt.subplots(figsize=(14, 6))
    ax.plot(smoothed, label='Smoothed Half-Life', linewidth=2)
    ax.axhline(threshold, color='red', linestyle='--', label=f'{threshold}-bar threshold')
    ax.fill_between(
        smoothed.index,
        0,
        smoothed.values,
        where=below_threshold.values,
        color='green',
        alpha=0.2,
        label='Tradable Regime'
    )

    ax.set_title(title)
    ax.set_xlabel("Time")
    ax.set_ylabel("Estimated Half-Life (bars)")
    ax.legend()
    ax.grid(True)
    fig.tight_layout()

    if return_fig:
        return fig
    else:
        plt.show()

def loop_over_pairs(_pairs, half_life_window, half_life_step):
    data_path = './common/data/binance/spread/'
    pdf_output_dir = './common/data/binance/spread/reports/'
    os.makedirs(pdf_output_dir, exist_ok=True)

    individual_pdfs = []

    for asset_1, asset_2 in _pairs:
        file_name = f"{asset_1}_{asset_2}_data.pkl"
        file_path = os.path.join(data_path, file_name)

        try:
            df = pd.read_pickle(file_path)
            df.index = pd.to_datetime(df['open_time']) if 'open_time' in df.columns else pd.to_datetime(
                df.index)

            hl_series = compute_half_life_series(
                df,
                col1='close_1',
                col2='close_2',
                window=half_life_window,
                step=half_life_step
            )

            # Plot and save to individual PDF
            fig = plot_half_life_series(
                hl_series,
                threshold=100,
                title=f'Rolling Half-Life ({asset_1} - {asset_2})',
                return_fig=True
            )

            pdf_file = os.path.join(pdf_output_dir, f"{asset_1}_{asset_2}_half_life.pdf")
            fig.savefig(pdf_file)
            individual_pdfs.append(pdf_file)
            plt.close(fig)

            print(f"✅ Saved: {pdf_file}")

        except Exception as e:
            print(f"⚠️ Error on {asset_1}-{asset_2}: {repr(e)}")

    # ---- MERGE ALL PDFs ----
    merged_pdf_path = os.path.join(pdf_output_dir, "all_pairs_half_life_report.pdf")
    merger = PdfMerger()

    for pdf in individual_pdfs:
        merger.append(pdf)

    merger.write(merged_pdf_path)
    merger.close()

    print(f"\n📄 Merged PDF saved to: {merged_pdf_path}")


# ========= MAIN SCRIPT =========
if __name__ == "__main__":
    file_path = './common/data/binance/spread/DOTUSDT_DOGEUSDT_data.pkl'
    df = pd.read_pickle(file_path)

    # Ensure datetime index
    if 'open_time' in df.columns:
        df.index = pd.to_datetime(df['open_time'])
    else:
        df.index = pd.to_datetime(df.index)

    # Compute and plot rolling half-life
    hl_series = compute_half_life_series(
        df,
        col1='close_1',
        col2='close_2',
        window=1000,
        step=100
    )

    plot_half_life_series(
        hl_series,
        threshold=100,
        title='Rolling Half-Life (DOTUSDT - DOGEUSDT)'
    )

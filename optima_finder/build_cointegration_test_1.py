import pandas as pd
import numpy as np
import statsmodels.api as sm
import matplotlib.pyplot as plt


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


def compute_half_life_series(df, col1='close_1', col2='close_2', window=2000, step=100):
    # Add this near the top of compute_half_life_series for debug
    DEBUG = False  # Set to True for one window

    for start in range(0, len(df) - window + 1, step):
        sub_df = df.iloc[start:start + window]
        y = sub_df[col1].values
        x = sub_df[col2].values

        if np.std(x) == 0 or np.std(y) == 0:
            continue

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

    """Compute rolling half-life over a sliding window between two asset price series."""
    half_lives = []
    timestamps = []

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
        except:
            hl = np.nan

        half_lives.append(hl)
        timestamps.append(df.index[start + window - 1])

    hl_series = pd.Series(half_lives, index=timestamps)
    print("Rolling half-life computed.")
    print(hl_series.describe())
    return hl_series


def plot_half_life_series(half_life_series, threshold=100, title='Rolling Half-Life of Mean Reversion'):
    """Plot rolling half-life series with shaded tradable regions."""
    smoothed = half_life_series.ewm(span=10).mean()

    plt.figure(figsize=(14, 6))
    plt.plot(smoothed, label='Smoothed Half-Life', linewidth=2)
    plt.axhline(threshold, color='red', linestyle='--', label=f'{threshold}-bar threshold')

    # Highlight tradable periods
    below_threshold = smoothed < threshold
    plt.fill_between(
        smoothed.index,
        0,
        smoothed.values,
        where=below_threshold.values,
        color='green',
        alpha=0.2,
        label='Tradable Regime'
    )

    plt.title(title)
    plt.xlabel("Time")
    plt.ylabel("Estimated Half-Life (bars)")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()


# ========= MAIN SCRIPT =========
if __name__ == "__main__":
    # Replace with your actual file path
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

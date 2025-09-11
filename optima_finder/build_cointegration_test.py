import pandas as pd
import numpy as np
import statsmodels.api as sm
from statsmodels.tsa.stattools import adfuller
import matplotlib.pyplot as plt


def compute_engle_granger_pvalue(y, x):
    if np.std(x) == 0 or np.std(y) == 0:
        return np.nan

    x = sm.add_constant(x)
    model = sm.OLS(y, x).fit()
    residuals = model.resid

    try:
        pval = adfuller(residuals, maxlag=1, autolag=None)[1]
    except:
        pval = np.nan

    return pval


def rolling_cointegration_pvalues(df, col1='close_1', col2='close_2', window=2000, step=100):
    pvals = []
    timestamps = []

    for start in range(0, len(df) - window + 1, step):
        sub_df = df.iloc[start:start + window]
        y = sub_df[col1].values
        x = sub_df[col2].values
        pval = compute_engle_granger_pvalue(y, x)

        pvals.append(pval)
        timestamps.append(df.index[start + window - 1])

    return pd.Series(pvals, index=timestamps)


def plot_smoothed_cointegration(df, col1='close_1', col2='close_2', window=2000, step=100, ema_span=10, threshold=0.05):
    print(f"Running rolling Engle-Granger test with window={window}, step={step}...")
    pval_series = rolling_cointegration_pvalues(df, col1, col2, window, step)
    pval_series = pval_series.clip(lower=0, upper=1)
    smoothed_pval = pval_series.ewm(span=ema_span).mean()

    plt.figure(figsize=(14, 6))
    plt.plot(smoothed_pval, label='Smoothed ADF p-value', linewidth=2)
    plt.axhline(threshold, color='red', linestyle='--', label='p = 0.05 threshold')

    # Highlight cointegrated regimes
    below_thresh = smoothed_pval < threshold
    plt.fill_between(smoothed_pval.index, 0, 1, where=below_thresh, color='green', alpha=0.2, label='Cointegrated')

    plt.title(f"Rolling Cointegration ADF Test ({col1} vs {col2})")
    plt.xlabel("Time")
    plt.ylabel("ADF p-value")
    plt.legend()
    plt.grid(True)
    plt.tight_layout()
    plt.show()


# ========= MAIN SCRIPT =========
if __name__ == "__main__":
    # Load your pair data
    file_path = './common/data/binance/spread/DOTUSDT_DOGEUSDT_data.pkl'
    df = pd.read_pickle(file_path)

    # Ensure proper datetime index
    if 'open_time' in df.columns:
        df.index = pd.to_datetime(df['open_time'])
    else:
        df.index = pd.to_datetime(df.index)

    # Run and plot cointegration analysis
    plot_smoothed_cointegration(
        df,
        col1='close_1',
        col2='close_2',
        window=3000,
        step=50,
        ema_span=10,
        threshold=0.05
    )

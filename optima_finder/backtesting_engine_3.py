#!/usr/bin/env python3
# synthetic_signal_backtester_fixed.py

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.dates as mdates


class SyntheticSignalBacktester:
    def __init__(self, df, signal_angle=0.0, trading_angle=0.0,
                 stepback=0.0003, order_size=1000, nc2l=5,
                 relative_margin_factor=50):
        self.df = df.copy()
        self.signal_angle = signal_angle * np.pi / 4 + np.pi / 4  # map [-1, 1] -> [45°, 135°]
        self.trading_angle = trading_angle * np.pi / 4 + np.pi / 4
        self.relative_margin_factor = relative_margin_factor
        self.stepback = stepback
        self.order_size = order_size
        self.nc2l = nc2l
        self.results = None
        self.upper_bounds = []
        self.lower_bounds = []
        self.margin = None  # will be set dynamically

    # ------------------------------------------------------------------
    def compute_vectors(self):
        # Use positive sin (avoid flipping axis unexpectedly)
        signal_vec = np.array([np.cos(self.signal_angle), np.sin(self.signal_angle)])
        trading_vec = np.array([np.cos(self.trading_angle), np.sin(self.trading_angle)])

        self.normalized_signal_vec = signal_vec / np.linalg.norm(signal_vec)
        self.normalized_trading_vec = trading_vec / np.linalg.norm(trading_vec)

        # Estimate minimum tick sizes
        tick_1 = self.df['close_1'].diff().abs().replace(0, np.nan).min()
        tick_2 = self.df['close_2'].diff().abs().replace(0, np.nan).min()

        base_margin = max(
            abs(self.normalized_signal_vec[0]) * tick_1,
            abs(self.normalized_signal_vec[1]) * tick_2
        )

        self.margin = base_margin * self.relative_margin_factor

    # ------------------------------------------------------------------
    def construct_synthetic_prices(self):
        self.df['mid_a'] = self.df['close_1']
        self.df['mid_b'] = self.df['close_2']
        self.df['signal_price'] = (
            self.df['mid_a'] * self.normalized_signal_vec[0] +
            self.df['mid_b'] * self.normalized_signal_vec[1]
        )

    # ------------------------------------------------------------------
    def run_backtest(self):
        print("Running synthetic signal backtest...")

        position_1, position_2, pnl = [0], [0], [0]
        cash = 0.0
        pos1 = pos2 = 0
        upper = lower = None

        # Position limits for both legs
        inv_lim1 = self.nc2l * abs(self.order_size * self.normalized_trading_vec[0])
        inv_lim2 = self.nc2l * abs(self.order_size * self.normalized_trading_vec[1])

        for i in range(1, len(self.df)):
            row, prev_row = self.df.iloc[i], self.df.iloc[i - 1]
            signal, price_1, price_2 = row['signal_price'], row['mid_a'], row['mid_b']

            if upper is None or lower is None:
                upper, lower = signal + self.margin, signal - self.margin

            entered = False

            # ---- LONG trade ----
            if signal < lower:
                new_pos1 = pos1 + self.order_size * self.normalized_trading_vec[0]
                new_pos2 = pos2 + self.order_size * self.normalized_trading_vec[1]
                if abs(new_pos1) <= inv_lim1 and abs(new_pos2) <= inv_lim2:
                    pos1, pos2 = new_pos1, new_pos2
                    entered = True

            # ---- SHORT trade ----
            elif signal > upper:
                new_pos1 = pos1 - self.order_size * self.normalized_trading_vec[0]
                new_pos2 = pos2 - self.order_size * self.normalized_trading_vec[1]
                if abs(new_pos1) <= inv_lim1 and abs(new_pos2) <= inv_lim2:
                    pos1, pos2 = new_pos1, new_pos2
                    entered = True

            # ---- Only recenter bands on actual trade ----
            if entered:
                upper, lower = signal + self.margin, signal - self.margin

            # ---- Book-keeping ----
            position_1.append(pos1)
            position_2.append(pos2)

            dPnl = pos1 * (price_1 - prev_row['mid_a']) + \
                   pos2 * (price_2 - prev_row['mid_b'])
            cash += dPnl
            pnl.append(cash)

            self.upper_bounds.append(upper)
            self.lower_bounds.append(lower)

        # Store results
        self.df = self.df.iloc[1:].copy()
        self.df['position_1'] = position_1[1:]
        self.df['position_2'] = position_2[1:]
        self.df['pnl'] = pnl[1:]
        self.df['upper'] = self.upper_bounds
        self.df['lower'] = self.lower_bounds
        self.results = self.df
        print("Backtest complete.")

    # ------------------------------------------------------------------
    def run(self):
        self.compute_vectors()
        self.construct_synthetic_prices()
        self.run_backtest()
        return self.results

    # ------------------------------------------------------------------
    def plot_results(self):
        if self.results is None:
            raise ValueError("Run backtest first using .run()")

        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 10), sharex=True)

        # ---- PnL ----
        ax1.plot(self.results.index, self.results['pnl'], label='PnL', color='blue')
        ax1.set_title("PnL Over Time")
        ax1.set_ylabel("PnL (USD)")
        ax1.grid(True, alpha=0.3)

        # ---- Positions ----
        ax2.plot(self.results.index, self.results['position_1'], label='Position 1', color='red')
        ax2.plot(self.results.index, self.results['position_2'], label='Position 2', color='green')
        ax2.set_title("Positions")
        ax2.set_ylabel("Contracts")
        ax2.legend()
        ax2.grid(True, alpha=0.3)

        # ---- Signal & Bounds ----
        ax3.plot(self.results.index, self.results['signal_price'], label='Signal Price', color='black')
        ax3.plot(self.results.index, self.results['upper'], '--', label='Upper Bound', alpha=0.7, color='orange')
        ax3.plot(self.results.index, self.results['lower'], '--', label='Lower Bound', alpha=0.7, color='blue')
        ax3.set_title("Synthetic Signal and Thresholds")
        ax3.legend()
        ax3.grid(True, alpha=0.3)

        # ---- Datetime formatting ----
        locator = mdates.AutoDateLocator()
        formatter = mdates.ConciseDateFormatter(locator)
        ax3.xaxis.set_major_locator(locator)
        ax3.xaxis.set_major_formatter(formatter)
        fig.autofmt_xdate()

        plt.tight_layout()
        plt.show()


# ----------------------------------------------------------------------
if __name__ == "__main__":
    _path = './common/data/binance/spread/'
    df = pd.read_pickle(f"{_path}/ADAUSDT_DOTUSDT_data.pkl")

    # Ensure datetime index
    df.index = pd.to_datetime(df.get("open_time", df.index), errors="coerce", utc=True)

    backtester = SyntheticSignalBacktester(
        df=df,
        signal_angle=0.80,       # range [-1, 1]
        trading_angle=0.50,      # range [-1, 1]
        stepback=0.0003,
        order_size=500,
        nc2l=7,
        relative_margin_factor=25
    )

    results = backtester.run()
    backtester.plot_results()

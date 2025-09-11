import pandas as pd
import numpy as np
import statsmodels.api as sm
import matplotlib.pyplot as plt


class BacktestingEngine:

    def __init__(self, merged_df, entry_threshold=1.5, exit_threshold=0.5, hedge_ratio_lookback=500, rebalance_period=15):
        self.df = merged_df.copy()
        self.entry_threshold = entry_threshold
        self.exit_threshold = exit_threshold
        self.hedge_ratio_lookback = hedge_ratio_lookback
        self.rebalance_period = rebalance_period
        self.results = None

    def compute_hedge_ratio(self):
        print("Computing hedge ratios...")
        hedge_ratios = [np.nan] * len(self.df)

        for i in range(self.hedge_ratio_lookback, len(self.df), self.rebalance_period):
            y = self.df['close_1'].iloc[i - self.hedge_ratio_lookback:i]
            x = self.df['close_2'].iloc[i - self.hedge_ratio_lookback:i]
            x = sm.add_constant(x)
            model = sm.OLS(y, x).fit()
            hr = model.params[1]
            for j in range(i, min(i + self.rebalance_period, len(self.df))):
                hedge_ratios[j] = hr

        self.df['hedge_ratio'] = hedge_ratios
        self.df = self.df.dropna().copy()
        print("Hedge ratios computed.")

    def compute_zscore(self, window=100):
        print("Computing spread and z-score...")
        self.df['spread'] = self.df['close_1'] - self.df['hedge_ratio'] * self.df['close_2']
        self.df['spread_mean'] = self.df['spread'].rolling(window=window).mean()
        self.df['spread_std'] = self.df['spread'].rolling(window=window).std()
        self.df['zscore'] = (self.df['spread'] - self.df['spread_mean']) / self.df['spread_std']
        print("Z-score computed.")

    def run_backtest(self):
        print("Running backtest...")
        position_1 = []
        position_2 = []
        inventory_1 = []
        inventory_2 = []
        total_inventory = []
        pnl = []
        signals = []
        cash = 0

        current_pos_1 = 0
        current_pos_2 = 0

        for i in range(len(self.df)):
            z = self.df['zscore'].iloc[i]
            p1 = self.df['close_1'].iloc[i]
            p2 = self.df['close_2'].iloc[i]
            hr = self.df['hedge_ratio'].iloc[i]
            signal = 0

            # Only rebalance every self.rebalance_period bars
            if i % self.rebalance_period == 0:
                base_usd = 1000
                qty_1 = base_usd / p1
                qty_2 = hr * qty_1

                # Entry logic
                if z > self.entry_threshold:
                    current_pos_1 = -qty_1
                    current_pos_2 = qty_2
                    signal = -1
                    print(f"[{i}] SHORT spread | z={z:.2f}")
                elif z < -self.entry_threshold:
                    current_pos_1 = qty_1
                    current_pos_2 = -qty_2
                    signal = 1
                    print(f"[{i}] LONG spread  | z={z:.2f}")
                elif abs(z) < self.exit_threshold:
                    current_pos_1 = 0
                    current_pos_2 = 0
                    signal = 0
                    print(f"[{i}] EXIT         | z={z:.2f}")

            position_1.append(current_pos_1)
            position_2.append(current_pos_2)
            signals.append(signal)

            inv1 = current_pos_1 * p1
            inv2 = current_pos_2 * p2
            inventory_1.append(inv1)
            inventory_2.append(inv2)
            total_inventory.append(inv1 + inv2)

            if i > 0:
                delta_pnl = (
                    position_1[-2] * (p1 - self.df['close_1'].iloc[i - 1]) +
                    position_2[-2] * (p2 - self.df['close_2'].iloc[i - 1])
                )
                cash += delta_pnl

            pnl.append(cash)

        self.df['position_1'] = position_1
        self.df['position_2'] = position_2
        self.df['inventory_1'] = inventory_1
        self.df['inventory_2'] = inventory_2
        self.df['inventory'] = total_inventory
        self.df['pnl'] = pnl
        self.df['signal'] = signals
        self.results = self.df
        print("Backtest complete.")

    def plot_results(self):
        if self.results is None:
            raise ValueError("Run backtest first using .run_backtest()")

        fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(14, 12), sharex=True)

        # PnL
        ax1.plot(self.results.index, self.results['pnl'], label='PnL', color='blue')
        ax1.set_ylabel('PnL (USD)', color='blue')
        ax1.set_title("PnL Over Time")

        # Inventories
        ax2.plot(self.results.index, self.results['inventory_1'], label='Inventory Asset 1', color='red', alpha=0.7)
        ax2.plot(self.results.index, self.results['inventory_2'], label='Inventory Asset 2', color='green', alpha=0.7)
        ax2.set_ylabel('Inventory Value')
        ax2.set_title("Inventory in Each Leg")
        ax2.legend()

        # Spread and signals
        ax3.plot(self.results.index, self.results['spread'], label='Spread', color='black')
        ax3.plot(self.results.index, self.results['spread_mean'], label='Spread Mean', linestyle='--', alpha=0.7)
        ax3.plot(self.results.index, self.results['spread_mean'] + self.entry_threshold * self.results['spread_std'],
                 label='Entry Threshold', linestyle=':', color='red')
        ax3.plot(self.results.index, self.results['spread_mean'] - self.entry_threshold * self.results['spread_std'],
                 label='Entry Threshold', linestyle=':', color='red')

        long_idx = self.results[self.results['signal'] == 1].index
        short_idx = self.results[self.results['signal'] == -1].index

        ax3.scatter(long_idx, self.results.loc[long_idx, 'spread'], marker='^', color='green', label='Long Entry')
        ax3.scatter(short_idx, self.results.loc[short_idx, 'spread'], marker='v', color='red', label='Short Entry')

        ax3.set_ylabel("Spread")
        ax3.set_title("Spread and Signal Execution")
        ax3.legend()

        plt.tight_layout()
        plt.show()

    def run_full_backtest(self):
        self.compute_hedge_ratio()
        self.compute_zscore()
        self.run_backtest()
        return self.results


if __name__ == "__main__":
    _path = './common/data/binance/spread/'
    merged_df = pd.read_pickle(f"{_path}/DOTUSDT_ADAUSDT_data.pkl")

    backtester = BacktestingEngine(merged_df)
    results = backtester.run_full_backtest()
    backtester.plot_results()

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.dates as mdates


def backtest_spot_perp_basis(
    df,
    investment,
    leverage,
    annual_borrow_rate,
    fee_bps,
    earn_yield,
    asset_name="ASSET",
    plot=True,
):
    """
    df: DataFrame with columns:
        ['perp_close', 'spot_close', 'fundingRate', 'fundingEvent']
        index: datetime (sorted)
    investment: base capital in USD
    leverage: e.g. 3x, 5x
    annual_borrow_rate: e.g. 0.10 for 10% per year
    fee_bps: trading fee in basis points PER LEG (default 20 bps)
    """

    df = df.copy().sort_index()

    # ----------------------------------------------------------------------
    # 1) INITIAL POSITION SETUP
    # ----------------------------------------------------------------------
    perp0 = df["perp_close"].iloc[0]
    spot0 = df["spot_close"].iloc[0]

    notional_usd = investment * leverage  # size per leg in USD

    # Quantities in coins
    qty_spot = notional_usd / spot0              # long this on spot
    qty_perp = notional_usd / perp0              # short this on perp

    # Trading fees (charged on notional of each leg)
    fee_rate = fee_bps / 10_000.0  # 20 bps => 0.002
    trading_fees = notional_usd * fee_rate * 2.0  # spot + perp

    # Borrowed capital (for leveraged spot)
    borrowed_usd = max(0.0, notional_usd - investment)  # guard if leverage <= 1

    # ----------------------------------------------------------------------
    # 2) PRICE PNL (MARK-TO-MARKET)
    # ----------------------------------------------------------------------
    spot_close = df["spot_close"]
    perp_close = df["perp_close"]

    pnl_spot = qty_spot * (spot_close - spot0)
    pnl_perp = qty_perp * (perp0 - perp_close)  # short: gain if price ↓

    price_pnl = pnl_spot + pnl_perp

    # ----------------------------------------------------------------------
    # 3) FUNDING PNL (APPLY ONLY WHEN fundingEvent != 0)
    #    Correctly use *current* perp notional, not fixed initial notional.
    # ----------------------------------------------------------------------
    perp_notional_t = qty_perp * perp_close  # USD notional over time

    funding_pnl_events = perp_notional_t * df["fundingEvent"]
    funding_pnl_cum = funding_pnl_events.cumsum()

    # ----------------------------------------------------------------------
    # 4) BORROW INTEREST (CONTINUOUS OVER TIME)
    # ----------------------------------------------------------------------
    idx = df.index.to_series()
    dt_days = idx.diff().dt.total_seconds() / 86400.0
    dt_days.iloc[0] = 0.0

    interest_cost = borrowed_usd * annual_borrow_rate * (dt_days / 365.0)
    interest_cost_cum = interest_cost.cumsum()

    # ----------------------------------------------------------------------
    # 4b) EARN YIELD (CONTINUOUS OVER TIME — OPPOSITE OF BORROW)
    # ----------------------------------------------------------------------
    earn_income = notional_usd * earn_yield * (dt_days / 365.0)
    earn_income_cum = earn_income.cumsum()

    # ----------------------------------------------------------------------
    # 5) EQUITY CURVE
    # ----------------------------------------------------------------------
    initial_equity = investment - trading_fees

    equity = (
            initial_equity
            + price_pnl
            + funding_pnl_cum
            - interest_cost_cum
            + earn_income_cum  # <--- ADD THIS
    )

    # ----------------------------------------------------------------------
    # 6) SPREAD IN PERCENT
    # ----------------------------------------------------------------------
    spread_pct = (perp_close / spot_close - 1.0) * 100.0
    df["spread_pct"] = spread_pct
    df["equity"] = equity
    df["price_pnl"] = price_pnl
    df["funding_pnl_cum"] = funding_pnl_cum
    df["interest_cost_cum"] = interest_cost_cum
    df["earn_income_cum"] = earn_income_cum

    # ----------------------------------------------------------------------
    # 6b) BTC PRICE + 50-DAY MA (HOURLY DATA)
    # ----------------------------------------------------------------------
    ma_window = 50 * 24  # 50 days in hours
    df["btc_price"] = df["spot_close"]
    df["btc_ma_50d"] = df["btc_price"].rolling(ma_window).mean()

    # MA slope (negative = bearish regime)
    df["btc_ma_slope"] = df["btc_ma_50d"].diff()

    # Boolean regime mask
    bearish_mask = df["btc_ma_slope"] < 0

    def shade_bearish(ax, x, mask, color="red", alpha=0.08):
        in_region = False
        start = None

        for t, m in zip(x, mask):
            if m and not in_region:
                start = t
                in_region = True
            elif not m and in_region:
                ax.axvspan(start, t, color=color, alpha=alpha, linewidth=0)
                in_region = False

        if in_region:
            ax.axvspan(start, x[-1], color=color, alpha=alpha, linewidth=0)

    # ----------------------------------------------------------------------
    # 7) PLOTS: 3 STACKED PANELS
    # ----------------------------------------------------------------------
    if plot:
        # ----------------------------------------------------------------------
        # 7) PLOTS: 4 STACKED PANELS
        # ----------------------------------------------------------------------
        if plot:
            fig, (ax1, ax2, ax3, ax4) = plt.subplots(
                4, 1, figsize=(16, 13),
                sharex=True,
                gridspec_kw={"height_ratios": [1, 1, 2, 2]}
            )

            # 1) Spread
            ax1.plot(df.index, df["spread_pct"], linewidth=1.5)
            ax1.axhline(0, color="gray", linestyle="--", linewidth=1, alpha=0.7)
            ax1.set_title(f"{asset_name} Spot–Perp Spread (%)")
            ax1.set_ylabel("Spread (%)")
            ax1.grid(alpha=0.3)

            # 2) Funding rate
            ax2.plot(df.index, df["fundingRate"], linewidth=1.5)
            ax2.axhline(0, color="gray", linestyle="--", linewidth=1, alpha=0.7)
            ax2.set_title(f"{asset_name} Perp Funding Rate")
            ax2.set_ylabel("Rate")
            ax2.grid(alpha=0.3)

            # 3) Equity curve
            ax3.plot(df.index, df["equity"], linewidth=1.8)
            ax3.set_title(f"{asset_name} Basis Strategy Equity Curve")
            ax3.set_ylabel("Equity (USD)")
            ax3.grid(alpha=0.3)

            # 4) BTC price + 50D MA
            ax4.plot(df.index, df["btc_price"], label="BTC Price", linewidth=1.2)
            ax4.plot(df.index, df["btc_ma_50d"], label="BTC 50D MA", linewidth=1.5)
            ax4.set_title("BTC Price and 50-Day Moving Average")
            ax4.set_ylabel("Price")
            ax4.grid(alpha=0.3)
            ax4.legend()

            # --- Shade bearish MA regime across ALL charts ---
            for ax in (ax1, ax2, ax3, ax4):
                shade_bearish(ax, df.index, bearish_mask)

            ax4.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
            plt.xticks(rotation=45)

            plt.tight_layout()
            plt.show()

    return df

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
    # 5) EQUITY CURVE
    # ----------------------------------------------------------------------
    initial_equity = investment - trading_fees

    equity = (
        initial_equity
        + price_pnl
        + funding_pnl_cum
        - interest_cost_cum
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

    # ----------------------------------------------------------------------
    # 7) PLOTS: 3 STACKED PANELS
    # ----------------------------------------------------------------------
    if plot:
        fig, (ax1, ax2, ax3) = plt.subplots(
            3, 1, figsize=(16, 10),
            sharex=True,
            gridspec_kw={"height_ratios": [1, 1, 2]}
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
        ax3.set_xlabel("Time")
        ax3.grid(alpha=0.3)

        ax3.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
        plt.xticks(rotation=45)

        plt.tight_layout()
        plt.show()

    return df

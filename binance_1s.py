#!/usr/bin/env python3
"""
Download 1-second OHLCV for Binance COIN-M (inverse) perpetuals by resampling aggTrades.

Requires: pip install requests
"""

from __future__ import annotations

import csv
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from typing import Optional, Dict, Any, List

import requests


BASE_URL = "https://dapi.binance.com"
AGG_TRADES_EP = "/dapi/v1/aggTrades"


def parse_time_to_ms(s: str) -> int:
    """
    Accepts:
      - milliseconds since epoch: "1700000000000"
      - ISO-8601: "2026-01-01T00:00:00Z" or "2026-01-01T00:00:00+00:00"
      - naive ISO-8601: assumed UTC, e.g. "2026-01-01T00:00:00"
    """
    s = s.strip()
    if s.isdigit():
        return int(s)
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    dt = datetime.fromisoformat(s)
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return int(dt.timestamp() * 1000)


def ms_to_iso_utc(ms: int) -> str:
    return datetime.fromtimestamp(ms / 1000, tz=timezone.utc).isoformat().replace("+00:00", "Z")


def floor_to_second_ms(ms: int) -> int:
    return (ms // 1000) * 1000


def request_with_backoff(
    session: requests.Session,
    url: str,
    params: Dict[str, Any],
    timeout: int,
    sleep: float,
) -> Any:
    """
    Basic retry/backoff for rate limiting (429/418) and transient 5xx.
    """
    backoff = max(0.2, sleep)
    for _ in range(12):
        r = session.get(url, params=params, timeout=timeout)
        if r.status_code in (418, 429):
            time.sleep(backoff)
            backoff = min(backoff * 2, 10.0)
            continue
        if 500 <= r.status_code < 600:
            time.sleep(backoff)
            backoff = min(backoff * 2, 10.0)
            continue
        r.raise_for_status()
        return r.json()
    raise RuntimeError("Too many retries (rate limit / server errors).")


def fetch_agg_trades(
    session: requests.Session,
    symbol: str,
    start_time_ms: Optional[int] = None,
    from_id: Optional[int] = None,
    limit: int = 1000,
    timeout: int = 30,
    sleep: float = 0.2,
) -> List[Dict[str, Any]]:
    """
    Uses startTime for the first page, then fromId for pagination.
    """
    url = BASE_URL + AGG_TRADES_EP
    params: Dict[str, Any] = {"symbol": symbol, "limit": min(int(limit), 1000)}

    if from_id is not None:
        params["fromId"] = int(from_id)
    elif start_time_ms is not None:
        params["startTime"] = int(start_time_ms)
    else:
        raise ValueError("Provide start_time_ms or from_id")

    data = request_with_backoff(session, url, params=params, timeout=timeout, sleep=sleep)
    if not isinstance(data, list):
        raise RuntimeError(f"Unexpected response: {data}")
    return data


@dataclass
class Candle:
    sec_ms: int
    open: float
    high: float
    low: float
    close: float
    volume: float
    count: int

    def to_csv_row(self) -> List[Any]:
        return [ms_to_iso_utc(self.sec_ms), self.open, self.high, self.low, self.close, self.volume, self.count]


class CandleBuilder:
    def __init__(self, writer: csv.writer, fill_missing: bool):
        self.w = writer
        self.fill_missing = fill_missing
        self.cur: Optional[Candle] = None
        self.last_close: Optional[float] = None

    def _flush_cur(self) -> None:
        if self.cur is None:
            return
        self.w.writerow(self.cur.to_csv_row())
        self.last_close = self.cur.close
        self.cur = None

    def _emit_missing(self, from_sec_ms: int, to_sec_ms: int) -> None:
        """
        Emit missing seconds (exclusive of from_sec_ms, up to < to_sec_ms).
        Uses last_close as O/H/L/C and 0 volume.
        """
        if not self.fill_missing or self.last_close is None:
            return
        sec = from_sec_ms + 1000
        while sec < to_sec_ms:
            c = Candle(sec, self.last_close, self.last_close, self.last_close, self.last_close, 0.0, 0)
            self.w.writerow(c.to_csv_row())
            sec += 1000

    def add_trade(self, t_ms: int, price: float, qty: float) -> None:
        sec_ms = floor_to_second_ms(t_ms)

        if self.cur is None:
            self.cur = Candle(sec_ms, price, price, price, price, qty, 1)
            return

        if sec_ms == self.cur.sec_ms:
            self.cur.high = max(self.cur.high, price)
            self.cur.low = min(self.cur.low, price)
            self.cur.close = price
            self.cur.volume += qty
            self.cur.count += 1
            return

        if sec_ms > self.cur.sec_ms:
            prev_sec = self.cur.sec_ms
            self._flush_cur()
            self._emit_missing(prev_sec, sec_ms)
            self.cur = Candle(sec_ms, price, price, price, price, qty, 1)
            return

        # Out-of-order trade (rare): ignore in this streaming implementation
        return

    def finish(self, end_ms: Optional[int] = None) -> None:
        if self.cur is None:
            return
        last_sec = self.cur.sec_ms
        self._flush_cur()
        if end_ms is not None and self.fill_missing and self.last_close is not None:
            end_sec = floor_to_second_ms(end_ms)
            sec = last_sec + 1000
            while sec <= end_sec:
                c = Candle(sec, self.last_close, self.last_close, self.last_close, self.last_close, 0.0, 0)
                self.w.writerow(c.to_csv_row())
                sec += 1000


def download_coinm_1s_ohlcv_from_aggtrades(
    symbol: str,
    out_csv: str,
    start_ms: int,
    end_ms: int,
    *,
    sleep: float = 0.2,
    timeout: int = 30,
    limit: int = 1000,
    fill_missing: bool = True,
) -> None:
    if end_ms < start_ms:
        raise ValueError("end_ms < start_ms")

    session = requests.Session()

    with open(out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["timestamp_utc", "open", "high", "low", "close", "volume", "agg_trade_count"])
        builder = CandleBuilder(w, fill_missing=fill_missing)

        # First page by startTime
        data = fetch_agg_trades(
            session,
            symbol=symbol,
            start_time_ms=start_ms,
            from_id=None,
            limit=limit,
            timeout=timeout,
            sleep=sleep,
        )

        if not data:
            # No trades in range; write only header.
            return

        data.sort(key=lambda x: (int(x["T"]), int(x["a"])))
        last_a = int(data[-1]["a"])

        for tr in data:
            t = int(tr["T"])
            if t < start_ms:
                continue
            if t > end_ms:
                builder.finish(end_ms=end_ms)
                return
            builder.add_trade(t_ms=t, price=float(tr["p"]), qty=float(tr["q"]))

        time.sleep(sleep)

        # Continue paging by fromId
        while True:
            data = fetch_agg_trades(
                session,
                symbol=symbol,
                start_time_ms=None,
                from_id=last_a + 1,
                limit=limit,
                timeout=timeout,
                sleep=sleep,
            )
            if not data:
                break

            data.sort(key=lambda x: (int(x["T"]), int(x["a"])))
            last_a = int(data[-1]["a"])

            done = False
            for tr in data:
                t = int(tr["T"])
                if t < start_ms:
                    continue
                if t > end_ms:
                    done = True
                    break
                builder.add_trade(t_ms=t, price=float(tr["p"]), qty=float(tr["q"]))

            if done:
                break

            time.sleep(sleep)

        builder.finish(end_ms=end_ms)


if __name__ == "__main__":
    # ----------------------------
    # HARD-CODED "ARGUMENTS" HERE
    # ----------------------------
    SYMBOL = "BTCUSD_PERP"                 # COIN-M inverse perpetual symbol
    OUT_CSV = "BTCUSD_PERP_1s.csv"

    # Choose ONE way to set the time range:

    # (A) ISO-8601 times (UTC recommended)
    START = "2026-01-01T00:00:00Z"
    END = "2026-01-01T00:05:00Z"

    # (B) Or epoch milliseconds (uncomment and set)
    # START = "1767225600000"
    # END   = "1767229200000"

    # Tuning
    SLEEP = 0.2            # seconds between requests
    TIMEOUT = 30           # HTTP timeout (seconds)
    LIMIT = 1000           # max 1000
    FILL_MISSING = True    # fill missing seconds with last close, 0 volume

    start_ms = parse_time_to_ms(START)
    end_ms = parse_time_to_ms(END)

    download_coinm_1s_ohlcv_from_aggtrades(
        symbol=SYMBOL,
        out_csv=OUT_CSV,
        start_ms=start_ms,
        end_ms=end_ms,
        sleep=SLEEP,
        timeout=TIMEOUT,
        limit=LIMIT,
        fill_missing=FILL_MISSING,
    )

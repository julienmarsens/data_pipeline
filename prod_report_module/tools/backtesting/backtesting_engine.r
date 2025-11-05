cat("[BOOT] --- entering backtesting_engine.R ---\n"); flush.console()
cat(file = stderr(), "[BOOT] --- entering backtesting_engine.R (stderr) ---\n"); flush.console()

cat("[BOOT] Top of script reached — forcing flush\n")
cat(file = stderr(), "[BOOT] Top of script reached — forcing flush (stderr)\n")
flush.console()

options(error = function() { traceback(2); q(status = 1) })

# ---------------------- DIAGNOSTIC BOOT TEST ----------------------
cat("[BOOT] Step 3 reached: clearing sinks (safe)\n"); flush.console()
try({
  n1 <- sink.number()
  n2 <- sink.number(type = "message")
  cat(file = stderr(), sprintf("[BOOT] Found %d output sinks, %d message sinks — closing safely\n", n1, n2))
  if (n1 > 0) for (i in seq_len(n1)) try(sink(NULL), silent = TRUE)
  if (n2 > 0) for (i in seq_len(n2)) try(sink(NULL, type = "message"), silent = TRUE)
}, silent = TRUE)

# ---------------------- END SAFE SINK CLEAR ----------------------

# Define minimal logging helpers (since we call them later)
boot_log <- function(msg) {
  ts <- format(Sys.time(), "%Y-%m-%d %H:%M:%S")
  cat(sprintf("[BOOT %s] %s\n", ts, msg))
  cat(file = stderr(), sprintf("[BOOT %s] %s\n", ts, msg))
  flush.console()
}

log_try <- function(label, expr) {
  boot_log(paste0("START: ", label))
  out <- try(expr, silent = TRUE)
  if (inherits(out, "try-error")) {
    boot_log(paste0("ERROR in ", label, ": ", as.character(out)))
    traceback(2)
    q(status = 1)
  } else {
    boot_log(paste0("OK: ", label))
  }
  invisible(out)
}
# ---------------------- END DIAGNOSTIC BOOT TEST ----------------------

# ======================================================================
# Cleanup working environment
# ======================================================================
log_try("Set LANGUAGE", Sys.setenv(LANGUAGE = "en"))

# ✅ FIXED: preserve our own logging functions
log_try("Clear workspace", {
  keep <- c("boot_log", "log_try")
  all_objs <- ls(all.names = TRUE)
  to_remove <- setdiff(all_objs, keep)
  if (length(to_remove)) remove(list = to_remove, envir = .GlobalEnv)
})

log_try("Graphics off", graphics.off())
log_try("Set TZ=UTC", Sys.setenv(TZ = "UTC"))

log_try("Load package: Rcpp", library(Rcpp))
log_try("Load package: zoo", library(zoo))
log_try("Load package: stats", library(stats))

args <- commandArgs(trailingOnly = TRUE)


ASSET_1 <- args[1]
ASSET_2 <- args[2]
IN_SAMPLE_DATE_START  <- args[3]
IN_SAMPLE_DATE_END    <- args[4]
OUT_SAMPLE_DATE_START <- args[5]
OUT_SAMPLE_DATE_END   <- args[6]
OUT_SAMPLE_TIME_START <- args[7]
TRADER_ID             <- args[14]
perso_local_path      <- args[15]
perso_disk_path       <- args[16]

# parse numerics
param_vals <- suppressWarnings(as.numeric(args[8:13]))
if (any(is.na(param_vals))) {
  stop("Numeric params could not be parsed: ", paste(args[8:13], collapse = ", "))
}
SIGNAL_ANGLE  <- param_vals[1]
MARGIN        <- param_vals[2]
STEP_BACK     <- param_vals[3]
TRADING_ANGLE <- param_vals[4]
ORDER_SIZE    <- param_vals[5]
MAX_CROSSING  <- as.integer(param_vals[6])

# Paths
repo_base_path  <- file.path(perso_local_path, "data_pipeline", "prod_report_module", "tools", "backtesting")
path.to.source  <- file.path(perso_disk_path, "market_data", "sync_market_data")
path.to.results <- file.path(perso_disk_path, "results")
PNL_SAVING_PATH <- file.path(perso_local_path, "data_pipeline", "prod_report_module", "local_data", "backtest")

log_try("Create output directories", {
  dir.create(path.to.results, recursive = TRUE, showWarnings = FALSE)
  dir.create(PNL_SAVING_PATH, recursive = TRUE, showWarnings = FALSE)
  boot_log(paste("repo_base_path  =", repo_base_path))
  boot_log(paste("path.to.source  =", path.to.source))
  boot_log(paste("path.to.results =", path.to.results))
  boot_log(paste("PNL_SAVING_PATH =", PNL_SAVING_PATH))
})

# ======================================================================
# Configuration constants
# ======================================================================
MIN_SHARPE <- 0.01
MIN_CROSSING <- 100
MIN_PNL_DRAWDOWN  <- 1
EXCHANGE_NAME <- "binance-coin-futures"

export2pdf            <- TRUE
is.plot.results       <- TRUE
is.export.pnl.surface <- TRUE
is.daily.hedged       <- TRUE

export.name.flag <- "_"
RELOAD           <- TRUE

# Dates converted to numeric YYYYMMDD
is_start_num  <- as.integer(gsub("_", "", IN_SAMPLE_DATE_START))
is_end_num    <- as.integer(gsub("_", "", IN_SAMPLE_DATE_END))
oos_start_num <- as.integer(gsub("_", "", OUT_SAMPLE_DATE_START))
oos_end_num   <- as.integer(gsub("_", "", OUT_SAMPLE_DATE_END))

oos.start.timestamp <- OUT_SAMPLE_TIME_START
oos.dates <- c(oos_start_num, oos_end_num)
is.dates  <- c(is_start_num, is_end_num)

DISPLAY_CONFIGURATION_CPP <- FALSE
max.bid.ask.filter        <- FALSE

config_version <- paste(ASSET_1, ASSET_2)

# ======================================================================
# Load C++ modules and R sources (guard each separately)
# ======================================================================
log_try("Rcpp::sourceCpp quoter_algo.cpp",
        Rcpp::sourceCpp(file.path(repo_base_path, "quoter_algo.cpp")))
# If you actually use a different C++ file, adjust here:
log_try("Rcpp::sourceCpp quoter_rm.cpp",
        Rcpp::sourceCpp(file.path(repo_base_path, "quoter_rm.cpp")))

log_try("source data_tools.r",        source(file.path(repo_base_path, "data_tools.r")))
log_try("source product_specs.r",     source(file.path(repo_base_path, "product_specs.r")))
log_try("source stats.r",             source(file.path(repo_base_path, "stats.r")))
log_try("source core_quoter_slave.r", source(file.path(repo_base_path, "core_quoter_slave.r")))

boot_log("Early boot completed; continuing with main script")

# ======================================================================
# Single run parameters
# ======================================================================
is.single.run <- TRUE
relative.signal.angle.range   <- SIGNAL_ANGLE
relative.margin.range         <- MARGIN
relative.step.back.range      <- STEP_BACK
relative.trading.angle.range  <- TRADING_ANGLE
relative.order.size.range     <- ORDER_SIZE
num.crossing.2.limit.range    <- MAX_CROSSING

# ======================================================================
# Helper: available dates with diagnostics
# ======================================================================
find.available.dates <- function(dates.range, path.to.source, product.name, exchange.name) {
  cat(file = stderr(), sprintf("[DATES] scan path=%s for %s/%s\n",
                               path.to.source, product.name[1], product.name[2]))
  files <- list.files(path = path.to.source)
  if (length(files) == 0) {
    cat(file = stderr(), sprintf("[ERROR] No files in %s\n", path.to.source))
    return(character(0))
  }
  parts <- strsplit(files, "__")
  mat <- try(do.call(rbind, parts), silent = TRUE)
  if (inherits(mat, "try-error") || ncol(mat) < 5) {
    cat(file = stderr(), "[ERROR] Unexpected filename structure; need at least 5 fields separated by '__'\n")
    return(character(0))
  }
  expected <- paste(exchange.name[1], exchange.name[2], product.name[1], product.name[2], sep = "__")
  head4 <- apply(mat[, 1:4, drop = FALSE], 1, paste, collapse = "__")
  idx <- which(head4 == expected)
  if (!length(idx)) {
    cat(file = stderr(),
        sprintf("[WARN] No matching files for pattern '%s'. Some examples: %s\n",
                expected, paste(head(files, 5), collapse = ", ")))
    return(character(0))
  }
  dates_raw <- gsub("\\..*$", "", mat[idx, 5])
  dates_raw <- gsub("_", "", dates_raw)
  keep <- dates_raw >= dates.range[1] & dates_raw <= dates.range[2]
  out <- dates_raw[keep]
  if (!length(out)) {
    cat(file = stderr(),
        sprintf("[WARN] No dates in range [%s,%s]. Available for pattern: %s\n",
                dates.range[1], dates.range[2], paste(sort(unique(dates_raw)), collapse = ", ")))
  }
  return(out)
}

# ======================================================================
# Product lists
# ======================================================================
exchange.names.lst    <- list()
instrument.type.lst   <- list()
product.names.lst     <- list()

instrument.type.lst[[1]] <- c("INVERSE", "INVERSE")
exchange.names.lst[[1]]  <- c(EXCHANGE_NAME, EXCHANGE_NAME)
product.names.lst[[1]]   <- c(ASSET_1, ASSET_2)

# ======================================================================
# MAIN
# ======================================================================
for (z in seq_along(product.names.lst)) {

  exchange.names  <- exchange.names.lst[[z]]
  instrument.type <- instrument.type.lst[[z]]
  product.names   <- product.names.lst[[z]]

  grid.util.surface <- NULL
  pnl.util.surface  <- list()

  # Dates (with logging)
  is.dates.vect  <- find.available.dates(is.dates,  path.to.source, product.names, exchange.names)
  oos.dates.vect <- find.available.dates(oos.dates, path.to.source, product.names, exchange.names)

  safe_head <- function(x) if (length(x)) head(x, 1) else NA
  safe_tail <- function(x) if (length(x)) tail(x, 1) else NA

  cat(file=stderr(), "\n###########################################################################################################\n")
  cat(file=stderr(), paste("[", Sys.time(), "] Starting new grid search for pair: ",
                           toupper(exchange.names[1]), "-", product.names[1],
                           " versus ", toupper(exchange.names[2]), "-", product.names[2], "\n", sep=""))
  cat(file=stderr(), paste("[", Sys.time(), "] IS start date: ", safe_head(is.dates.vect),
                           " end date: ", safe_tail(is.dates.vect), "\n", sep=""))
  cat(file=stderr(), paste("[", Sys.time(), "] OOS start date: ", safe_head(oos.dates.vect),
                           " end date: ", safe_tail(oos.dates.vect), "\n", sep=""))
  cat(file=stderr(), "###########################################################################################################\n")

  if (length(is.dates.vect) == 0 || length(oos.dates.vect) == 0) {
    stop(sprintf("[FATAL] No available dates in %s for %s-%s between %s/%s and %s/%s",
                 path.to.source, product.names[1], product.names[2],
                 is.dates[1], is.dates[2], oos.dates[1], oos.dates[2]))
  }

  num.loops  <- length(relative.signal.angle.range) * length(relative.margin.range) * length(relative.step.back.range) *
                length(relative.trading.angle.range) * length(relative.order.size.range) * length(num.crossing.2.limit.range)
  iter.num   <- 1
  num.optima <- 0

  product.name.std.a  <- paste(toupper(gsub("-", "_", product.names[1])), "_", instrument.type[1], "_", toupper(exchange.names[1]), sep="")
  product.name.std.b  <- paste(toupper(gsub("-", "_", product.names[2])), "_", instrument.type[2], "_", toupper(exchange.names[2]), sep="")

  if (export2pdf) {
    pdf_file <- file.path(path.to.results, paste0("PDF_", config_version, export.name.flag, ".pdf"))
    cat(file = stderr(), sprintf("[PDF] Opening %s\n", pdf_file))
    pdf(file = pdf_file)
  }

  # Specs (defensive)
  prod.spec.a <- product.specs[[product.name.std.a]]
  prod.spec.b <- product.specs[[product.name.std.b]]
  if (is.null(prod.spec.a) || is.null(prod.spec.b)) {
    stop(sprintf("[FATAL] Missing product specs for %s or %s", product.name.std.a, product.name.std.b))
  }

  type.a <- instrument.type[1]; type.b <- instrument.type[2]
  fx.a <- as.numeric(prod.spec.a$fx.rate);         fx.b <- as.numeric(prod.spec.b$fx.rate)
  min.order.size.a <- as.numeric(prod.spec.a$min.order.size)
  min.order.size.b <- as.numeric(prod.spec.b$min.order.size)
  lot.size.a  <- as.numeric(prod.spec.a$lot.size); lot.size.b <- as.numeric(prod.spec.b$lot.size)
  tick.size.a <- as.numeric(prod.spec.a$tick.size); tick.size.b <- as.numeric(prod.spec.b$tick.size)
  t.fees.maker.a <- as.numeric(prod.spec.a$t.fees.maker); t.fees.maker.b <- as.numeric(prod.spec.b$t.fees.maker)
  t.fees.taker.a <- as.numeric(prod.spec.a$t.fees.taker); t.fees.taker.b <- as.numeric(prod.spec.b$t.fees.taker)

  # Load data
  if (RELOAD) {
    prices.bbo.a.b     <- read_r_bbo_cpp_repl(exchange.names, product.names, is.dates.vect,  path.to.source)
    prices.bbo.a.b.oos <- read_r_bbo_cpp_repl(exchange.names, product.names, oos.dates.vect, path.to.source)
    cat(file = stderr(),
        sprintf("[DATA] Loaded IS=%d rows, OOS=%d rows\n", nrow(prices.bbo.a.b), nrow(prices.bbo.a.b.oos)))

    if (is.single.run) {
      timestamp.oos <- as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01")
      idx.oos.time  <- substr(timestamp.oos, 1, 19) >= paste(paste(substr(oos.dates[1], 1, 4), "-",
                                                                   substr(oos.dates[1], 5, 6), "-",
                                                                   substr(oos.dates[1], 7, 8), sep=""),
                                                             " ", oos.start.timestamp, sep="")
      prices.bbo.a.b.oos <- prices.bbo.a.b.oos[idx.oos.time,]
      cat(file = stderr(), sprintf("[DATA] Filtered OOS by start time → %d rows\n", nrow(prices.bbo.a.b.oos)))
    }

    names(prices.bbo.a.b)     <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")
    names(prices.bbo.a.b.oos) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")

    prices.bbo.a.b.org     <- prices.bbo.a.b
    prices.bbo.a.b.org.oos <- prices.bbo.a.b.oos
  } else {
    prices.bbo.a.b     <- prices.bbo.a.b.org
    prices.bbo.a.b.oos <- prices.bbo.a.b.org.oos
  }

  if (nrow(prices.bbo.a.b) == 0 || nrow(prices.bbo.a.b.oos) == 0) {
    stop("[FATAL] Empty IS or OOS price matrices after loading.")
  }

  # EOD indices
  if (is.daily.hedged) {
    idx.is.eod  <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                     nrow(prices.bbo.a.b))
    idx.oos.eod <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                     nrow(prices.bbo.a.b.oos))
    daily.date.is <- unique(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
    daily.date    <- unique(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
  } else {
    idx.is.eod  <- NULL
    idx.oos.eod <- NULL
  }

  # Regression (guard slope)
  regression.length <- nrow(prices.bbo.a.b)
  lm_b_a <- lm(bid.b ~ bid.a, data = head(prices.bbo.a.b, regression.length))
  if (length(lm_b_a$coefficients) < 2 || is.na(lm_b_a$coefficients[2]) || lm_b_a$coefficients[2] == 0) {
    stop("[FATAL] Invalid regression slope (NA or zero) — cannot compute base.direction.")
  }
  intercept.regression <- lm_b_a$coefficients[1]
  slope.regression     <- lm_b_a$coefficients[2]
  base.direction <- 1 / slope.regression

  # Pre FX convert
  prices.bbo.a.b[, c("bid.a","ask.a","bid.b","ask.b")] <-
    t(t(prices.bbo.a.b[, c("bid.a","ask.a","bid.b","ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))

  order.size.range <- relative.order.size.range

  idx.bid.ask.not.null.a <- which(prices.bbo.a.b[,"bid.a"]-prices.bbo.a.b[,"ask.a"]!=0)
  idx.bid.ask.not.null.b <- which(prices.bbo.a.b[,"bid.b"]-prices.bbo.a.b[,"ask.b"]!=0)
  if (!length(idx.bid.ask.not.null.a) || !length(idx.bid.ask.not.null.b)) {
    stop("[FATAL] All bid-ask spreads are zero — malformed data?")
  }

  minimum.spreads <- c(min(prices.bbo.a.b[idx.bid.ask.not.null.a,"ask.a"]-prices.bbo.a.b[idx.bid.ask.not.null.a,"bid.a"]),
                       min(prices.bbo.a.b[idx.bid.ask.not.null.b,"ask.b"]-prices.bbo.a.b[idx.bid.ask.not.null.b,"bid.b"]))
  tickSize.a <- min(tick.size.a*fx.a, minimum.spreads[1])
  tickSize.b <- min(tick.size.b*fx.b, minimum.spreads[2])

  cat(file=stderr(), sprintf("[INFO] Starting grid search: total loops = %d\n", num.loops)); flush.console()


  # ====================================================================
  # CORE GRID
  # ====================================================================
  for (i in seq_along(relative.signal.angle.range)) {

    relative.signal.angle <- relative.signal.angle.range[i]
    base.signal.angle     <- relative.signal.angle*pi/4 + pi/4

    price.2.use <- prices.bbo.a.b

    # signal & synthetic quotes
    signal.vector            <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
    signal.angle             <- atan(signal.vector[2]/signal.vector[1]) + pi
    normalized.signal.vector <- signal.vector / sqrt(sum(signal.vector^2))
    signal.prices <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                             price.2.use[, "ask.b"] * normalized.signal.vector[2],
                           price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                             price.2.use[, "bid.b"] * normalized.signal.vector[2])

    for (j in seq_along(relative.margin.range)) {
      relative.margin <- relative.margin.range[j]
      base.margin     <- max(abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
                             abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b))
      margin          <- base.margin * relative.margin

      for (k in seq_along(relative.step.back.range)) {
        stepback <- relative.step.back.range[k] * margin

        theo.price        <- (signal.prices[1,1] + signal.prices[1,2]) / 2
        margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
        margin.inv.slope  <- margin.inv.vector[2] / margin.inv.vector[1]

        dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                   price.2.use[, "bid.a"], price.2.use[, "ask.a"],
                                   price.2.use[, "bid.b"], price.2.use[, "ask.b"],
                                   theo.price, margin, stepback,
                                   margin.inv.vector[1], margin.inv.vector[2],
                                   margin.inv.slope,
                                   normalized.signal.vector[1], normalized.signal.vector[2],
                                   tickSize.a, tickSize.b)

        quote.level <- data.frame(price.2.use,
                                  pmax(0, dPrice$aSellLevelA), pmax(0, dPrice$aBuyLevelA),
                                  pmax(0, dPrice$aSellLevelB), pmax(0, dPrice$aBuyLevelB))
        names(quote.level) <- c("timedate","bid.a","ask.a","bid.b","ask.b",
                                "aSellLevelA","aBuyLevelA","aSellLevelB","aBuyLevelB")

        if (!any(dPrice$moveTheoPriceVec != 0)) next

        for (l in seq_along(relative.trading.angle.range)) {
          base.trading.angle        <- relative.trading.angle.range[l]*1/4*pi + 1/4*pi
          trading.vector            <- c(cos(base.trading.angle), -sin(base.trading.angle))
          normalized.trading.vector <- trading.vector / sqrt(sum(trading.vector^2))

          for (m in seq_along(relative.order.size.range)) {

            order.size         <- abs(order.size.range[m] * normalized.trading.vector)
            typical.order.size <- order.size.range[m]

            for (n in seq_along(num.crossing.2.limit.range)) {
              nc2l <- num.crossing.2.limit.range[n]

              base.order.size.a <- order.size[1]
              base.order.size.b <- order.size[2]

              safe.position <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"],
                                                    quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                    quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"],
                                                    quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                    nc2l, base.order.size.a, base.order.size.b)

              clean.order.rounded <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                           sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)

              safe.position.a.b <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))

              idx.xing <- clean.order.rounded[,1] != 0 | clean.order.rounded[,2] != 0

              precision.prices <- 1e-5
              idx.1 <- quote.level[-nrow(quote.level),"aSellLevelA"] - quote.level[-1,"bid.a"] <=  precision.prices
              idx.2 <- quote.level[-nrow(quote.level),"aBuyLevelA"]  - quote.level[-1,"ask.a"] >= -precision.prices
              idx.3 <- quote.level[-nrow(quote.level),"aSellLevelB"] - quote.level[-1,"bid.b"] <=  precision.prices
              idx.4 <- quote.level[-nrow(quote.level),"aBuyLevelB"]  - quote.level[-1,"ask.b"] >= -precision.prices

              trade.price.raw <- cbind(rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)),
                                       rep(0, nrow(safe.position.a.b)))
              trade.price.raw[which(idx.1)+1,] <- cbind(quote.level[which(idx.1),"aSellLevelA"],
                                                        quote.level[which(idx.1),"aSellLevelA"],
                                                        quote.level[which(idx.1)+1,"ask.b"],
                                                        quote.level[which(idx.1)+1,"ask.b"])
              trade.price.raw[which(idx.2)+1,] <- cbind(quote.level[which(idx.2),"aBuyLevelA"],
                                                        quote.level[which(idx.2),"aBuyLevelA"],
                                                        quote.level[which(idx.2)+1,"bid.b"],
                                                        quote.level[which(idx.2)+1,"bid.b"])
              trade.price.raw[which(idx.3)+1,] <- cbind(quote.level[which(idx.3)+1,"ask.a"],
                                                        quote.level[which(idx.3)+1,"ask.a"],
                                                        quote.level[which(idx.3),"aSellLevelB"],
                                                        quote.level[which(idx.3),"aSellLevelB"])
              trade.price.raw[which(idx.4)+1,] <- cbind(quote.level[which(idx.4)+1,"bid.a"],
                                                        quote.level[which(idx.4)+1,"bid.a"],
                                                        quote.level[which(idx.4),"aBuyLevelB"],
                                                        quote.level[which(idx.4),"aBuyLevelB"])

              trade.price <- data.frame(prices.bbo.a.b[,1], trade.price.raw)

              transaction.fees <- cbind(rep(0, nrow(safe.position.a.b)),
                                        rep(0, nrow(safe.position.a.b)))

              order.fees <- clean.order.rounded * 0
              order.fees[which(idx.1),1]   <- clean.order.rounded[which(idx.1)+1,1]
              order.fees[which(idx.1)+1,2] <- clean.order.rounded[which(idx.1)+1,2]
              order.fees[which(idx.2),1]   <- clean.order.rounded[which(idx.2)+1,1]
              order.fees[which(idx.2)+1,2] <- clean.order.rounded[which(idx.2)+1,2]
              order.fees[which(idx.3)+1,1] <- clean.order.rounded[which(idx.3)+1,1]
              order.fees[which(idx.3),2]   <- clean.order.rounded[which(idx.3)+1,2]
              order.fees[which(idx.4)+1,1] <- clean.order.rounded[which(idx.4)+1,1]
              order.fees[which(idx.4),2]   <- clean.order.rounded[which(idx.4)+1,2]

              transaction.fees[which(idx.1)+1,] <- cbind(abs(order.fees[which(idx.1),1])*t.fees.maker.a, abs(order.fees[which(idx.1)+1,2])*t.fees.taker.b)
              transaction.fees[which(idx.2)+1,] <- cbind(abs(order.fees[which(idx.2),1])*t.fees.maker.a, abs(order.fees[which(idx.2)+1,2])*t.fees.taker.b)
              transaction.fees[which(idx.3)+1,] <- cbind(abs(order.fees[which(idx.3)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.3),2])*t.fees.maker.b)
              transaction.fees[which(idx.4)+1,] <- cbind(abs(order.fees[which(idx.4)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.4),2])*t.fees.maker.b)

              transaction.fees.cum <- cbind(cumsum(transaction.fees[,1]), cumsum(transaction.fees[,2]))

              # ==============================
              # Daily-hedged IS PnL
              # ==============================
              previous.quote.position.a <- 0
              previous.quote.position.b <- 0
              liquidation.mid.price.a <- NULL
              liquidation.mid.price.b <- NULL
              trade.serie.a <- NULL
              trade.serie.b <- NULL
              fees.serie.a  <- NULL
              fees.serie.b  <- NULL
              event_times_is   <- numeric(0)
              eod_event_idx_is <- integer(0)

              if (!length(idx.is.eod)) {
                stop("[FATAL] idx.is.eod not computed — check IS dataset timestamps.")
              }

              for (w in seq_along(idx.is.eod)) {
                pre_n <- if (is.null(trade.serie.a)) 0 else nrow(trade.serie.a)
                if (w == 1) idx.day <- 1:idx.is.eod[1] else idx.day <- (idx.is.eod[w-1]+1):idx.is.eod[w]

                idx.xing.day   <- idx.xing[idx.day]
                timestamps_day <- prices.bbo.a.b[idx.day, "time_seconds"]

                entry.mid.price.a <- ifelse(is.null(liquidation.mid.price.a),
                                            head(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2,
                                            liquidation.mid.price.a)
                entry.mid.price.b <- ifelse(is.null(liquidation.mid.price.b),
                                            head(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2,
                                            liquidation.mid.price.b)
                liquidation.mid.price.a <- tail(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
                liquidation.mid.price.b <- tail(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2

                if (any(idx.xing.day)) {
                  rows <- which(idx.xing.day)
                  t.fees.bc.a <- as.numeric(transaction.fees[idx.day[rows], 1] / trade.price[idx.day[rows], 3])
                  t.fees.bc.b <- as.numeric(transaction.fees[idx.day[rows], 2] / trade.price[idx.day[rows], 5])

                  fees.serie.a  <- c(fees.serie.a, 0)
                  fees.serie.b  <- c(fees.serie.b, 0)
                  if (is.null(trade.serie.a)) trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a) else trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                  if (is.null(trade.serie.b)) trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b) else trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))

                  trade.serie.a <- rbind(trade.serie.a, cbind(-clean.order.rounded[idx.day[rows], 1], trade.price[idx.day[rows], 2]))
                  trade.serie.b <- rbind(trade.serie.b, cbind(-clean.order.rounded[idx.day[rows], 2], trade.price[idx.day[rows], 4]))

                  fees.serie.a  <- c(fees.serie.a, t.fees.bc.a)
                  fees.serie.b  <- c(fees.serie.b, t.fees.bc.b)

                  previous.quote.position.a <- previous.quote.position.a + sum(-clean.order.rounded[idx.day[rows], 1])
                  previous.quote.position.b <- previous.quote.position.b + sum(-clean.order.rounded[idx.day[rows], 2])

                  fees.serie.a  <- c(fees.serie.a, 0)
                  fees.serie.b  <- c(fees.serie.b, 0)
                  trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                  trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))

                  event_times_is   <- c(event_times_is, timestamps_day[1], timestamps_day[rows], tail(timestamps_day, 1))
                  eod_event_idx_is <- c(eod_event_idx_is, nrow(trade.serie.a))

                } else {
                  fees.serie.a  <- c(fees.serie.a, 0)
                  fees.serie.b  <- c(fees.serie.b, 0)
                  if (is.null(trade.serie.a)) trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a) else trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                  if (is.null(trade.serie.b)) trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b) else trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                  trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                  trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))
                  event_times_is   <- c(event_times_is, timestamps_day[1], tail(timestamps_day, 1))
                  eod_event_idx_is <- c(eod_event_idx_is, nrow(trade.serie.a))
                  fees.serie.a  <- c(fees.serie.a, 0)
                  fees.serie.b  <- c(fees.serie.b, 0)
                }
              }

              pnl.dsc.a <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2]-fees.serie.a)
              pnl.dsc.b <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2]-fees.serie.b)
              pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]), cumsum(pnl.dsc.a[,2]))
              pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]), cumsum(pnl.dsc.b[,2]))
              pnl.cum.dbg.a <- as.numeric(pnl.cum.a[,1] + pnl.cum.a[,2]*trade.serie.a[,2])
              pnl.cum.dbg.b <- as.numeric(pnl.cum.b[,1] + pnl.cum.b[,2]*trade.serie.b[,2])
              pnl.cum.a <- pnl.cum.dbg.a
              pnl.cum.b <- pnl.cum.dbg.b

              pnl.wo.mh <- pnl.cum.a + pnl.cum.b
              stopifnot(length(pnl.wo.mh) == length(event_times_is))
              event_times_is_posix <- as.POSIXct(event_times_is/1000, origin = "1970-01-01", tz = "UTC")

              cat(file=stderr(), paste("[",Sys.time(),"]",
                                       paste0(" -- [", iter.num, "/", num.loops, '] completed --- optima # ',
                                              num.optima, " PnL: ", tail(pnl.wo.mh,1),
                                              " --- i:",i," # j:",j," # k:", k," # l:",l," # m:", m, "# n:", n), "\n", sep=""))
              iter.num <- iter.num + 1

              # Daily IS PnL
              if (length(idx.is.eod) != length(unique(substr(event_times_is_posix, 1, 10)))) {
                cat(file=stderr(), "[WARN] IS day count mismatch between indices and event timestamps.\n")
              }
              stopifnot(length(eod_event_idx_is) == length(daily.date.is))
              cum_eod_is   <- pnl.wo.mh[eod_event_idx_is]
              daily_pnl_is <- c(cum_eod_is[1], diff(cum_eod_is))
              sharpe.ratio <- mean(daily_pnl_is, na.rm = TRUE) / sd(daily_pnl_is, na.rm = TRUE)

              if (is.single.run) {
                is_df <- data.frame(
                  plot_index = seq_along(pnl.wo.mh),
                  time_ms    = as.numeric(event_times_is),
                  time_iso   = event_times_is_posix,
                  date       = substr(event_times_is_posix, 1, 10),
                  cum_pnl    = pnl.wo.mh,
                  leg_a      = as.numeric(pnl.cum.a),
                  leg_b      = as.numeric(pnl.cum.b)
                )
                write.table(is_df, file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_IS.csv")),
                            quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)

                is_daily_df <- data.frame(
                  date = daily.date.is,
                  daily_pnl = as.numeric(daily_pnl_is),
                  cum_pnl_day_eod = as.numeric(cum_eod_is),
                  stringsAsFactors = FALSE
                )
                write.table(is_daily_df, file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_IS_daily.csv")),
                            quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
              }

              # -------------------------
              # OOS backtest (guarded)
              # -------------------------
              if (nrow(prices.bbo.a.b.oos) == 0) stop("[ERROR] OOS dataset empty")
              need_cols <- c("time_seconds","bid.a","ask.a","bid.b","ask.b")
              if (!all(need_cols %in% names(prices.bbo.a.b.oos))) {
                stop(sprintf("[ERROR] OOS dataset columns invalid. Got: %s",
                             paste(names(prices.bbo.a.b.oos), collapse=", ")))
              }
              if (!length(idx.oos.eod)) {
                cat(file=stderr(), "[WARN] No OOS EOD indices; results may be unreliable.\n")
              }

              pnl.wo.mh.oos.lst <- tryCatch({
                run.oos.sim(prices.bbo.a.b.oos,
                            normalized.signal.vector,
                            margin,
                            stepback,
                            tickSize.a,
                            tickSize.b,
                            normalized.trading.vector,
                            base.order.size.a,
                            base.order.size.b,
                            nc2l,
                            fx.a,
                            fx.b,
                            min.order.size.a,
                            min.order.size.b,
                            t.fees.maker.a,
                            t.fees.maker.b,
                            t.fees.taker.a,
                            t.fees.taker.b,
                            type.a,
                            type.b,
                            is.daily.hedged,
                            idx.oos.eod,
                            daily.date)
              }, error = function(e) {
                cat(file=stderr(), sprintf("[ERROR] run.oos.sim failed: %s\n", e$message))
                return(NULL)
              })
              if (is.null(pnl.wo.mh.oos.lst)) next

              pnl.wo.mh.oos <- pnl.wo.mh.oos.lst$pnl.wo.mh

              # reconstruct OOS event timestamps (defensive)
              idx.xing_oos                <- pnl.wo.mh.oos.lst$idx.xing
              idx.crossing.all.raw        <- sort(c(which(idx.xing_oos), idx.oos.eod, idx.oos.eod + 1))
              idx.crossing.all.clean      <- c(idx.crossing.all.raw[-length(idx.crossing.all.raw)], nrow(prices.bbo.a.b.oos))
              event_oos_ms                <- prices.bbo.a.b.oos[idx.crossing.all.clean, 1]
              event_oos_posix             <- as.POSIXct(event_oos_ms/1000, origin="1970-01-01", tz="UTC")

              dates_vec_oos <- pnl.wo.mh.oos.lst$daily.date.serie
              u_dates_oos   <- unique(dates_vec_oos)
              eod_vals_oos  <- sapply(u_dates_oos, function(d) {
                vals <- pnl.wo.mh.oos[dates_vec_oos == d]
                if (length(vals) == 0) return(NA_real_) else tail(vals, 1)
              })

              if (all(is.na(eod_vals_oos))) {
                stop("[ERROR] OOS EOD vector entirely NA — check OOS date mapping.")
              }

              daily_pnl_oos <- diff(c(0, eod_vals_oos))
              sharpe.ratio.oos <- if (sd(daily_pnl_oos, na.rm = TRUE) == 0) NA_real_ else
                mean(daily_pnl_oos, na.rm = TRUE) / sd(daily_pnl_oos, na.rm = TRUE)

              if (is.single.run) {
                oos_df <- data.frame(
                  plot_index = seq_along(pnl.wo.mh.oos),
                  time_ms    = as.numeric(event_oos_ms),
                  time_iso   = event_oos_posix,
                  date       = substr(event_oos_posix, 1, 10),
                  cum_pnl    = pnl.wo.mh.oos
                )
                write.table(oos_df, file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS.csv")),
                            quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)

                oos_daily_df <- data.frame(
                  date = u_dates_oos,
                  daily_pnl = as.numeric(daily_pnl_oos),
                  cum_pnl_day_eod = as.numeric(eod_vals_oos),
                  stringsAsFactors = FALSE
                )
                write.table(oos_daily_df, file.path(PNL_SAVING_PATH, paste0(TRADER_ID, "_backtest_OOS_daily.csv")),
                            quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
              }

              # Plots
              usd.profit.cum <- cbind(pnl.cum.a, pnl.cum.b)
              if (is.plot.results) {
                par(mfrow = c(4,1), cex.main = 0.8)
                titl.relative <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                       relative.trading.angle.range[l],"#",relative.order.size.range[m],"#", num.crossing.2.limit.range[n], sep="")
                plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh), max(pnl.wo.mh)),
                     main=paste("In-Sample run (", head(is.dates.vect,1), "-", tail(is.dates.vect,1), ")", sep=""),
                     xlab=titl.relative)

                titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                              stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                              order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")
                plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2]),
                                                 max(pnl.wo.mh, usd.profit.cum[,1], usd.profit.cum[,2])),
                     main=titl, cex.main=0.5)
                lines(usd.profit.cum[,1], col=2, lty=2)
                lines(usd.profit.cum[,2], col=3, lty=2)

                plot(safe.position.a.b[,1], col=2, type='l',
                     ylim=c(min(safe.position.a.b[,1], safe.position.a.b[,2]),
                            max(safe.position.a.b[,1], safe.position.a.b[,2])),
                     ylab="inventory", xlab="red -> product 1, green -> product 2",
                     main=paste("max inventories (USD) : (", max(abs(safe.position.a.b[,1])), " - ", max(abs(safe.position.a.b[,2])), ")", sep=""))
                lines(safe.position.a.b[,2], col=3)

                plot(pnl.wo.mh.oos, type='l', ylim=c(min(pnl.wo.mh.oos), max(pnl.wo.mh.oos)),
                     main=paste("OOS-Run(", head(oos.dates.vect,1), "-", tail(oos.dates.vect,1), ")", sep=""))
              }

              # Surfaces / logs
              if (is.export.pnl.surface) {
                titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                              stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                              order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")
                pnl.util.surface[[titl]] <- zoo(pnl.wo.mh, order.by = as.numeric(event_times_is))
              }

              status.log <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                  relative.trading.angle.range[l],"#",relative.order.size.range[m],"#",num.crossing.2.limit.range[n],
                                  " ### pnl is: ", tail(pnl.wo.mh, 1),
                                  " ### pnl oos: ", tail(pnl.wo.mh.oos, 1),
                                  " ### sharpe.ratio: ", sharpe.ratio,
                                  " ### sharpe.ratio oos: ", sharpe.ratio.oos,
                                  sep="")
              cat(file=stderr(), paste("[",Sys.time(),"] New Optima found : ", status.log, "\n", sep=""))
              num.optima <- num.optima + 1

            } # n
          } # m
        } # l
      } # k
    } # j
  } # i

  # ====================================================================
  # Export utility surface and close PDF
  # ====================================================================
  if (!is.null(grid.util.surface)) {
    colnames(grid.util.surface) <- c("absolute.parameters","relative.signal.angle","relative.margin","relative.step.back",
                                     "relative.trading.angle","relative.order.size","num.crossing.2.limit",
                                     "sharpe.ratio","pnl")
    to_export <- file.path(path.to.results, paste0("gs_", product.name.std.a, "_vs_", product.name.std.b, "_", export.name.flag, ".csv"))
    write.table(grid.util.surface, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
  }

  if (length(pnl.util.surface)) {
    pnl.util.surface <- pnl.util.surface[sapply(pnl.util.surface, function(x) length(x) > 0)]
    if (length(pnl.util.surface)) {
      pnl.util.surface.raw <- do.call(merge, pnl.util.surface)
      if (!is.null(pnl.util.surface.raw) && length(pnl.util.surface.raw) > 0) {
        idx_ms <- as.numeric(index(pnl.util.surface.raw))
        time_seconds <- floor(idx_ms/1000)
        time_iso <- as.POSIXct(time_seconds, origin = "1970-01-01", tz = "UTC")
        pnl.util.surface.df <- data.frame(
          time_seconds = time_seconds,
          time_iso     = time_iso,
          coredata(pnl.util.surface.raw),
          check.names  = FALSE
        )
        to_export <- file.path(path.to.results, paste0("pnl_", product.name.std.a, "_vs_", product.name.std.b, "_", export.name.flag, ".csv"))
        write.table(pnl.util.surface.df, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
      } else {
        cat(file=stderr(), "[WARN] pnl.util.surface.raw empty — skipping CSV export.\n")
      }
    } else {
      cat(file=stderr(), "[WARN] No non-empty pnl.util.surface entries — skipping merge/export.\n")
    }
  } else {
    cat(file=stderr(), "[WARN] pnl.util.surface empty — skipping export.\n")
  }

  if (export2pdf) dev.off()

} # end z

# ======================================================================
# FINAL: Generate order + quote PDF from LAST_RUN_DATA (if captured)
# ======================================================================
try({
  if (exists("LAST_RUN_DATA")) {
    quote.level <- LAST_RUN_DATA$quote.level
    clean.order.rounded <- LAST_RUN_DATA$clean.order.rounded
    ASSET_1 <- LAST_RUN_DATA$ASSET_1
    ASSET_2 <- LAST_RUN_DATA$ASSET_2

    mid.a <- (quote.level$bid.a + quote.level$ask.a) / 2
    mid.b <- (quote.level$bid.b + quote.level$ask.b) / 2
    time.posix <- as.POSIXct(quote.level$timedate / 1000, origin="1970-01-01", tz="UTC")

    idx.order.a <- which(clean.order.rounded[,1] != 0)
    idx.order.b <- which(clean.order.rounded[,2] != 0)

    order_plot_pdf <- file.path(path.to.results, paste0("PDF_Orders_", ASSET_1, "_", ASSET_2, "_.pdf"))

    pdf(order_plot_pdf, width=10, height=10)
    par(mfrow=c(4,1), mar=c(3.5,4,2,1))

    plot(time.posix, mid.a, type='l', lwd=1.2,
         main=paste("Asset A:", ASSET_1, "- Mid Price & Orders"),
         xlab="Time", ylab="Mid Price")
    if (length(idx.order.a) > 0)
      points(time.posix[idx.order.a], mid.a[idx.order.a],
             col=ifelse(clean.order.rounded[idx.order.a,1] > 0, "green3", "red3"),
             pch=19, cex=0.6)
    legend("topleft", legend=c("Mid","Buy","Sell"),
           col=c("black","green3","red3"),
           lty=c(1,NA,NA), pch=c(NA,19,19), cex=0.8)

    plot(time.posix, mid.b, type='l', lwd=1.2,
         main=paste("Asset B:", ASSET_2, "- Mid Price & Orders"),
         xlab="Time", ylab="Mid Price")
    if (length(idx.order.b) > 0)
      points(time.posix[idx.order.b], mid.b[idx.order.b],
             col=ifelse(clean.order.rounded[idx.order.b,2] > 0, "green3", "red3"),
             pch=19, cex=0.6)
    legend("topleft", legend=c("Mid","Buy","Sell"),
           col=c("black","green3","red3"),
           lty=c(1,NA,NA), pch=c(NA,19,19), cex=0.8)

    plot(time.posix, quote.level$aBuyLevelA, type='l', lwd=1,
         main=paste("Asset A:", ASSET_1, "- Our Quoted Bid/Ask Levels"),
         xlab="Time", ylab="Price",
         ylim=range(c(quote.level$aBuyLevelA, quote.level$aSellLevelA), na.rm=TRUE))
    lines(time.posix, quote.level$aSellLevelA, lwd=1)
    lines(time.posix, mid.a, lwd=0.8, lty=2)
    legend("topleft", legend=c("Bid Quote","Ask Quote","Mid Price"),
           lty=c(1,1,2), lwd=c(1,1,0.8), cex=0.8)

    plot(time.posix, quote.level$aBuyLevelB, type='l', lwd=1,
         main=paste("Asset B:", ASSET_2, "- Our Quoted Bid/Ask Levels"),
         xlab="Time", ylab="Price",
         ylim=range(c(quote.level$aBuyLevelB, quote.level$aSellLevelB), na.rm=TRUE))
    lines(time.posix, quote.level$aSellLevelB, lwd=1)
    lines(time.posix, mid.b, lwd=0.8, lty=2)
    legend("topleft", legend=c("Bid Quote","Ask Quote","Mid Price"),
           lty=c(1,1,2), lwd=c(1,1,0.8), cex=0.8)

    dev.off()
    cat(file=stderr(), paste("[", Sys.time(), "] ✅ Saved extended order/quote PDF to:", order_plot_pdf, "\n"))
  } else {
    cat(file=stderr(), "[WARN] No LAST_RUN_DATA found — no order/quote PDF generated.\n")
  }
}, silent = FALSE)

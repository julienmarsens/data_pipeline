###########  Cleanup working environment
Sys.setenv (LANGUAGE="en")
remove(list=ls(all=T))
graphics.off()

options(digits=10)
Sys.setenv(TZ='UTC')

###########  load libs

library(zoo)
library(yaml)
library(doParallel)
library(foreach)

###########  constant configuration

# Get arguments
args <- commandArgs(trailingOnly = TRUE)

perso_local_path <- args[7]
perso_disk_path <- args[8]

config_version <- args[9]

repo_base_path  <- file.path(perso_local_path, "data_pipeline", "optima_finder", "tools", "grid_search")
grid_config_path <- file.path(perso_local_path, "data_pipeline", "optima_finder", "config")

config_path <- paste0(grid_config_path, "/", config_version, ".yaml")
config_file <- yaml::read_yaml(config_path)

MIN_SHARPE <- config_file$filtering$min_sharpe
MIN_CROSSING_VEC <- config_file$filtering$min_crossing_per_day * as.integer(args[10])

# Skew configuration
ENABLE_SKEW <- config_file$skew$enable_skew

#  Choose from pairs list & export statement
# plot
export2pdf <- config_file$output$export_to_pdf

is.daily.hedged       <- TRUE

if (export2pdf) {
  is.plot.results       <- TRUE
  is.export.pnl.surface <- TRUE
} else {
  is.plot.results       <- FALSE
  is.export.pnl.surface <- TRUE
}

export.name.flag   <- "grid" # ref. c(50, seq(from=100, to=500, by=100))

# if false don't reload data at each grid search (not each loop within a grid)
RELOAD             <- TRUE

# over one day
is_start_num <- as.integer(gsub("_", "", args[3]))
is_end_num <- as.integer(gsub("_", "", args[4]))
oos_start_num <- as.integer(gsub("_", "", args[5]))
oos_end_num <- as.integer(gsub("_", "", args[6]))

oos.dates      <- c(oos_start_num, oos_end_num) #
is.dates       <- c(is_start_num, is_end_num) #

# update conf parameters at the end of the file accordingly
DISPLAY_CONFIGURATION_CPP <- FALSE
# legacy filtering test
max.bid.ask.filter        <- FALSE

###########  Core HFT statarb quoter strategy - Init
library(rstudioapi)
library(zoo)

# home build libs
if (rstudioapi::isAvailable()) {
  PATH_2_ROOT <- dirname(dirname(dirname(rstudioapi::getSourceEditorContext()$path)))
} else {
  PATH_2_ROOT <- dirname(dirname(dirname(getwd())))
}

setwd(PATH_2_ROOT)
# data source
path.to.source  <- file.path(perso_disk_path, "market_data", "sync_market_data")
path.to.results  <- file.path(perso_disk_path, "results", args[11])

###########  Load and/or install R packages and libs

# C++ (pre-compiled package)
library(quoterPkg)
# R utils
source(paste(repo_base_path, "/", "data_tools.r", sep=""))
source(paste(repo_base_path, "/", "product_specs.r", sep=""))
source(paste(repo_base_path, "/", "stats.r", sep=""))
# oos slave script
source(paste(repo_base_path, "/", "core_quoter_slave_release.r", sep=""))

# --- Parallel setup (fork-based, shares memory with parent) ---
ncores_inner <- max(1, min(4, parallel::detectCores() %/% 3))
registerDoParallel(cores = ncores_inner)
cat(file=stderr(), paste0("[Parallel] Using ", ncores_inner, " cores for inner loop parallelization\n"))

# -------------------------------------------------
# Auto margin calibration
# -------------------------------------------------
calibrate_margin_range <- function(prices,
                                   normalized.signal.vector,
                                   margin.inv.vector,
                                   margin.inv.slope,
                                   tick.size.a,
                                   tick.size.b,
                                   fx.a,
                                   fx.b,
                                   min_crossings,
                                   max_crossings,
                                   max_range = 5000,   # keep default
                                   stepback.factors = c(0.5, 0.75, 0.9999)) {

  cat("\n[Calibration] --- Starting margin calibration ---\n")

  # --- construct signal prices ---
  signal.prices <- cbind(
    prices[,"bid.a"] * normalized.signal.vector[1] +
      prices[,"ask.b"] * normalized.signal.vector[2],
    prices[,"ask.a"] * normalized.signal.vector[1] +
      prices[,"bid.b"] * normalized.signal.vector[2]
  )

  theo.price <- mean(c(signal.prices[,1], signal.prices[,2]), na.rm=TRUE)
  cat("[Calibration] theo.price =", theo.price, "\n")
  cat("[Calibration] min_crossings =", min_crossings,
      "| max_crossings =", max_crossings, "\n")

  # --- build exploration sets ---
  finer_region    <- seq(4, 20, by = 4)
  fine_region     <- seq(20, 100, by = 10)
  coarse_region   <- seq(100, 1000, by = 50)
  coarser_region  <- seq(1000, max_range, by = 200)
  coarse_levels   <- unique(c(finer_region, fine_region, coarse_region, coarser_region))

  cat("[Calibration] candidate relative margins (multipliers) =",
      paste(coarse_levels, collapse = ", "), "\n")

  # --- compute minimum spreads (same as in loop) ---
  idx.bid.ask.not.null.a <- which(prices[,"bid.a"] < prices[,"ask.a"])
  idx.bid.ask.not.null.b <- which(prices[,"bid.b"] < prices[,"ask.b"])

  minimum.spreads <- c(
    min(prices[idx.bid.ask.not.null.a,"ask.a"] - prices[idx.bid.ask.not.null.a,"bid.a"], na.rm=TRUE),
    min(prices[idx.bid.ask.not.null.b,"ask.b"] - prices[idx.bid.ask.not.null.b,"bid.b"], na.rm=TRUE)
  )

  # --- base margin (identical to loop logic) ---
  base.margin <- max(
    abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
    abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b)
  )
  cat("[Calibration] base.margin =", base.margin, "\n")

  # --- evaluate all candidates across stepback factors ---
  test_scores <- sapply(coarse_levels, function(margin_candidate) {
    margin <- base.margin * margin_candidate

    scores_per_stepback <- sapply(stepback.factors, function(sb) {
      stepback <- sb * margin
      dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                 prices[,"bid.a"], prices[,"ask.a"],
                                 prices[,"bid.b"], prices[,"ask.b"],
                                 theo.price,
                                 theMargin   = margin,
                                 theStepback = stepback,
                                 margin.inv.vector[1], margin.inv.vector[2],
                                 margin.inv.slope,
                                 normalized.signal.vector[1], normalized.signal.vector[2],
                                 tick.size.a, tick.size.b,
                                 enableSkew = FALSE)

      score <- sum(dPrice$moveTheoPriceVec != 0)

      if (score < min_crossings || score > max_crossings) {
        return(-Inf)
      } else {
        return(score)
      }
    })

    best_score <- max(scores_per_stepback)
    return(best_score)
  })

  cat("[Calibration] test_scores by margin:\n")
  print(data.frame(
    margin_rel = coarse_levels,
    margin_abs = coarse_levels * base.margin,
    score = test_scores
  ))

  # --- choose best ---
  if (all(is.infinite(test_scores))) {
    # fallback: select margin whose raw crossings are closest to the band
    raw_scores <- sapply(coarse_levels, function(margin_candidate) {
      margin <- base.margin * margin_candidate
      stepback <- max(stepback.factors) * margin
      dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                 prices[,"bid.a"], prices[,"ask.a"],
                                 prices[,"bid.b"], prices[,"ask.b"],
                                 theo.price,
                                 theMargin   = margin,
                                 theStepback = stepback,
                                 margin.inv.vector[1], margin.inv.vector[2],
                                 margin.inv.slope,
                                 normalized.signal.vector[1], normalized.signal.vector[2],
                                 tick.size.a, tick.size.b,
                                 enableSkew = FALSE)
      return(sum(dPrice$moveTheoPriceVec != 0))
    })

    dist_to_band <- sapply(raw_scores, function(x) {
      if (x < min_crossings) return(min_crossings - x)  # below band
      if (x > max_crossings) return(x - max_crossings)  # above band
      return(0)
    })

    best_idx   <- which.min(dist_to_band)
    best_margin <- coarse_levels[best_idx]
    best_abs    <- best_margin * base.margin
    cat("[Calibration] ⚠️ WARNING: no valid margins in criteria, fallback to closest-to-band crossings =",
        best_margin, "| absolute =", best_abs, "| raw crossings =", raw_scores[best_idx], "\n")

    # refine around fallback best_margin
    if (best_margin %in% finer_region) {
      step_size <- 2
    } else if (best_margin %in% fine_region) {
      step_size <- 10
    } else if (best_margin %in% coarse_region) {
      step_size <- 30
    } else if (best_margin %in% coarser_region) {
      step_size <- 200
    } else {
      step_size <- 200
    }
    candidates <- seq(best_margin - 2*step_size,
                      best_margin + 2*step_size,
                      by = step_size)
    fine_range <- candidates[candidates > 0 & candidates <= max_range]

  } else {
    best_margin <- coarse_levels[which.max(test_scores)]
    best_abs    <- best_margin * base.margin
    cat("[Calibration] ✅ Best relative margin found =", best_margin,
        "| absolute =", best_abs,
        "| score =", max(test_scores, na.rm=TRUE), "\n")

    # Decide step size based on region
    if (best_margin %in% finer_region) {
      step_size <- 2
    } else if (best_margin %in% fine_region) {
      step_size <- 10
    } else if (best_margin %in% coarse_region) {
      step_size <- 30
    } else if (best_margin %in% coarser_region) {
      step_size <- 200
    } else {
      step_size <- 200
    }

    candidates <- seq(best_margin - 2*step_size,
                      best_margin + 2*step_size,
                      by = step_size)
    fine_range <- candidates[candidates > 0 & candidates <= max_range]
  }

  cat("[Calibration] Refined search grid (relative) =",
      paste(fine_range, collapse = ", "), "\n")
  cat("[Calibration] Refined search grid (absolute) =",
      paste(round(fine_range * base.margin, 4), collapse = ", "), "\n")
  cat("[Calibration] --- Calibration complete ---\n\n")

  return(fine_range)
}



###########  Setup configuration parameters
###########  Grid parameters

# relative.margin.range    <- seq(from=config_file$grid$margin_range[1], to=config_file$grid$margin_range[2], by=config_file$grid$margin_range_step)
# relative.margin.range <- fine_range
relative.step.back.range      <- c(config_file$grid$step_back_range[1], config_file$grid$step_back_range[2], config_file$grid$step_back_range[3])#c(0.5, 0.75, .9999)
relative.trading.angle.range  <- c(0, seq(from=config_file$grid$trading_angle_range[1], to=config_file$grid$trading_angle_range[2], by=config_file$grid$trading_angle_range_step))
relative.order.size.range     <- config_file$grid$order_size_range
num.crossing.2.limit.range    <- seq(from=config_file$grid$crossing_to_limit_range[1], to=config_file$grid$crossing_to_limit_range[2], by=config_file$grid$crossing_to_limit_range_step)

# single/production run
if(config_file$single_run_param$run) {

  relative.margin.range         <- config_file$single_run_param$margin
  relative.step.back.range      <- config_file$single_run_param$step_back
  relative.trading.angle.range  <- config_file$single_run_param$trading_angle
  relative.order.size.range     <- config_file$single_run_param$order_size
  num.crossing.2.limit.range    <- config_file$single_run_param$crossing_to_limit
}

# Products grid implementation

# build combination of product
build.comb <- function(products) {
  grid.prod <- expand.grid(products,products) # All permutations

  # remove pairs with the same product
  idx.same.prod  <- grid.prod[,1]==grid.prod[,2]
  grid.prod.dupl <- grid.prod[!idx.same.prod,]

  # remove same pairs but inverse prod
  grid.prod.unq <- grid.prod.dupl[1,]
  for(i in 2:nrow(grid.prod.dupl)) {
    condition <- is.na(match(grid.prod.dupl[i,1], grid.prod.unq[,2]) == match(grid.prod.dupl[i,2], grid.prod.unq[,1]))

    if(!condition) next

    grid.prod.unq <- rbind(grid.prod.unq, grid.prod.dupl[i,])
  }
  names(grid.prod.unq) <- c("product.a", "product.b")

  return(grid.prod.unq)
}

###########  Read product specs and build combination
###########  REM: at this stage, don't consider deribit
if(FALSE) {
  product.names    <- names(product.specs)[-c(1,2)]
  prod.combination <- build.comb(product.names)

  # create object list for input to function
  product.exchange.names.lst <- list()
  exchange.names.lst         <- list()
  product.names.lst          <- list()
  instrument.type.lst        <- list()

  prod.combination <- prod.combination[31:35,]

  for(i in 1:nrow(prod.combination)) {
    product.exchange.names.lst[[i]]        <- c(as.character(prod.combination[i,1]), as.character(prod.combination[i,2]))

    # format names
    prod.splt.a                            <- tolower(strsplit(product.exchange.names.lst[[i]][1], "_")[[1]][1:2])
    prod.splt.b                            <- tolower(strsplit(product.exchange.names.lst[[i]][2], "_")[[1]][1:2])

    exchange.names.lst[[i]]                <- c("binance-coin-futures", "binance-coin-futures")
    product.names.lst[[i]]                 <- c(paste(prod.splt.a[1], prod.splt.a[2], sep="-"), paste(prod.splt.b[1], prod.splt.b[2], sep="-"))
    instrument.type.lst[[i]]               <- c("INVERSE", "INVERSE") # i.e. SPOT, LINEAR, INVERSE
  }
}

###########################################################################################################
exchange.names.lst    <- list()
instrument.type.lst   <- list()
product.names.lst     <- list()

###########  New combination 2025.04.13
instrument.type.lst[[1]] <- c("INVERSE", "INVERSE")
exchange.names.lst[[1]]  <- c(config_file$assets$exchange_name, config_file$assets$exchange_name)
product.names.lst[[1]]   <- c(args[1], args[2])

######### HELPER FUNCTIONS
find.available.dates <- function(dates.range, path.to.source, product.name, exchange.name) {
  file_names           <- list.files(path = path.to.source)
  file_names_raw       <- strsplit(file_names, "__")
  product.exchange     <- paste(exchange.name[1], "__", exchange.name[2], "__", product.name[1], "__", product.name[2], sep="")
  file.name.prod.ex    <- apply(do.call(rbind, file_names_raw)[,1:4], 1, paste, collapse="__")
  idx.prod.ex          <- product.exchange == file.name.prod.ex

  raw_dates <- do.call(rbind, file_names_raw)[,5]
  raw_dates <- sub("\\..*$", "", raw_dates)      # drop extension
  raw_dates <- gsub("_", "", raw_dates)          # remove underscores
  available.date <- raw_dates[idx.prod.ex]

  idx.date <- available.date >= dates.range[1] & available.date <= dates.range[2]
  return(available.date[idx.date])
}

###########  Main cross exchange market making algorithm
###########  Main loop through each pair

for(z in 1:length(product.names.lst)) {

    exchange.names       <- exchange.names.lst[[z]]
    instrument.type      <- instrument.type.lst[[z]]
    product.names        <- product.names.lst[[z]]

    # utility surface to be exported
    grid.util.surface    <- NULL
    pnl.util.surface     <- list()
    skew.util.surface    <- list()

    ######### FILTER IS AND OOS DATES

    is.dates.vect         <- find.available.dates(is.dates, path.to.source, product.names, exchange.names)
    oos.dates.vect        <- find.available.dates(oos.dates, path.to.source, product.names, exchange.names)
    # verbose run pair names and is-oos dates
    cat(file=stderr(), "\n###########################################################################################################\n")
    cat(file=stderr(), paste("[",Sys.time(),"] Starting new grid search for pair: ", args[1], "-", args[2], "\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] IS start: ", head(is.dates.vect,1), " end: ", tail(is.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] OOS start: ", head(oos.dates.vect,1), " end: ", tail(oos.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), "###########################################################################################################\n")

    # check that data source is available, skip otherwise
    if(length(is.dates.vect)==0 || length(oos.dates.vect)==0) next

    ###########  naming convention
    product.name.std.a  <- paste(toupper(gsub("-", "_", product.names[1])), "_", instrument.type[1], "_", toupper(exchange.names[1]), sep="")
    product.name.std.b  <- paste(toupper(gsub("-", "_", product.names[2])), "_", instrument.type[2], "_", toupper(exchange.names[2]), sep="")

    ###########  Export to pdf file
    if(export2pdf) pdf(file = paste(path.to.results, "/chart_", args[1], "_", args[2], "_", export.name.flag, ".pdf", sep=""))

    ###########  lookup product specs
    prod.spec.a         <- product.specs[[product.name.std.a]]
    prod.spec.b         <- product.specs[[product.name.std.b]]

    type.a              <- instrument.type[1]
    type.b              <- instrument.type[2]

    fx.a                <- as.numeric(prod.spec.a$fx.rate)
    fx.b                <- as.numeric(prod.spec.b$fx.rate)

    min.order.size.a    <- as.numeric(prod.spec.a$min.order.size)
    min.order.size.b    <- as.numeric(prod.spec.b$min.order.size)

    lot.size.a          <-  as.numeric(prod.spec.a$lot.size)
    lot.size.b          <-  as.numeric(prod.spec.b$lot.size)

    tick.size.a         <-  as.numeric(prod.spec.a$tick.size) # to be considered as minimum price increment...in this implementation
    tick.size.b         <-  as.numeric(prod.spec.b$tick.size)

    t.fees.maker.a      <- as.numeric(prod.spec.a$t.fees.maker)
    t.fees.maker.b      <- as.numeric(prod.spec.b$t.fees.maker)

    t.fees.taker.a      <- as.numeric(prod.spec.a$t.fees.taker)
    t.fees.taker.b      <- as.numeric(prod.spec.b$t.fees.taker)

    ###########  Load data source
    if(RELOAD) {
      prices.bbo.a.b           <- read_r_bbo_cpp_repl(exchange.names, product.names, is.dates.vect, path.to.source)
      prices.bbo.a.b.oos       <- read_r_bbo_cpp_repl(exchange.names, product.names, oos.dates.vect, path.to.source)

      # comply with legacy naming convention
      names(prices.bbo.a.b)      <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")
      names(prices.bbo.a.b.oos)  <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b")

      prices.bbo.a.b.org       <- prices.bbo.a.b
      prices.bbo.a.b.org.oos   <- prices.bbo.a.b.oos
    } else {
      prices.bbo.a.b           <- prices.bbo.a.b.org
      prices.bbo.a.b.oos       <- prices.bbo.a.b.org.oos
    }

    if(is.daily.hedged) {

      idx.is.eod   <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                        length(prices.bbo.a.b[,"time_seconds"]))
      idx.oos.eod  <- c(which(!duplicated(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10)))[-1]-1,
                        length(prices.bbo.a.b.oos[,"time_seconds"]))

      # for daily sharpe calculation
      daily.date.is     <- unique(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
      daily.date        <- unique(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
    } else {
      idx.is.eod   <- NULL
      idx.oos.eod  <- NULL
    }

    # Pre-compute FX-converted OOS prices (used for OOS crossing cache)
    prices.bbo.a.b.oos.fx <- prices.bbo.a.b.oos
    prices.bbo.a.b.oos.fx[, c("bid.a", "ask.a", "bid.b", "ask.b")] <-
      t(t(prices.bbo.a.b.oos[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a, fx.a, fx.b, fx.b))

    ###########  Compute best cointegration direction based on linear regression

    # use full sample for regression
    regression.length <- nrow(prices.bbo.a.b)

    # === compute regression slope ===
    lm_b_a <- lm(bid.b ~ bid.a, data = head(prices.bbo.a.b, regression.length))
    intercept.regression <- lm_b_a$coefficients[1]
    slope.regression     <- lm_b_a$coefficients[2]
    base.direction <- 1/slope.regression

      # # Convert regression slope into absolute angle (deg and rad)
    slope_angle_rad <- atan(slope.regression)
    slope_angle_deg <- slope_angle_rad * 180/pi

    # --- Build a stable angle grid via multiplicative β perturbations ---
    # Baseline angle (deg) and slope β
    theta0_deg <- slope_angle_deg
    beta0 <- tan(theta0_deg * pi/180)

    # User knobs (unchanged elsewhere):
    r_max <- 0.60      # max relative change in β for the outer points (e.g., ±20%)
    n_vals <- 5        # number of angles to test (keep 5 to match your ±2 setup)

    # Geometric multipliers around 1, symmetric in log-space:
    delta <- log(1 + r_max)
    mult  <- exp(seq(-delta, delta, length.out = n_vals))

    # Apply to β, map back to angles; preserve β sign automatically
    beta_grid   <- beta0 * mult
    angles_deg  <- atan(beta_grid) * 180/pi

    # Keep strictly inside (0, 90) and add a tiny safety margin
    eps <- 1e-6
    angles_deg <- angles_deg[angles_deg > eps & angles_deg < 90 - eps]

    # (Optional) If θ is *extremely* close to 0° or 90°, ensure at least a tiny band:
    if (length(angles_deg) == 1) {
      # Use local small-angle approximation: Δθ ≈ 0.5 * r_max * sin(2θ)
      dtheta_deg <- 0.5 * r_max * sin(2 * theta0_deg * pi/180) * 180/pi
      dtheta_deg <- max(dtheta_deg, 0.01)  # minimum 0.01°
      angles_deg <- sort(unique(c(theta0_deg - 2*dtheta_deg,
                                  theta0_deg - 1*dtheta_deg,
                                  theta0_deg,
                                  theta0_deg + 1*dtheta_deg,
                                  theta0_deg + 2*dtheta_deg)))
      angles_deg <- angles_deg[angles_deg > eps & angles_deg < 90 - eps]
    }

    # Convert to your relative units if needed (unchanged)
    relative.signal.angle.range <- (angles_deg*pi/180 - pi/4) / (pi/4)

    # --- Verbose logging (kept in your style) ---
    cat(file=stderr(), "\n[Calibration] --- Stable angle grid (log-β) ---\n")
    cat(file=stderr(), "[Calibration] slope_angle (deg) =", round(theta0_deg, 6), "\n")
    cat(file=stderr(), "[Calibration] r_max (β rel. change) = ±", r_max, "\n")
    cat(file=stderr(), "[Calibration] testing absolute signal angles (deg) =",
        paste(round(angles_deg, 6), collapse = ", "), "\n")
    cat(file=stderr(), "[Calibration] mapped β grid =",
        paste(signif(beta_grid, 6), collapse = ", "), "\n")
    cat(file=stderr(), "[Calibration] -----------------------------------\n\n")

    # -------------------------- new -------------------------- #

    # === build normalized signal vector for calibration ===
    base.signal.angle <- atan(slope.regression)  # regression angle in radians
    signal.vector <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
    normalized.signal.vector <- signal.vector / sqrt(sum(signal.vector^2))

    # === margin inverse vector ===
    margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
    margin.inv.slope  <- margin.inv.vector[2] / margin.inv.vector[1]

    # === construct signal prices (needed for calibration) ===
    signal.prices <- cbind(
      prices.bbo.a.b[,"bid.a"] * normalized.signal.vector[1] +
        prices.bbo.a.b[,"ask.b"] * normalized.signal.vector[2],
      prices.bbo.a.b[,"ask.a"] * normalized.signal.vector[1] +
        prices.bbo.a.b[,"bid.b"] * normalized.signal.vector[2]
    )

    theo.price <- mean(signal.prices[1,])



    # relative.margin.range    <- seq(from=10, to=100, by=10)

    # -------------------------- end new -------------------------- #

    ###########  Display progress bar for each pair

    num.optima              <- 0

    ###########  max bid-ask filter -> not used as maker...

    if(max.bid.ask.filter) {
      max.spread.prob <- 0.9995
      window.size     <- 100 # number of ticks

      # smooth spread
      roll.smooth     <- function(x, k) rollapply(x, k, function(x) mean(x), partial=TRUE)

      quantile.a      <- quantile(roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size), probs=max.spread.prob)
      quantile.b      <- quantile(roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size), probs=max.spread.prob)

      idx.max.bid.ask.spread.a <- roll.smooth(prices.bbo.a.b[,3]-prices.bbo.a.b[,2], window.size)>quantile.a
      idx.max.bid.ask.spread.b <- roll.smooth(prices.bbo.a.b[,5]-prices.bbo.a.b[,4], window.size)>quantile.b

      # filter outliers
      prices.bbo.a.b.filter                                   <- prices.bbo.a.b
      prices.bbo.a.b.filter[idx.max.bid.ask.spread.a, c(2,3)] <- NA
      prices.bbo.a.b.filter[idx.max.bid.ask.spread.b, c(4,5)] <- NA

      prices.bbo.a.b.filter <- na.locf(prices.bbo.a.b.filter)

      par(mfrow=c(2,1))
      idx <- 1:nrow(prices.bbo.a.b)
      plot((prices.bbo.a.b[idx,2]+prices.bbo.a.b[idx,3])/2, type='l')
      plot((prices.bbo.a.b[idx,4]+prices.bbo.a.b[idx,5])/2, type='l')
    }

    ###########  plot prices and regression for the in-sample period

    # plot mid-price
    if(export2pdf) {
      par(mfrow=c(3,1))
      plot((prices.bbo.a.b[,2]+prices.bbo.a.b[,3])/2, type='l')
      plot((prices.bbo.a.b[,4]+prices.bbo.a.b[,5])/2, type='l')

      idx.order.a <- order(prices.bbo.a.b[,"bid.a"])
      plot(cbind(prices.bbo.a.b[idx.order.a,"bid.a"], prices.bbo.a.b[idx.order.a,"bid.b"]))
      lines(cbind(prices.bbo.a.b[, "bid.a"], intercept.regression+slope.regression*prices.bbo.a.b[, "bid.a"]), col=2)
    }

    ###########  In case the products are not quoted in USD -> deal with FX rates conversion

    prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))

    if(max.bid.ask.filter) {
      prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
      names(prices.bbo.a.b.filter) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b") # backward compatibility
    }

    ###########  Set init parameters from product information

    # Determine Order.size.range, Max.order.in.delta, delta is the product lot size
    #order.size.range       <- cbind(relative.order.size.range * min.order.size.a, relative.order.size.range * min.order.size.b)

    # for crypto currencies don't consider lot size/delta and keep ordersize a,b relativelly aligned...
    order.size.range       <- relative.order.size.range

    #max.delta.orders       <- c(max.order.size.a * delta.a, max.order.size.b * delta.b)

    # Compute minimum bid-ask spreads for Margin calculation (2 versions)
    # if one tick...minimum.spreads            <- c(tick.size.a, tick.size.b)
    # beware: we disregard null bid-ask spreads
    idx.bid.ask.not.null.a  <- which(prices.bbo.a.b[,"bid.a"]-prices.bbo.a.b[,"ask.a"]!=0)
    idx.bid.ask.not.null.b  <- which(prices.bbo.a.b[,"bid.b"]-prices.bbo.a.b[,"ask.b"]!=0)

    minimum.spreads         <- c(min(prices.bbo.a.b[idx.bid.ask.not.null.a,"ask.a"]-prices.bbo.a.b[idx.bid.ask.not.null.a,"bid.a"]),
                                 min(prices.bbo.a.b[idx.bid.ask.not.null.b,"ask.b"]-prices.bbo.a.b[idx.bid.ask.not.null.b,"bid.b"])) # one tick for liquid products

    # ensure that tick size as calculated is smaller or equal to the minimum spreads seen (i.e. min(dp) versus min(ask-bid))
    # will be used within the MM pnl calculation
    tickSize.a              <- min(tick.size.a*fx.a, minimum.spreads[1])
    tickSize.b              <- min(tick.size.b*fx.b, minimum.spreads[2])

    ###########  Core algorithm

  for (mc_idx in seq_along(MIN_CROSSING_VEC)) {

      MIN_CROSSING <- MIN_CROSSING_VEC[mc_idx]

      cat(file=stderr(),
          paste0("\n[Grid] Using MIN_CROSSING = ", MIN_CROSSING,
                 " (config index ", mc_idx, ")\n"))

      # --- calibrate margin range for this config ---
      relative.margin.range <- calibrate_margin_range(
        prices = prices.bbo.a.b,
        normalized.signal.vector = normalized.signal.vector,
        margin.inv.vector = margin.inv.vector,
        margin.inv.slope = margin.inv.slope,
        tick.size.a = tick.size.a,
        tick.size.b = tick.size.b,
        fx.a = fx.a,
        fx.b = fx.b,
        min_crossings = MIN_CROSSING,
        max_crossings = MIN_CROSSING * 2
      )

        # loop over all grid of parameters -> brut force calculation, keep track of run id
num.loops <- length(relative.signal.angle.range) *
             length(relative.margin.range) *
             length(relative.step.back.range) *
             length(relative.trading.angle.range) *
             length(relative.order.size.range) *
             length(num.crossing.2.limit.range)

    iter.num                <- 1

    ###########  Compute signal angle

    for(i in 1:length(relative.signal.angle.range)) {

      relative.signal.angle <- relative.signal.angle.range[i]

      # base signal angle is constructed such that it is always in quadrant I
      base.signal.angle     <- relative.signal.angle*pi/4+pi/4

      if(max.bid.ask.filter) {
        price.2.use <- prices.bbo.a.b.filter
      } else {
        price.2.use <- prices.bbo.a.b
      }

      # determine whether positively or negatively correlated pair
      if(FALSE) {
        # positive base direction
        signal.vector            <- c(cos(base.signal.angle), base.direction * sin(base.signal.angle))
        signal.angle             <- atan(signal.vector[2]/signal.vector[1]) # don't add pi
        normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
        signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "bid.b"] * normalized.signal.vector[2],
                                          price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "ask.b"] * normalized.signal.vector[2])
      } else {
        # negative base direction
        signal.vector            <- c(cos(base.signal.angle), -base.direction * sin(base.signal.angle))
        signal.angle             <- atan(signal.vector[2]/signal.vector[1]) + pi # add pi
        normalized.signal.vector <- signal.vector/sqrt(sum(signal.vector^2))
        signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "ask.b"] * normalized.signal.vector[2],
                                          price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                            price.2.use[, "bid.b"] * normalized.signal.vector[2])
      }

      ###########  Compute margin

      for(j in 1:length(relative.margin.range)) {

        # TODO: factorize minimum profitability margin
        relative.margin            <- relative.margin.range[j]
        base.margin                <- max(abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
                                          abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b))

        # ensure that margin is a multiple of min spread or ticksize projected to the NSV
        margin                     <- base.margin * relative.margin

        ###########  Stepback calculation

        for(k in 1:length(relative.step.back.range)) {

          stepback <- relative.step.back.range[k] * margin # (ev. Multiplied by the margin or not)

          ###########  Prepare theoretical price and inputs

          if (nrow(signal.prices) == 0) {
            warning("signal.prices is empty, skipping iteration")
            next
          }
          if (ncol(signal.prices) < 2) {
            warning("signal.prices malformed (ncol < 2), skipping iteration")
            next
          }

          theo.price <- (signal.prices[1,1] + signal.prices[1,2]) / 2

          # sanity checks before calling C++ crossing
          if (length(theo.price) == 0 || is.na(theo.price)) {
            warning("theo.price missing, skipping iteration")
            next
          }
          if (is.na(margin) || is.na(stepback)) {
            warning("margin/stepback invalid, skipping iteration")
            next
          }

          ###########  Quoter
          margin.inv.vector <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
          margin.inv.slope  <- margin.inv.vector[2] / margin.inv.vector[1]

          # extra input validation
          if (any(sapply(list(signal.prices[,1], signal.prices[,2],
                              price.2.use[,"bid.a"], price.2.use[,"ask.a"],
                              price.2.use[,"bid.b"], price.2.use[,"ask.b"]),
                         function(x) length(x) == 0))) {
            warning("One or more price vectors empty, skipping iteration")
            next
          }

          dPrice <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                     price.2.use[,"bid.a"],
                                     price.2.use[,"ask.a"],
                                     price.2.use[,"bid.b"],
                                     price.2.use[,"ask.b"],
                                     theo.price, margin, stepback,
                                     margin.inv.vector[1], margin.inv.vector[2],
                                     margin.inv.slope,
                                     normalized.signal.vector[1], normalized.signal.vector[2],
                                     tick.size.a, tick.size.b,
                                     enableSkew = ENABLE_SKEW)

          # Compute IS skew statistics
          skew.activations.is <- sum(dPrice$skewUpperVec > 1 | dPrice$skewLowerVec > 1)
          pct.time.skewed.is  <- skew.activations.is / length(dPrice$skewUpperVec)
          avg.skew.upper.is   <- mean(dPrice$skewUpperVec)
          avg.skew.lower.is   <- mean(dPrice$skewLowerVec)

          # check crossings and how long a quote would have become a trade
          # compute trade price on the maker and taker side
          quote.level        <- data.frame(price.2.use,
                                           dPrice$aSellLevelA,
                                           dPrice$aBuyLevelA,
                                           dPrice$aSellLevelB,
                                           dPrice$aBuyLevelB)

          names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")

          ###########  Compute order size new version
          # if no crossing at all for that rp...go to the next iteration
          num.crossings.is <- sum(dPrice$moveTheoPriceVec != 0)
          if(num.crossings.is == 0) {
            inner.loop.size <- length(relative.trading.angle.range) *
                               length(relative.order.size.range) *
                               length(num.crossing.2.limit.range)
            iter.num <- iter.num + inner.loop.size
            next
          }
          # Early termination: if crossings < MIN_CROSSING, skip all inner loops
          if(num.crossings.is < MIN_CROSSING) {
            inner.loop.size <- length(relative.trading.angle.range) *
                               length(relative.order.size.range) *
                               length(num.crossing.2.limit.range)
            iter.num <- iter.num + inner.loop.size
            next
          }

          # to speed up process keep only non zero crossing
          #idx.occurence  <- dPrice$moveTheoPriceVec!=0

          ###########  Pre-compute OOS crossing (depends only on signal_angle, margin, stepback)
          signal.prices.oos <- cbind(
            prices.bbo.a.b.oos.fx[, "bid.a"] * normalized.signal.vector[1] +
              prices.bbo.a.b.oos.fx[, "ask.b"] * normalized.signal.vector[2],
            prices.bbo.a.b.oos.fx[, "ask.a"] * normalized.signal.vector[1] +
              prices.bbo.a.b.oos.fx[, "bid.b"] * normalized.signal.vector[2])

          theo.price.oos      <- (signal.prices.oos[1,1] + signal.prices.oos[1,2]) / 2
          margin.inv.vector.oos <- c(-normalized.signal.vector[2], normalized.signal.vector[1])
          margin.inv.slope.oos  <- margin.inv.vector.oos[2] / margin.inv.vector.oos[1]

          dPrice.oos <- generateCrossing(signal.prices.oos[,1], signal.prices.oos[,2],
                                         prices.bbo.a.b.oos.fx[, "bid.a"],
                                         prices.bbo.a.b.oos.fx[, "ask.a"],
                                         prices.bbo.a.b.oos.fx[, "bid.b"],
                                         prices.bbo.a.b.oos.fx[, "ask.b"],
                                         theo.price.oos, margin, stepback,
                                         margin.inv.vector.oos[1], margin.inv.vector.oos[2],
                                         margin.inv.slope.oos,
                                         normalized.signal.vector[1], normalized.signal.vector[2],
                                         tick.size.a, tick.size.b,
                                         enableSkew = ENABLE_SKEW)

          # Build OOS quote levels (reuse for all inner loop iterations)
          quote.level.oos <- data.frame(prices.bbo.a.b.oos.fx,
                                        dPrice.oos$aSellLevelA,
                                        dPrice.oos$aBuyLevelA,
                                        dPrice.oos$aSellLevelB,
                                        dPrice.oos$aBuyLevelB)
          names(quote.level.oos) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b",
                                      "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")

          # OOS skew statistics (computed once per outer triple)
          skew.activations.oos <- sum(dPrice.oos$skewUpperVec > 1 | dPrice.oos$skewLowerVec > 1)
          pct.time.skewed.oos  <- skew.activations.oos / length(dPrice.oos$skewUpperVec)
          avg.skew.upper.oos   <- mean(dPrice.oos$skewUpperVec)
          avg.skew.lower.oos   <- mean(dPrice.oos$skewLowerVec)
          skew.intensity.oos   <- pmax(dPrice.oos$skewUpperVec, dPrice.oos$skewLowerVec)

          ###########  Parallel inner loop computation (trading angle x order size x nc2l)

          inner.params <- expand.grid(
            l_idx = seq_along(relative.trading.angle.range),
            m_idx = seq_along(relative.order.size.range),
            n_idx = seq_along(num.crossing.2.limit.range)
          )

          inner.results <- foreach(row = seq_len(nrow(inner.params))) %dopar% {
            l <- inner.params$l_idx[row]
            m <- inner.params$m_idx[row]
            n <- inner.params$n_idx[row]

            # === Trading angle ===
            base.trading.angle        <- relative.trading.angle.range[l]*1/4*pi+1/4*pi
            trading.vector            <- c(cos(base.trading.angle), -sin(base.trading.angle))
            normalized.trading.vector <- trading.vector/sqrt(sum(trading.vector^2))

            # === Order size ===
            order.size                <- abs(order.size.range[m] * normalized.trading.vector)
            nc2l                      <- num.crossing.2.limit.range[n]
            base.order.size.a         <- order.size[1]
            base.order.size.b         <- order.size[2]

            # === IS: Inventory control + PnL (C++) ===
            safe.position <- nc2lInventoryControl(
              quote.level[,"bid.a"], quote.level[,"ask.a"],
              quote.level[,"bid.b"], quote.level[,"ask.b"],
              quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"],
              quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
              nc2l, base.order.size.a, base.order.size.b)

            pnl.result <- computeDailyHedgedPnL(
              quote.level[,"bid.a"], quote.level[,"ask.a"],
              quote.level[,"bid.b"], quote.level[,"ask.b"],
              quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"],
              quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
              safe.position$theOrderVecA, safe.position$theOrderVecB,
              min.order.size.a, min.order.size.b,
              t.fees.maker.a, t.fees.maker.b,
              t.fees.taker.a, t.fees.taker.b,
              as.integer(idx.is.eod),
              is.daily.hedged)

            pnl.wo.mh       <- pnl.result$pnl_wo_mh
            daily.date.serie <- daily.date.is[pnl.result$daily_day_index]

            # Base result (always returned for progress logging)
            result <- list(
              l = l, m = m, n = n,
              pnl.tail.is = if(length(pnl.wo.mh) > 0) tail(pnl.wo.mh, 1) else NA
            )

            # IS filters
            if(length(pnl.wo.mh) < MIN_CROSSING) return(result)
            if(is.na(tail(pnl.wo.mh,1)) || tail(pnl.wo.mh,1) <= 0) return(result)

            # IS statistics
            pnl.all      <- c(0, diff(pnl.wo.mh))
            pnl.daily    <- aggregate(as.numeric(pnl.all), by=list(daily.date.serie), FUN=sum)
            daily.sharpe <- mean(pnl.daily[,2])/sd(pnl.daily[,2])
            if(daily.sharpe < MIN_SHARPE) return(result)

            sharpe.ratio <- daily.sharpe
            pnl.dsc      <- diff(pnl.wo.mh)
            pnl.dsc.nz   <- pnl.dsc[pnl.dsc!=0]
            N            <- length(pnl.dsc.nz)
            mean_N       <- mean(pnl.dsc.nz) * N
            std_neg      <- sd(pnl.dsc.nz[pnl.dsc.nz<0]) * sqrt(N)
            sortino.ratio <- mean_N / std_neg
            return.factor <- mean(pnl.dsc.nz)/maxdrawdown(pnl.dsc)

            # === OOS PnL (C++) ===
            pnl.wo.mh.oos.lst <- run.oos.sim(
              prices.bbo.a.b.oos, dPrice.oos, quote.level.oos,
              normalized.trading.vector, base.order.size.a, base.order.size.b,
              nc2l, min.order.size.a, min.order.size.b,
              t.fees.maker.a, t.fees.maker.b, t.fees.taker.a, t.fees.taker.b,
              type.a, type.b, is.daily.hedged, idx.oos.eod, daily.date)

            pnl.wo.mh.oos <- pnl.wo.mh.oos.lst$pnl.wo.mh

            # OOS daily Sharpe ratio
            sharpe.ratio.oos <- 0
            if(length(pnl.wo.mh.oos) > 1) {
              pnl.all.oos      <- c(0, diff(pnl.wo.mh.oos))
              daily.date.serie.oos <- pnl.wo.mh.oos.lst$daily.date.serie
              if(length(daily.date.serie.oos) == length(pnl.all.oos) && length(unique(daily.date.serie.oos)) > 1) {
                pnl.daily.oos    <- aggregate(as.numeric(pnl.all.oos), by=list(daily.date.serie.oos), FUN=sum)
                if(sd(pnl.daily.oos[,2]) > 0) {
                  sharpe.ratio.oos <- mean(pnl.daily.oos[,2]) / sd(pnl.daily.oos[,2])
                }
              }
            }

            # PnL/Skew surface entries
            titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                          stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                          order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")

            pnl.surface.entry  <- NULL
            skew.surface.entry <- NULL
            if(is.export.pnl.surface) {
              if(length(pnl.wo.mh.oos) == 0) {
                pnl.surface.entry  <- zoo(rep(0, nrow(prices.bbo.a.b.oos)),
                                          order.by=prices.bbo.a.b.oos[,"time_seconds"])
                skew.surface.entry <- zoo(rep(1, nrow(prices.bbo.a.b.oos)),
                                          order.by=prices.bbo.a.b.oos[,"time_seconds"])
              } else {
                idx.seq.oos <- trunc(seq(from = 1, to = nrow(prices.bbo.a.b.oos), length.out = length(pnl.wo.mh.oos)))
                pnl.surface.entry  <- zoo(pnl.wo.mh.oos, order.by=prices.bbo.a.b.oos[idx.seq.oos,"time_seconds"])
                skew.surface.entry <- zoo(skew.intensity.oos[idx.seq.oos], order.by=prices.bbo.a.b.oos[idx.seq.oos,"time_seconds"])
              }
            }

            # OOS statistics
            num.crossing_oos <- sum(diff(pnl.wo.mh.oos) != 0)
            t_seq <- seq_along(pnl.wo.mh.oos)
            fit   <- lm(pnl.wo.mh.oos ~ t_seq)
            slope <- coef(fit)[2]
            r2    <- summary(fit)$r.squared

            # Grid row
            grid.row <- c(titl,
              relative.signal.angle.range[i], relative.margin.range[j], relative.step.back.range[k],
              relative.trading.angle.range[l], relative.order.size.range[m], num.crossing.2.limit.range[n],
              sharpe.ratio, tail(pnl.wo.mh,1), sharpe.ratio.oos, tail(pnl.wo.mh.oos,1),
              num.crossing_oos, r2, mc_idx,
              skew.activations.is, pct.time.skewed.is, avg.skew.upper.is, avg.skew.lower.is,
              skew.activations.oos, pct.time.skewed.oos, avg.skew.upper.oos, avg.skew.lower.oos)

            result$grid.row <- grid.row
            result$pnl.surface.key   <- titl
            result$pnl.surface.entry <- pnl.surface.entry
            result$skew.surface.entry <- skew.surface.entry
            result$pnl.wo.mh.is  <- pnl.wo.mh
            result$pnl.wo.mh.oos <- pnl.wo.mh.oos

            # Optima filtering
            result$is.optima <- FALSE
            if(length(pnl.wo.mh.oos) > 0 && !is.na(tail(pnl.wo.mh.oos,1)) && tail(pnl.wo.mh.oos,1) > 0 &&
               length(pnl.wo.mh.oos) >= MIN_CROSSING) {
              pnl.all.oos   <- c(0, diff(pnl.wo.mh.oos))
              pnl.daily.oos <- aggregate(as.numeric(pnl.all.oos), by=list(pnl.wo.mh.oos.lst$daily.date.serie), FUN=sum)
              daily.sharpe.oos <- mean(pnl.daily.oos[,2])/sd(pnl.daily.oos[,2])

              if(daily.sharpe.oos >= MIN_SHARPE && slope > 0 && r2 >= config_file$filtering$minimum_pnl_curve_r2) {
                result$is.optima       <- TRUE
                result$sharpe.ratio.is <- sharpe.ratio
                result$sharpe.ratio.oos <- daily.sharpe.oos
                result$sortino.ratio   <- sortino.ratio
                result$return.factor   <- return.factor
                # IS inventory for plotting
                if(is.plot.results) {
                  clean.order.a <- sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a
                  clean.order.b <- sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b
                  result$safe.position.a.b <- cbind(cumsum(clean.order.a), cumsum(clean.order.b))
                }
              }
            }

            return(result)
          }

          ###########  Post-processing: aggregate results sequentially
          for (res in inner.results) {
            l <- res$l; m <- res$m; n <- res$n

            cat(file=stderr(), paste("[",Sys.time(),"]",
              paste0(" -- [", iter.num, "/", num.loops, '] completed --- optima # ',
                     num.optima, " PnL: ", res$pnl.tail.is,
                     " --- i:",i," # j:",j," # k:", k," # l:",l," # m:", m, "# n:", n), "\n", sep=""))
            iter.num <- iter.num + 1

            # Append grid row (if OOS was computed)
            if (!is.null(res$grid.row)) {
              grid.util.surface <- rbind(grid.util.surface, res$grid.row)
            }

            # Append PnL/Skew surface entries
            if (!is.null(res$pnl.surface.entry)) {
              pnl.util.surface[[res$pnl.surface.key]]  <- res$pnl.surface.entry
              skew.util.surface[[res$pnl.surface.key]] <- res$skew.surface.entry
            }

            # Optima reporting and plotting
            if (isTRUE(res$is.optima)) {
              pnl.wo.mh     <- res$pnl.wo.mh.is
              pnl.wo.mh.oos <- res$pnl.wo.mh.oos

              if(is.plot.results) {
                safe.position.a.b <- res$safe.position.a.b
                par(mfrow=c(3,1), cex.main=0.8)
                titl.relative <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                       relative.trading.angle.range[l],"#",relative.order.size.range[m],"#", num.crossing.2.limit.range[n], sep="")
                plot(pnl.wo.mh, type='l', ylim=c(min(pnl.wo.mh), max(pnl.wo.mh)),
                     main=paste("In-Sample run (", head(is.dates.vect,1), "-", tail(is.dates.vect,1), ")", sep=""),
                     xlab=titl.relative)
                plot(safe.position.a.b[,1], col=2, type='l',
                     ylim=c(min(safe.position.a.b[,1], safe.position.a.b[,2]),
                            max(safe.position.a.b[,1], safe.position.a.b[,2])),
                     ylab="inventory", xlab="red -> product 1, green -> product 2",
                     main=paste("max inventories (USD) : (", max(abs(safe.position.a.b[,1])), " - ", max(abs(safe.position.a.b[,2])), ")", sep=""))
                lines(safe.position.a.b[,2], col=3)
                plot(pnl.wo.mh.oos, type='l', ylim=c(min(pnl.wo.mh.oos), max(pnl.wo.mh.oos)),
                     main=paste("OOS-Run(", head(oos.dates.vect,1), "-", tail(oos.dates.vect,1), ")", sep=""))
              }

              status.log <- paste(relative.signal.angle.range[i],"#",relative.margin.range[j],"#",relative.step.back.range[k],"#",
                                  relative.trading.angle.range[l],"#",relative.order.size.range[m],"#",num.crossing.2.limit.range[n],
                                  " ### pnl is: ", tail(pnl.wo.mh, 1),
                                  " ### pnl oos: ", tail(pnl.wo.mh.oos, 1),
                                  " ### sharpe.ratio: ", res$sharpe.ratio.is,
                                  " ### sharpe.ratio oos: ", res$sharpe.ratio.oos,
                                  " sortino.ratio: ", res$sortino.ratio, " return.factor: ", res$return.factor, sep="")

              cat(file=stderr(), paste("[",Sys.time(),"] New Optima found : ", status.log, "\n", sep=""))
              num.optima <- num.optima + 1
            }
          } # end inner results processing

          } # end k loop, i.e. relative.step.back.range
        } # end j loop, i.e. relative.margin.range
      } # end i loop, i.e. relative.signal.angle.range
    } # end mc_idx loop (min_crossing_per_day variants)
  } # end of z produc list



    ###########################################################################################################
    ###########  Export utility surface file and close pdf stream writer
    ###########################################################################################################
    if(!is.null(grid.util.surface)) {
      colnames(grid.util.surface) <- c(
        "absolute.parameters",
        "relative.signal.angle",
        "relative.margin",
        "relative.step.back",
        "relative.trading.angle",
        "relative.order.size",
        "num.crossing.2.limit",
        "sharpe.ratio",
        "pnl",
        "sharpe.ratio.oos",
        "pnl.oos",
        "num.crossing.oos",
        "r2",
        "min_crossing_cfg_id",
        "skew_activations_is",
        "pct_time_skewed_is",
        "avg_skew_upper_is",
        "avg_skew_lower_is",
        "skew_activations_oos",
        "pct_time_skewed_oos",
        "avg_skew_upper_oos",
        "avg_skew_lower_oos"
      )

      to_export  <- paste(path.to.results, "/gs_", args[1], "_", args[2], "_", export.name.flag, ".csv", sep="")
      write.table(grid.util.surface, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
    }

    #if(is.export.pnl.surface) {
    if(length(pnl.util.surface)) {

      # concatenate pnl results and export to file
      pnl.util.surface.raw        <- do.call(merge, pnl.util.surface)

      pnl.util.surface.clean      <- na.locf0(pnl.util.surface.raw)

      pnl.util.surface.mtx        <- as.matrix(pnl.util.surface.clean)
      pnl.util.surface.mtx[is.na(pnl.util.surface.mtx)] <- 0
      pnl.util.surface.mtx                              <- cbind(as.numeric(rownames(pnl.util.surface.mtx)), pnl.util.surface.mtx)
      colnames(pnl.util.surface.mtx)[1]                 <- "time_seconds"
      head(pnl.util.surface.mtx)

      to_export                   <- paste(path.to.results, "/pnl_", args[1], "_", args[2], "_", export.name.flag, ".csv", sep="")
      write.table(pnl.util.surface.mtx, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
    }

    # Export skew intensity surface
    if(length(skew.util.surface)) {

      skew.util.surface.raw        <- do.call(merge, skew.util.surface)

      skew.util.surface.clean      <- na.locf0(skew.util.surface.raw)

      skew.util.surface.mtx        <- as.matrix(skew.util.surface.clean)
      skew.util.surface.mtx[is.na(skew.util.surface.mtx)] <- 1
      skew.util.surface.mtx                              <- cbind(as.numeric(rownames(skew.util.surface.mtx)), skew.util.surface.mtx)
      colnames(skew.util.surface.mtx)[1]                 <- "time_seconds"

      to_export                   <- paste(path.to.results, "/skew_", args[1], "_", args[2], "_", export.name.flag, ".csv", sep="")
      write.table(skew.util.surface.mtx, to_export, quote = FALSE, sep = ",", row.names = FALSE, append = FALSE)
    }

    # close pdf writer
    if(export2pdf) dev.off()
   # end z

  ###########################################################################################################


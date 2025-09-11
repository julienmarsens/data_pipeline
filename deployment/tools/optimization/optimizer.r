###########################################################################################################
###########  Cleanup working environment
###########################################################################################################
# ref. http://sebastian.statistics.utoronto.ca/books/algo-and-hf-trading/code/
# https://hackmd.io/@pre-vert/smm#Moving-the-price-interval
# https://hftbacktest.readthedocs.io/en/latest/tutorials/Risk%20Mitigation%20through%20Price%20Protection%20in%20Extreme%20Market%20Conditions.html
# tredax123$ sewall
Sys.setenv (LANGUAGE="en")
remove(list=ls(all=T))
graphics.off()

options(digits=10)
Sys.setenv(TZ='UTC')

# load libs
library(zoo)
library(jsonlite)

#  constant configuration

# Get arguments
args <- commandArgs(trailingOnly = TRUE)

# legacy implementation
MIN_SHARPE                      <- -150.0
MIN_CROSSING                    <- 0 # min crossings for the is period

#  Choose from pairs list & export statement
export2pdf            <- TRUE
is.daily.hedged       <- TRUE
is.plot.results       <- FALSE
is.export.pnl.surface <- FALSE

# if false don't reload data at each grid search (not each loop within a grid)
RELOAD             <- TRUE

is.dates       <- c(args[1], args[2]) #
oos.dates      <- c(args[3], args[4]) #

# update conf parameters at the end of the file accordingly
DISPLAY_CONFIGURATION_CPP <- FALSE
# legacy filtering test
max.bid.ask.filter        <- FALSE
is.stop.loss              <- TRUE

#  Core HFT statarb quoter strategy - Init

perso_local_path <- args[5]
perso_disk_path <- args[6]

repo_base_path  <- file.path(perso_local_path, "data_pipeline", "deployment", "tools", "optimization")
path.to.source  <- file.path(perso_disk_path, "data_torus_sync_r")
path.to.results  <- file.path(perso_disk_path, "results")

path_to_json_results  <- file.path(perso_local_path, "data_pipeline", "deployment", "temporary")

# C++
Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_algo.cpp", sep=""))
Rcpp::sourceCpp(paste(repo_base_path, "/", "quoter_rm.cpp", sep=""))
# R utils
source(paste(repo_base_path, "/", "data_tools.r", sep=""))
source(paste(repo_base_path, "/", "product_specs.r", sep=""))
source(paste(repo_base_path, "/", "stats.r", sep=""))
# oos slave script
source(paste(repo_base_path, "/", "core_quoter_slave.r", sep=""))

# Build list of pairs
exchange.names.lst    <- list()
instrument.type.lst   <- list()
product.names.lst     <- list()

# store stats for max drawdown and inventory
stats.list <- list()

# Define product pairs

# Function to parse product pairs from your input format

product.pairs <- eval(parse(text=args[7]))

# Initialize lists

instrument.type.lst <- list()
exchange.names.lst  <- list()
product.names.lst   <- list()

# Loop through product pairs

for (i in seq_along(product.pairs)) {
  instrument.type.lst[[i]] <- rep("INVERSE", 2)
  exchange.names.lst[[i]]  <- rep("binance-coin-futures", 2)
  product.names.lst[[i]]   <- product.pairs[[i]]
}

# Pair names and stop-loss settings

export.name.flag         <- paste0("pair_", seq_along(product.pairs))
stop.loss.tailing.pnl.vct <- rep(-10000, length(product.pairs))

###########  Setup configuration parameters - build a list of configurations
###########################################################################################################
relative.parameters.list <- list()

# Each row = one pair's parameters:
# signal.angle, margin, step.back, trading.angle, order.size, num.crossing

# Function to parse raw_params from Python-style input
parse_raw_params <- function(input_str) {
  jsonlite::fromJSON(input_str)
}

raw_params <- parse_raw_params(args[8])

# Convert to list of numeric vectors
param.values <- lapply(raw_params, function(x) {
  as.numeric(strsplit(x, "#")[[1]])
})

# Column names (match your data.frame fields)
param.names <- c(
  "relative.signal.angle.range",
  "relative.margin.range",
  "relative.step.back.range",
  "relative.trading.angle.range",
  "relative.order.size.range",
  "num.crossing.2.limit.range"
)

# Initialize list
relative.parameters.list <- list()

# Fill dynamically
for (i in seq_along(param.values)) {
  relative.parameters.list[[i]] <- as.data.frame(
    as.list(param.values[[i]]),
    col.names = param.names
  )
  names(relative.parameters.list[[i]]) <- param.names
}

# Optional: assign pair names
names(relative.parameters.list) <- paste0("pair_", seq_along(param.values))

###########################################################################################################
######### HELPER FUNCTIONS
###########################################################################################################
find.available.dates <- function(dates.range, path.to.source, product.name, exchange.name) {

  # list available file names
  file_names           <- list.files(path = path.to.source)
  file_names_raw       <- strsplit(file_names, "__")

  #naming convention
  product.exchange    <- paste(exchange.name[1], "__", exchange.name[2], "__", product.name[1], "__", product.name[2], sep="")

  file.name.prod.ex   <- apply(do.call( rbind, file_names_raw)[,1:4], 1, paste, collapse="__")

  # first select matching product and exchange file names
  idx.prod.ex         <- product.exchange==file.name.prod.ex
  available.date      <- substr(do.call( rbind, file_names_raw)[,5], 1, 8)[idx.prod.ex]

  # filter date
  idx.date            <- available.date>=dates.range[1] & available.date<=dates.range[2]

  return(available.date[idx.date])
}

###########################################################################################################
###########  Main cross exchange market making algorithm
###########################################################################################################

###########################################################################################################
###########  Main loop through each pair
###########################################################################################################
pnl.aggregated.lst      <- list()
absolute.parameters     <- list()

for(z in 1:length(product.names.lst)) {

  # lookup for relative parameters information
  exchange.names       <- exchange.names.lst[[z]]
  instrument.type      <- instrument.type.lst[[z]]
  product.names        <- product.names.lst[[z]]

  relative.signal.angle.range  <- relative.parameters.list[[z]]$relative.signal.angle.range
  relative.margin.range        <- relative.parameters.list[[z]]$relative.margin.range
  relative.step.back.range     <- relative.parameters.list[[z]]$relative.step.back.range
  relative.trading.angle.range <- relative.parameters.list[[z]]$relative.trading.angle.range
  relative.order.size.range    <- relative.parameters.list[[z]]$relative.order.size.range
  num.crossing.2.limit.range   <- relative.parameters.list[[z]]$num.crossing.2.limit.range

  stop.loss.tailing.pnl        <- stop.loss.tailing.pnl.vct[z]

  # utility surface to be exported
  grid.util.surface    <- NULL
  pnl.util.surface     <- list()

  # stop loss parameters
  pnl.sl         <- NULL
  date.sl        <- NULL
  idx.restart.sl <- NULL

  ###########################################################################################################
  ######### STOP LOSS SPECIFIC
  ###########################################################################################################
  trailing.date      <- oos.dates[1]
  is.first.iteration <- TRUE # in case we run the stop loss print only the stat the first iteration
  while(trailing.date != oos.dates[2]) {

    ###########################################################################################################
    ######### FILTER IS AND OOS DATES
    ###########################################################################################################
    is.dates.vect         <- find.available.dates(is.dates, path.to.source, product.names, exchange.names)

    # in order to rerun oos only from the next day after a stop occured
    oos.dates.sl          <- c(trailing.date, oos.dates[2])
    oos.dates.vect        <- find.available.dates(oos.dates.sl, path.to.source, product.names, exchange.names)

    # verbose run pair names and is-oos dates
    cat(file=stderr(), "\n###########################################################################################################\n")
    cat(file=stderr(), paste("[",Sys.time(),"] Starting new grid search or rerun after stop loss for pair: ", toupper(exchange.names[1]), "-", product.names[1],
                             " versus ", toupper(exchange.names[2]), "-", product.names[2], "\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] IS start date: ", head(is.dates.vect,1), " end date: ", tail(is.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), paste("[",Sys.time(),"] OOS start date: ", head(oos.dates.vect,1), " end date: ", tail(oos.dates.vect,1),"\n", sep=""))
    cat(file=stderr(), "###########################################################################################################\n")

    # check that data source is available, skip otherwise
    if(length(is.dates.vect)==0 || length(oos.dates.vect)==0) next

    ###########################################################################################################
    ###########  Display progress bar for each pair
    ###########################################################################################################
    # loop over all grid of parameters -> brut force calculation, keep track of run id
    num.loops               <- length(relative.signal.angle.range) * length(relative.margin.range) * length(relative.step.back.range) *
      length(relative.trading.angle.range) * length(relative.order.size.range) * length(num.crossing.2.limit.range)
    iter.num                <- 1
    num.optima              <- 0

    ###########################################################################################################
    ###########  naming convention
    ###########################################################################################################
    product.name.std.a  <- paste(toupper(gsub("-", "_", product.names[1])), "_", instrument.type[1], "_", toupper(exchange.names[1]), sep="")
    product.name.std.b  <- paste(toupper(gsub("-", "_", product.names[2])), "_", instrument.type[2], "_", toupper(exchange.names[2]), sep="")

    ###########################################################################################################
    ###########  Export to pdf file
    ###########################################################################################################
    if(export2pdf && is.first.iteration) pdf(file = paste(path.to.results, "/gs_", product.name.std.a, "_vs_", product.name.std.b, "_", export.name.flag[z], ".pdf", sep=""))

    ###########################################################################################################
    ###########  lookup product specs
    ###########################################################################################################
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

    ###########################################################################################################
    ###########  Load data source
    ###########################################################################################################
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
      # JF: TO BE UPDATED 20250801
      daily.date.is     <- unique(substr(as.POSIXct(prices.bbo.a.b[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
      daily.date        <- unique(substr(as.POSIXct(prices.bbo.a.b.oos[,"time_seconds"]/1000, origin="1970-01-01"),1,10))
    } else {
      idx.is.eod   <- NULL
      idx.oos.eod  <- NULL
    }

    ###########################################################################################################
    ###########  Compute best cointegration direction based on linear regression
    ###########################################################################################################
    # linear regression used to validate direction of integration AND data quality test...

    # validation will be made oos period...to prevent (too much) overfitting
    regression.length <- nrow(prices.bbo.a.b)

    # compute regression line
    lm_b_a = lm(bid.b~bid.a, data = head(prices.bbo.a.b, regression.length))

    # check R squared
    summary(lm_b_a)

    intercept.regression <- lm_b_a$coefficients[1]
    slope.regression     <- lm_b_a$coefficients[2]

    signal.angle         <- atan(slope.regression)*180/pi # in deg

    base.direction        <- 1/slope.regression

    ###########################################################################################################
    ###########  max bid-ask filter -> not used as maker...
    ###########################################################################################################
    # legacy, not used currently
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

    ###########################################################################################################
    ###########  plot prices and regression for the in-sample period
    ###########################################################################################################
    # plot mid-price
    if(export2pdf && is.first.iteration) {
      par(mfrow=c(3,1))
      plot((prices.bbo.a.b[,2]+prices.bbo.a.b[,3])/2, type='l')
      plot((prices.bbo.a.b[,4]+prices.bbo.a.b[,5])/2, type='l')

      idx.order.a <- order(prices.bbo.a.b[,"bid.a"])
      plot(cbind(prices.bbo.a.b[idx.order.a,"bid.a"], prices.bbo.a.b[idx.order.a,"bid.b"]))
      lines(cbind(prices.bbo.a.b[, "bid.a"], intercept.regression+slope.regression*prices.bbo.a.b[, "bid.a"]), col=2)

      is.first.iteration <- FALSE
    }

    ###########################################################################################################
    ###########  In case the products are not quoted in USD -> deal with FX rates conversion
    ###########################################################################################################
    prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))

    if(max.bid.ask.filter) {
      prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b.filter[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
      names(prices.bbo.a.b.filter) <- c("time_seconds", "bid.a", "ask.a", "bid.b", "ask.b") # backward compatibility
    }

    ###########################################################################################################
    ###########  Set init parameters from product information
    ###########################################################################################################
    # Determine Order.size.range, Max.order.in.delta, delta is the product lot size
    #order.size.range       <- cbind(relative.order.size.range * min.order.size.a, relative.order.size.range * min.order.size.b)

    # for crypto currencies don't consider lot size/delta and keep ordersize a,b relativelly aligned...
    order.size.range       <- relative.order.size.range

    idx.bid.ask.not.null.a  <- which(prices.bbo.a.b[,"bid.a"]-prices.bbo.a.b[,"ask.a"]!=0)
    idx.bid.ask.not.null.b  <- which(prices.bbo.a.b[,"bid.b"]-prices.bbo.a.b[,"ask.b"]!=0)

    minimum.spreads         <- c(min(prices.bbo.a.b[idx.bid.ask.not.null.a,"ask.a"]-prices.bbo.a.b[idx.bid.ask.not.null.a,"bid.a"]),
                                 min(prices.bbo.a.b[idx.bid.ask.not.null.b,"ask.b"]-prices.bbo.a.b[idx.bid.ask.not.null.b,"bid.b"])) # one tick for liquid products

    # ensure that tick size as calculated is smaller or equal to the minimum spreads seen (i.e. min(dp) versus min(ask-bid))
    # will be used within the MM pnl calculation
    tickSize.a              <- min(tick.size.a*fx.a, minimum.spreads[1])
    tickSize.b              <- min(tick.size.b*fx.b, minimum.spreads[2])

    ###########################################################################################################
    ###########  Core algorithm
    ###########################################################################################################

    ###########################################################################################################
    ###########  Compute signal angle
    ###########################################################################################################
    # relative.signal.angle.range [-1, 1], representing relative signal angles used in the grid search
    # Calculate Base Direction for signal angle and trading angle
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
      #if(cov.mat[1,2]<0) {
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

      ###########################################################################################################
      ###########  Compute margin
      ###########################################################################################################
      for(j in 1:length(relative.margin.range)) {

        # TODO: factorize minimum profitability margin
        relative.margin            <- relative.margin.range[j]
        base.margin                <- max(abs(normalized.signal.vector[1] * minimum.spreads[1]/fx.a),
                                          abs(normalized.signal.vector[2] * minimum.spreads[2]/fx.b))

        # ensure that margin is a multiple of min spread or ticksize projected to the NSV
        margin                     <- base.margin * relative.margin

        ###########################################################################################################
        ###########  Stepback calculation
        ###########################################################################################################
        for(k in 1:length(relative.step.back.range)) {

          stepback = relative.step.back.range[k]*margin # (ev. Multiplied by the margin or not)

          ###########################################################################################################
          ###########  Call crossing algorithm to get triggers (C++ code to speed up things)
          ###########################################################################################################
          theo.price               <- (signal.prices[1,1]+signal.prices[1,2])/2 # theoretical price at t=0

          ###########################################################################################################
          ###########  Quoter
          ###########################################################################################################
          margin.inv.vector        <- c(-normalized.signal.vector[2],normalized.signal.vector[1])
          margin.inv.slope         <- margin.inv.vector[2]/margin.inv.vector[1]

          dPrice        <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                            price.2.use[, "bid.a"],
                                            price.2.use[, "ask.a"],
                                            price.2.use[, "bid.b"],
                                            price.2.use[, "ask.b"],
                                            theo.price, margin, stepback,
                                            margin.inv.vector[1], margin.inv.vector[2],
                                            margin.inv.slope,
                                            normalized.signal.vector[1], normalized.signal.vector[2],
                                            tick.size.a, tick.size.b)

          # check crossings and how long a quote would have become a trade
          # compute trade price on the maker and taker side
          quote.level        <- data.frame(price.2.use,
                                           dPrice$aSellLevelA,
                                           dPrice$aBuyLevelA,
                                           dPrice$aSellLevelB,
                                           dPrice$aBuyLevelB)

          names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")

          ###########################################################################################################
          ###########  Compute order size new version
          ###########################################################################################################
          # if no crossing at all for that rp...go to the next iteration
          if(!any(dPrice$moveTheoPriceVec!=0)) next

          ###########################################################################################################
          ###########  Compute trading angle
          ###########################################################################################################
          for(l in 1:length(relative.trading.angle.range)) {

            # Base.trading.angle is contructed such that it is always in quadrant I
            base.trading.angle        <- relative.trading.angle.range[l]*1/4*pi+1/4*pi
            #base.trading.angle        <- -0.99*1/4*pi+1/4*pi
            trading.vector            <- c(cos(base.trading.angle), -sin(base.trading.angle))
            normalized.trading.vector <- trading.vector/sqrt(sum(trading.vector^2)) # no need...

            ###########################################################################################################
            ###########  Compute order size (legacy & Q)
            ###########################################################################################################
            for(m in 1:length(relative.order.size.range)) {

              # consider min order size see above
              # modif USD: order.size.range should be a percentage invesment of the contract value
              order.size                <- abs(order.size.range[m] * normalized.trading.vector)
              typical.order.size        <- order.size.range[m]

              ###########################################################################################################
              ###########  RM - SAG - i.e. number of crossing...prevent martingal mechanism...
              ###########################################################################################################
              for(n in 1:length(num.crossing.2.limit.range)) {

                nc2l                      <- num.crossing.2.limit.range[n]

                titl <- paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                              stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                              order.size.range[m],"#",num.crossing.2.limit.range[n], sep="")

                absolute.parameters[[export.name.flag[z]]] <- c(
                  absolute.parameters[[export.name.flag[z]]], titl
                )
                print(titl)

                base.order.size.a         <- order.size[1]
                base.order.size.b         <- order.size[2]

                max.inventories           <- abs(num.crossing.2.limit.range[n] * c(base.order.size.a, base.order.size.b))

                safe.position             <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"], quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"], quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                nc2l, base.order.size.a, base.order.size.b)

                # constrains to min order size
                # clean.order.rounded       <- cbind(sign(safe.position$theOrderVecA)*round(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                #                                    sign(safe.position$theOrderVecB)*round(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)

                clean.order.rounded       <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                                   sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)

                safe.position.a.b         <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))

                # ✅ compute inventories for stats
                max.inv.a <- max(abs(safe.position.a.b[,1]))
                max.inv.b <- max(abs(safe.position.a.b[,2]))

                # safe.position.a.b         <- cbind(safe.position$thePositionVecA, safe.position$thePositionVecB)
                # clean.order.rounded       <- rbind(c(0, 0), diff(safe.position.a.b))

                # necessary for pnl & fees calculation -> crossing index
                idx.xing                   <- clean.order.rounded[,1] !=0 | clean.order.rounded[,2] !=0

                ###########################################################################################################
                ###########  PnL calculation
                ###########################################################################################################
                precision.prices        <-  1.e-5
                idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] - quote.level[-1,"bid.a"] <= precision.prices
                idx.2                   <- quote.level[-nrow(quote.level),"aBuyLevelA"] - quote.level[-1,"ask.a"] >= -precision.prices
                idx.3                   <- quote.level[-nrow(quote.level),"aSellLevelB"] - quote.level[-1,"bid.b"] <= precision.prices
                idx.4                   <- quote.level[-nrow(quote.level),"aBuyLevelB"] - quote.level[-1,"ask.b"] >= -precision.prices

                trade.price.raw         <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)),
                                                 rep(0, time=nrow(safe.position.a.b)))

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

                trade.price                      <- data.frame(prices.bbo.a.b[,1], trade.price.raw)

                # compute the transaction fees. -> retrieved dynamically from product_specs.r file
                transaction.fees                  <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                                           rep(0, time=nrow(safe.position.a.b)))

                order.fees                   <- clean.order.rounded *0

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

                transaction.fees.cum              <- cbind(cumsum(transaction.fees[,1]), cumsum(transaction.fees[,2]))

                ###########################################################################################################
                ###########  PnL calculation -> Version 2.0
                ###########  Inverse swap -> i.e. coin margined products
                ###########################################################################################################
                if(!is.daily.hedged) {
                  t.fees.bc.a <- transaction.fees[idx.xing,1]/trade.price[idx.xing,3]
                  t.fees.bc.b <- transaction.fees[idx.xing,2]/trade.price[idx.xing,5]

                  pnl.dsc.a   <- cbind(-clean.order.rounded[idx.xing,1], clean.order.rounded[idx.xing,1]/trade.price[idx.xing,2]-t.fees.bc.a)
                  pnl.dsc.b   <- cbind(-clean.order.rounded[idx.xing,2], clean.order.rounded[idx.xing,2]/trade.price[idx.xing,4]-t.fees.bc.b)

                  pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                  pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))

                  # format to legacy format, WARNING: remove/comment 2 following lines if proceed to validation, see below...
                  pnl.cum.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.price[idx.xing,3]
                  pnl.cum.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.price[idx.xing,5]

                  plot(pnl.cum.a+pnl.cum.b, type='l')

                } else {

                  ###########################################################################################################
                  ###########  PnL calculation -> Version 3.0
                  ###########  Inverse swap -> i.e. coin margined products
                  ###########  hedge accumulation spot inventory daily
                  ###########################################################################################################
                  # loop through days and compute intraday cummulative pnl
                  previous.quote.position.a <- 0
                  previous.quote.position.b <- 0

                  liquidation.mid.price.a <- NULL
                  liquidation.mid.price.b <- NULL

                  trade.serie.a <- NULL
                  trade.serie.b <- NULL

                  fees.serie.a  <- NULL
                  fees.serie.b  <- NULL

                  # keep track of daily pnl for daily pnl sharpe calculation
                  daily.date.serie  <- NULL

                  for(w in 1:length(idx.is.eod)) {

                    # find crossings within that day (if any)
                    if(w==1){
                      idx.day      <- 1:idx.is.eod[1]
                    } else {
                      idx.day      <- (idx.is.eod[w-1]+1):idx.is.eod[w]
                    }

                    # daily crossing
                    idx.xing.day <- idx.xing[idx.day]

                    # compute liquidation price and entry price
                    entry.mid.price.a <- ifelse(is.null(liquidation.mid.price.a), head(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2, liquidation.mid.price.a)
                    entry.mid.price.b <- ifelse(is.null(liquidation.mid.price.b), head(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2, liquidation.mid.price.b)

                    liquidation.mid.price.a <- tail(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
                    liquidation.mid.price.b <- tail(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2

                    pnl.dsc.a   <- NULL
                    pnl.dsc.b   <- NULL

                    # check that at least one crossing occured during that day
                    if(any(idx.xing.day)) {

                      # compute intraday cummulative pnl for that day
                      # beware if there is only one crossing that day...not a matrix but a vector...
                      if(length(idx.xing.day)==1) {
                        t.fees.bc.a <- as.numeric(transaction.fees[idx.day,][1]/trade.price[idx.day,][3])
                        t.fees.bc.b <- as.numeric(transaction.fees[idx.day,][2]/trade.price[idx.day,][5])
                      } else {
                        t.fees.bc.a <- transaction.fees[idx.day,][idx.xing.day,1]/trade.price[idx.day,][idx.xing.day,3]
                        t.fees.bc.b <- transaction.fees[idx.day,][idx.xing.day,2]/trade.price[idx.day,][idx.xing.day,5]
                      }

                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      if(is.null(trade.serie.a)) {
                        trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                      }

                      if(is.null(trade.serie.b)) {
                        trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
                      } else {
                        trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                      }

                      # beware if there is only one crossing that day...not a matrix but a vector...
                      if(length(idx.xing.day)==1) {
                        trade.serie.a <- rbind(trade.serie.a, as.numeric(cbind(-clean.order.rounded[idx.day,][1], trade.price[idx.day,][2])))
                        trade.serie.b <- rbind(trade.serie.b, as.numeric(cbind(-clean.order.rounded[idx.day,][2], trade.price[idx.day,][4])))
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, cbind(-clean.order.rounded[idx.day,][idx.xing.day,1], trade.price[idx.day,][idx.xing.day,2]))
                        trade.serie.b <- rbind(trade.serie.b, cbind(-clean.order.rounded[idx.day,][idx.xing.day,2], trade.price[idx.day,][idx.xing.day,4]))
                      }

                      fees.serie.a  <- c(fees.serie.a, t.fees.bc.a)
                      fees.serie.b  <- c(fees.serie.b, t.fees.bc.b)

                      # keep track of positions in quote w/o liquidation
                      previous.quote.position.a <- sum(pnl.dsc.a[,1])
                      previous.quote.position.b <- sum(pnl.dsc.b[,1])

                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                      trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))
                      # end dbg

                    } else {

                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)

                      if(is.null(trade.serie.a)) {
                        trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
                      } else {
                        trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
                      }

                      if(is.null(trade.serie.b)) {
                        trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
                      } else {
                        trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
                      }

                      trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
                      trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))

                      fees.serie.a  <- c(fees.serie.a, 0)
                      fees.serie.b  <- c(fees.serie.b, 0)
                   }
                    # JF: TO BE UPDATED 20250801
                    daily.date.serie  <- c(daily.date.serie, rep(daily.date.is[w], nrow(trade.serie.a)-length(daily.date.serie)))
                  }

                  pnl.dsc.a   <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2]-fees.serie.a)
                  pnl.dsc.b   <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2]-fees.serie.b)

                  pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
                  pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))

                  pnl.cum.dbg.a <- as.numeric(pnl.cum.a[,1]+pnl.cum.a[,2]*trade.serie.a[,2])
                  pnl.cum.dbg.b <- as.numeric(pnl.cum.b[,1]+pnl.cum.b[,2]*trade.serie.b[,2])

                  # format for legacy naming
                  pnl.cum.a     <- pnl.cum.dbg.a
                  pnl.cum.b     <- pnl.cum.dbg.b
                }

                ###########################################################################################################
                ###########  End PnL calculation -> Version 3.0
                ###########################################################################################################

                ###########################################################################################################
                ###########  Plot and compute statistics
                ###########################################################################################################
                # aggregate results
                pnl.wo.mh <- pnl.cum.a + pnl.cum.b   # ✅ keep this line

                # --------------------------------------------------------------

                trade.serie.a_2 <- trade.serie.a
                trade.serie.b_2 <- trade.serie.b

                iter.num         <- iter.num + 1 # increment grid search counter

                # move to next iteration if pnl <= 0
                if(length(pnl.wo.mh)<MIN_CROSSING && !is.stop.loss) next # prevent error when pnl vector is empty...
                if(!is.stop.loss && (is.na(tail(pnl.wo.mh,1)) || tail(pnl.wo.mh,1)<=0)) next

                ###########################################################################################################
                ###########  Compute return statistics
                ###########################################################################################################
                # eventually compute daily pnl to compute daily sharpe (sharpe distorision due to occurence frequency...)
                pnl.all            <- c(0, diff(pnl.wo.mh))
                pnl.daily          <- aggregate(as.numeric(pnl.all), by=list(daily.date.serie), FUN=sum)
                daily.sharpe       <- mean(pnl.daily[,2])/sd(pnl.daily[,2])

                if(daily.sharpe<MIN_SHARPE && !is.stop.loss) next

                sharpe.ratio     <- daily.sharpe

                # # compute sharpe ratio or sortino ratio for non zero aggregated (2 legs) pnl
                pnl.dsc          <- diff(pnl.wo.mh)
                pnl.dsc.nz       <- pnl.dsc[pnl.dsc!=0]

                ###########################################################################################################
                ###########  Export grid results for further utility surface analysis
                ###########################################################################################################
                if(!is.stop.loss) {
                  grid.util.surface <- rbind(grid.util.surface,
                                             c(paste(normalized.signal.vector[1],"#",normalized.signal.vector[2],"#",margin,"#",
                                                     stepback,"#",normalized.trading.vector[1],"#", normalized.trading.vector[2],"#",
                                                     order.size.range[m],"#",num.crossing.2.limit.range[n], sep=""),
                                              relative.signal.angle.range[i],relative.margin.range[j],relative.step.back.range[k],
                                             relative.trading.angle.range[l],relative.order.size.range[m], num.crossing.2.limit.range[n],
                                             sharpe.ratio, tail(pnl.wo.mh,1)))
                }

                ###########################################################################################################
                # compute sortino ratio
                N                <- length(pnl.dsc.nz)
                mean_N           <- mean(pnl.dsc.nz) * N
                std_neg          <- sd(pnl.dsc.nz[pnl.dsc.nz<0]) * sqrt(N)
                sortino.ratio    <- mean_N / std_neg

                # compute max drawdown
                return.factor      <- mean(pnl.dsc.nz)/maxdrawdown(pnl.dsc)

                ###########################################################################################################
                ###########  Run oos backtest -> pass absolute parameters
                ###########################################################################################################
                pnl.wo.mh.oos.lst <- run.oos.sim(prices.bbo.a.b.oos,
                                        normalized.signal.vector,
                                        margin,
                                        stepback,
                                        tick.size.a,
                                        tick.size.b,
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

                # legacy
                pnl.wo.mh.oos <- pnl.wo.mh.oos.lst$pnl.wo.mh

                # ✅ compute OOS max drawdown here
                equity_curve <- as.numeric(pnl.wo.mh.oos)
                if (length(equity_curve) > 1) {
                  running_max  <- cummax(equity_curve)
                  drawdowns    <- equity_curve - running_max
                  max.dd       <- min(drawdowns)
                } else {
                  max.dd <- NA
                }

                # ✅ save stats now with valid values
                # format pair names cleanly for JSON
                clean_name <- function(x) {
                  toupper(gsub("usd-perp", "USDT", x))   # e.g. "dogeusd-perp" -> "DOGEUSDT"
                }

                pair_key <- paste0("[", clean_name(product.names[1]), ", ", clean_name(product.names[2]), "]")

                stats.list[[pair_key]] <- list(
                  max_drawdown = max.dd,
                  max_inventory = c(max.inv.a, max.inv.b)
                )

                cat(file=stderr(), paste("[",Sys.time(),"]",
                                         paste0(" -- [", iter.num, "/", num.loops, '] completed --- pnl oos for the period # ',
                                                num.optima, " PnL: ", tail(pnl.wo.mh.oos,1),
                                                " --- i:",i," # j:",j," # k:", k," # l:",l," # m:", m, "# n:", n), "\n", sep=""))

                # skip reporting/plot if performance is neg or zero crossings
                if(!is.stop.loss && (length(pnl.wo.mh.oos) == 0 || is.na(tail(pnl.wo.mh.oos,1)) || tail(pnl.wo.mh.oos,1)<=0)) next
                if(length(pnl.wo.mh.oos)<MIN_CROSSING && !is.stop.loss) next # prevent error when pnl vector is empty...

                ###########################################################################################################
                ###########  Compute stop loss logic
                ###########################################################################################################
                # lookup for stop loss
                idx.sl         <- as.numeric(which(stop.loss.tailing.pnl >= drawdown(c(0, diff(pnl.wo.mh.oos)))))[1]

                par(mfrow=c(2,1))
                plot(as.numeric(pnl.wo.mh.oos), ylab="PnL", xlab="crossing",
                     main=paste("PnL without stop loss for the period [", oos.dates.sl[1], "-", oos.dates.sl[2], "]", sep=""),
                     type='l')

                idx.all.sl <- as.numeric(which(stop.loss.tailing.pnl >= drawdown(c(0, diff(pnl.wo.mh.oos)))))

                if(!is.na(idx.sl)) abline(v=idx.sl, col=2, lty=2)

                # compute rolling drawdown
                plot(drawdown(c(0, diff(pnl.wo.mh.oos))), type='l', xlab="crossing", ylab="usd", main="Drawdown")
                if(!is.na(idx.sl)) abline(v=idx.sl, col=2, lty=2)

                # check if stop loss triggered
                if(!is.na(idx.sl)) {

                  #plot(pnl.wo.mh.oos[1:idx.sl], type='l')

                  # stop loss triggered
                  trailing.date  <- gsub("-", "", as.Date(pnl.wo.mh.oos.lst$daily.date.serie[idx.sl+1])+1)
                  date.sl        <- c(date.sl, pnl.wo.mh.oos.lst$daily.date.serie[1:idx.sl])

                  # save previous performance and inventory
                  if(is.null(pnl.sl)) {
                    pnl.sl         <- c(pnl.sl, as.numeric(pnl.wo.mh.oos[1:idx.sl]))
                  } else {
                    pnl.sl         <- c(pnl.sl, tail(as.numeric(pnl.sl),1) + pnl.wo.mh.oos[1:idx.sl])
                  }
                } else {
                  trailing.date  <- oos.dates[2]
                  date.sl        <- c(date.sl, pnl.wo.mh.oos.lst$daily.date.serie)

                  if(is.null(pnl.sl)) {
                    pnl.sl         <- c(pnl.sl, as.numeric(pnl.wo.mh.oos))
                  } else {
                    pnl.sl         <- c(pnl.sl, tail(as.numeric(pnl.sl),1) + as.numeric(pnl.wo.mh.oos))
                  }
                }

                idx.restart.sl <- c(idx.restart.sl, length(pnl.sl))

                ###########################################################################################################
                ###########  End main loop
                ###########################################################################################################

              } # end n loop, i.e. num.crossing.2.limit.range
            } # end m loop, i.e. relative.order.size.range
          } # end l loop, i.e. relative.trading.angle.range
        } # end k loop, i.e. relative.step.back.range
      } # end j loop, i.e. relative.margin.range
    } # end i loop, i.e. relative.signal.angle.range
  } # end trailing date

  # plot pnl with stop loss and restart
  par(mfrow=c(1,1))
  plot(pnl.sl, ylab="PnL", xlab="date", main="PnL WITH stop loss for the whole period", type='l')

  if(!is.null(idx.restart.sl) && length(idx.restart.sl)>1) abline(v=idx.restart.sl[-length(idx.restart.sl)], col=2, lty=2)

  # close pdf writer
  if(export2pdf) dev.off()

  ###########################################################################################################
  ###########  Format and save pnl
  ###########################################################################################################
  library(zoo)

  # Function to make unique by shifting duplicates by 1 second
  make_unique_time <- function(dates) {
    dup_idx <- duplicated(dates) | duplicated(dates, fromLast = TRUE)

    # If there are duplicates, shift each duplicate group by tiny increments
    if (any(dup_idx)) {
      # For each unique date, shift duplicates by 1 second increments
      dates <- ave(as.numeric(dates), dates, FUN = function(x) x + seq_along(x) - 1)
      dates <- as.POSIXct(dates, origin = "1970-01-01", tz = "UTC")
    }
    dates
  }

  # Apply
  dte.sl.unq <- make_unique_time(as.POSIXct(date.sl))

  # Build zoo
  pnl.zoo <- zoo(pnl.sl, dte.sl.unq)

  # save pnl result within a list
  pnl.aggregated.lst[[z]] <- pnl.zoo

} # end z

###########################################################################################################
###########  Format and save pnl
###########################################################################################################

###########################################################################################################
######### Portfolio optimisation - i.e. markovitz
######### ref. https://cran.r-project.org/web/packages/PortfolioAnalytics/vignettes/portfolio_vignette.pdf
######### https://bookdown.org/compfinezbook/introcompfinr/ReturnCalculationsR.html#calculating-returns
###########################################################################################################
library(PortfolioAnalytics)

# merge result and carry forward messing value
pnl.container           <- na.locf(do.call(merge, c(pnl.aggregated.lst, all = TRUE)))
pnl.container           <- pnl.container[!is.na(apply(pnl.container, 1,sum)),]
colnames(pnl.container) <- export.name.flag

# create a return object
# add one day to the container to complete the return time serie versus descrete...
# rebuild a discrete pnl to compute the return based on the init investment.
aum.init         <- 100e3
aum.cum.pnl      <- pnl.container + matrix(aum.init, nrow=nrow(pnl.container), ncol=ncol(pnl.container))

# Compute returns and align timestamps
returns          <- zoo(coredata(aum.cum.pnl[-1, ]) / coredata(aum.cum.pnl[-nrow(aum.cum.pnl), ]) - 1, order.by = index(aum.cum.pnl)[-1])

# Get a character vector of the fund names
fund.names       <- colnames(returns)

# Specify a portfolio object by passing a character vector for the
# assets argument.
pspec            <- portfolio.spec(assets=fund.names)
print.default(pspec)

pspec <- add.constraint(portfolio=pspec,
                        type="box",
                        min=0.1,
                        max=0.5)

# objective: minimise risk
port_rnd = add.objective(portfolio = pspec, type = "risk", name = "StdDev")

# objective: maximise return
port_rnd = add.objective(portfolio = port_rnd, type = "return", name = "mean")

# optimise random portfolios
# compute daily return
# aggregate daily constrains
returns.daily     <- aggregate(returns, by=list(substr(time(returns),1,10)), sum)
returns.daily.zoo <- zoo(coredata(returns.daily), as.Date(as.character(time(returns.daily)) ))

opt_minvar = optimize.portfolio(R = returns, portfolio = port_rnd, optimize_method = "random",
                                trace = TRUE, search_size = 40000)
# plot
chart.RiskReward(opt_minvar, risk.col = "StdDev", return.col = "mean", chart.assets = TRUE)  #also plots the equally weighted portfolio

# Extract the optimal weights
extractWeights(opt_minvar)

opt_weights <- extractWeights(opt_minvar)

# Build result summary
# Build result summary
result_summary <- list(
  optimization_weights = opt_weights,
  max_stats = stats.list,
  absolute_parameters = absolute.parameters
)

# Save to JSON
library(jsonlite)
write_json(result_summary,
           path = file.path(path_to_json_results, "optimization_results.json"),
           pretty = TRUE, auto_unbox = TRUE)

print(opt_minvar)

r_minvar <- Return.portfolio(R = returns.daily.zoo, weights = extractWeights(opt_minvar))
colnames(r_minvar) <- "opt_minvar"

# Plot the  minvar returns
# barplot(r_minvar)

#####################################
# benchmark - equal weights
# Create a vector of equal weights
equal_weights <- rep(1 / ncol(returns.daily.zoo), ncol(returns.daily.zoo))

# Compute the benchmark returns
r_benchmark <- Return.portfolio(R = returns.daily.zoo, weights = equal_weights)
colnames(r_benchmark) <- "benchmark"

# Plot the benchmark returns
# barplot(r_benchmark)

# Combine the returns
ret <- cbind(r_benchmark, r_minvar)

# Compute annualized returns
table.AnnualizedReturns(R = ret)

# Chart the performance summary
charts.PerformanceSummary(R = ret)

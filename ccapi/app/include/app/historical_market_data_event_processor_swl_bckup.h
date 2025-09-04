#ifndef APP_INCLUDE_APP_HISTORICAL_MARKET_DATA_EVENT_PROCESSOR_H_
#define APP_INCLUDE_APP_HISTORICAL_MARKET_DATA_EVENT_PROCESSOR_H_
#include <fstream>
#include <iostream>

#include "app/common.h"
#include "ccapi_cpp/ccapi_event.h"
namespace ccapi {
class HistoricalMarketDataEventProcessor {
 public:
  explicit HistoricalMarketDataEventProcessor(std::function<bool(const Event& event)> eventHandler) : eventHandler(eventHandler) {}
  void processEvent() {
    //this->clockSeconds = 0;
    auto currentDateTp = this->historicalMarketDataStartDateTp;

    std::string lineMarketDepthTrade;
    //bool shouldContinueTrade{true};
    std::vector<std::string> previousSplittedMarketDepth;
    while (currentDateTp < this->historicalMarketDataEndDateTp) {
      const auto& currentDateISO = UtilTime::getISOTimestamp(currentDateTp, "%F");
      APP_LOGGER_INFO("Start processing " + currentDateISO + ".");
      
      // legacy
      // coinbase__btc-usd__2023-04-01__market-depth
      //std::string fileNameWithDirBase = this->historicalMarketDataDirectory + "/" + this->historicalMarketDataFilePrefix + this->exchange + "__" +
      //                                  this->baseAsset + "-" + this->quoteAsset + "__" + currentDateISO + "__";

      // swl file name
      // coinbase__coinbase__btc-usd__btc-usd__2023-04-01
      std::string fileNameWithDirBase = this->historicalMarketDataDirectory + "/" + this->historicalMarketDataFilePrefix + 
                                        this->exchange_a + "__" + this->exchange_b + "__" + 
                                        this->instrument_a + "__" + 
                                        this->instrument_b + "__" +
                                        currentDateISO;
      
      //std::string fileNameWithDirBase = this->historicalMarketDataDirectory + "/" + "coinbase__coinbase__btc-usd__btc-usd__" + currentDateISO;

      std::ifstream fMarketDepthTrade;

      fMarketDepthTrade.open(fileNameWithDirBase + this->historicalMarketDataFileSuffix + ".csv");
      APP_LOGGER_INFO("Opening file " + fileNameWithDirBase + this->historicalMarketDataFileSuffix + ".csv.");

      if (fMarketDepthTrade) {
        APP_LOGGER_INFO("Opened file " + fileNameWithDirBase + this->historicalMarketDataFileSuffix + ".csv.");
        fMarketDepthTrade.ignore(INT_MAX, '\n');

        // loop through daily data source for the given pair/day
        while (std::getline(fMarketDepthTrade, lineMarketDepthTrade) && !lineMarketDepthTrade.empty()) {
          APP_LOGGER_INFO("File market-depth & trade next line is " + lineMarketDepthTrade + ".");
          auto splittedMarketDepthTrade = UtilString::split(lineMarketDepthTrade, ',');

          // parse file lines and trigger appropriate event sequentially
          double currentTimeMarketDepthTrade = std::stoi(splittedMarketDepthTrade.at(0));

          // start-end time
          if (currentTimeMarketDepthTrade < std::chrono::duration_cast<std::chrono::seconds>(this->startTimeTp.time_since_epoch()).count()) {
            continue;
          }
          if (currentTimeMarketDepthTrade >=
              std::chrono::duration_cast<std::chrono::seconds>(this->startTimeTp.time_since_epoch()).count() + this->totalDurationSeconds && this->totalDurationSeconds!=0) {           
            return;
          }

          // We have to deal with four non exclusive cases; i.e. market depth, trade for prodA and prodB
          // mkt depth prodA
          if(splittedMarketDepthTrade.at(1) != "NA") {

            // rebuild corresponding subvector
            std::vector<std::string> mktdeptvct = {splittedMarketDepthTrade.at(0),
                                              splittedMarketDepthTrade.at(1),
                                              splittedMarketDepthTrade.at(2)};
            // trigger event
            this->processMarketDataEventMarketDepth(mktdeptvct, "prodA");
          }
          // mkt depth prodB
          if(splittedMarketDepthTrade.at(6) != "NA") {        
            // rebuild corresponding subvector
            std::vector<std::string> mktdeptvct = {splittedMarketDepthTrade.at(0),
                                              splittedMarketDepthTrade.at(6),
                                              splittedMarketDepthTrade.at(7)};
            // trigger event
            this->processMarketDataEventMarketDepth(mktdeptvct, "prodB");
          }
          // trade prodA
          if(splittedMarketDepthTrade.at(3) != "NA") {       
            // rebuild corresponding subvector
            std::vector<std::string> tradevct = {splittedMarketDepthTrade.at(0),
                                              splittedMarketDepthTrade.at(3),
                                              splittedMarketDepthTrade.at(4),
                                              splittedMarketDepthTrade.at(5)};
            // trigger event
            //this->processMarketDataEventTrade(tradevct, "prodA");
          }
                    // trade prodA
          if(splittedMarketDepthTrade.at(8) != "NA") {        
            // rebuild corresponding subvector
            std::vector<std::string> tradevct = {splittedMarketDepthTrade.at(0),
                                              splittedMarketDepthTrade.at(8),
                                              splittedMarketDepthTrade.at(9),
                                              splittedMarketDepthTrade.at(10)};
            // trigger event
            //this->processMarketDataEventTrade(tradevct, "prodB");
          }
        } // end loop through daily rows
      } else {
        APP_LOGGER_INFO("Warning: unable to open file for date " + UtilTime::getISOTimestamp(currentDateTp));
      } // end open daily file
    APP_LOGGER_INFO("End processing " + currentDateISO + ".");
    currentDateTp += std::chrono::hours(24);
  } // end loop through daily file
}

  TimePoint historicalMarketDataStartDateTp{std::chrono::seconds{0}}, historicalMarketDataEndDateTp{std::chrono::seconds{0}},
      startTimeTp{std::chrono::seconds{0}};
  std::string exchange, baseAsset, quoteAsset, historicalMarketDataDirectory, historicalMarketDataFilePrefix, historicalMarketDataFileSuffix;
  int clockStepSeconds{}, clockSeconds{}, totalDurationSeconds{};

  std::string instrument_a{}, instrument_b{}, exchange_a{}, exchange_b{}, baseAsset_a{}, baseAsset_b{}, quoteAsset_a{}, quoteAsset_b{};

 private:
  void processMarketDataEventTrade(const std::vector<std::string>& splittedLine, std::string prodId) {
    Event event;
    event.setType(Event::Type::SUBSCRIPTION_DATA);
    Message message;
    message.setType(exchange.rfind("binance", 0) == 0 ? Message::Type::MARKET_DATA_EVENTS_AGG_TRADE : Message::Type::MARKET_DATA_EVENTS_TRADE);
    message.setRecapType(Message::RecapType::NONE);
    TimePoint messageTime = UtilTime::makeTimePoint(UtilTime::divide(splittedLine.at(0)));
    message.setTime(messageTime);
    message.setTimeReceived(messageTime);

    message.setCorrelationIdList({std::string(PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID)+"#"+prodId});
    
    // legacy
    //message.setCorrelationIdList({PUBLIC_SUBSCRIPTION_DATA_TRADE_CORRELATION_ID});
    std::vector<Element> elementList;
    Element element;
    element.insert(CCAPI_LAST_PRICE, splittedLine.at(1));
    element.insert(CCAPI_LAST_SIZE, splittedLine.at(2));
    element.insert(CCAPI_IS_BUYER_MAKER, splittedLine.at(3));
    elementList.emplace_back(std::move(element));
    message.setElementList(elementList);
    event.addMessage(message);
    APP_LOGGER_DEBUG("Generated a backtest event: " + event.toStringPretty());
    
    // comment next line to remove trades
    //this->eventHandler(event);
  }
  void processMarketDataEventMarketDepth(const std::vector<std::string>& splittedLine, std::string prodId) {
    Event event;
    event.setType(Event::Type::SUBSCRIPTION_DATA);
    Message message;
    message.setType(Message::Type::MARKET_DATA_EVENTS_MARKET_DEPTH);
    message.setRecapType(Message::RecapType::NONE);
    TimePoint messageTime = UtilTime::makeTimePoint(std::make_pair(std::stol(splittedLine.at(0)), 0));
    message.setTime(messageTime);
    message.setTimeReceived(messageTime);
    
    message.setCorrelationIdList({std::string(PUBLIC_SUBSCRIPTION_DATA_MARKET_DEPTH_CORRELATION_ID)+"#"+prodId});

    // legacy
    //message.setCorrelationIdList({PUBLIC_SUBSCRIPTION_DATA_MARKET_DEPTH_CORRELATION_ID});
    std::vector<Element> elementList;
    if (!splittedLine.at(1).empty()) {
      auto levels = UtilString::split(splittedLine.at(1), '|');
      for (const auto& level : levels) {
        auto found = level.find('_');
        Element element;
        element.insert(CCAPI_BEST_BID_N_PRICE, level.substr(0, found));
        element.insert(CCAPI_BEST_BID_N_SIZE, level.substr(found + 1));
        elementList.emplace_back(std::move(element));
      }
    }
    if (!splittedLine.at(2).empty()) {
      auto levels = UtilString::split(splittedLine.at(2), '|');
      for (const auto& level : levels) {
        auto found = level.find('_');
        Element element;
        element.insert(CCAPI_BEST_ASK_N_PRICE, level.substr(0, found));
        element.insert(CCAPI_BEST_ASK_N_SIZE, level.substr(found + 1));
        elementList.emplace_back(std::move(element));
      }
    }
    message.setElementList(elementList);
    event.addMessage(message);
    APP_LOGGER_INFO("Generated a backtest event: " + event.toStringPretty());
    this->eventHandler(event);
  }

  std::function<bool(const Event& event)> eventHandler;
};
} /* namespace ccapi */
#endif  // APP_INCLUDE_APP_HISTORICAL_MARKET_DATA_EVENT_PROCESSOR_H_

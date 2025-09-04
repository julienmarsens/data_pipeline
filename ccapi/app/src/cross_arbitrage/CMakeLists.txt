if(CROSS_ARBITRAGE_CMAKE_PROJECT_NAME)
  set(NAME ${CROSS_ARBITRAGE_CMAKE_PROJECT_NAME})
else()
  set(NAME cross_arbitrage)
endif()
# set(NAME cross_arbitrage)
project(${NAME})

#add_compile_definitions(CCAPI_ENABLE_SERVICE_MARKET_DATA)
#add_compile_definitions(CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT)

add_executable(${NAME} main.cpp)

add_compile_definitions(CCAPI_LEGACY_USE_WEBSOCKETPP)

if(NOT CCAPI_LEGACY_USE_WEBSOCKETPP)
  add_dependencies(${NAME} boost rapidjson)
endif()

# If backtesting, you may want to comment out the following line to improve run-time speed.
add_compile_definitions(CCAPI_APP_ENABLE_LOG_INFO)

# If uncommented, the program will print out more information.
add_compile_definitions(CCAPI_ENABLE_LOG_TRACE)
add_compile_definitions(CCAPI_ENABLE_LOG_DEBUG)

# leave commented
#set(CMAKE_BUILD_TYPE Debug)

# comment if LIVE & PAPER/uncomment if Backtest
add_compile_definitions(CCAPI_APP_IS_BACKTEST)

#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BITMEX)
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BINANCE)
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_KUCOIN)

#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_COINBASE)
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_GEMINI)

add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BINANCE_COIN_FUTURES)
add_compile_definitions(CCAPI_ENABLE_EXCHANGE_DERIBIT)

add_compile_definitions(CCAPI_ENABLE_SERVICE_MARKET_DATA)
add_compile_definitions(CCAPI_ENABLE_SERVICE_EXECUTION_MANAGEMENT)




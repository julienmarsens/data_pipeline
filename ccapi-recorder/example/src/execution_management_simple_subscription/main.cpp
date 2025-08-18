#include "ccapi_cpp/ccapi_session.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <filesystem>
#include <sys/stat.h>
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/error/en.h"
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <zlib.h>
#include <iostream>
#include <memory>

namespace ccapi {

Logger* Logger::logger = nullptr;

// Enhanced configuration structure
struct Config {
    std::string pathToStorage;
    std::vector<std::pair<std::string, std::string>> exchangeProducts;
    int flushIntervalSeconds;
    int bufferFlushSize;
    bool enableDebugOutput;
    
    /*If config not found, default config*/
    Config() : 
        pathToStorage("./data/orderbook"),
        flushIntervalSeconds(30),
        bufferFlushSize(1000),
        enableDebugOutput(true) {}
};

/*
Improved file writer for CSV data with buffering
*/
class CsvWriter {
public:
    CsvWriter() : bufferSize(0), maxBufferSize(1024) {}
    
    void open(const std::string& filename, std::ios_base::openmode mode) {
        file.open(filename, mode);
        buffer.reserve(maxBufferSize * 100); // Pre-allocate buffer
    }
    
    void writeRow(const std::vector<std::string>& data) {
        if (!file.is_open()) return;
        
        // Write to buffer instead of directly to file
        size_t estimatedSize = 0;
        for (const auto& field : data) {
            estimatedSize += field.size() + 1; // +1 for comma or newline
        }
        
        if (buffer.capacity() < buffer.size() + estimatedSize) {
            buffer.reserve(buffer.size() + estimatedSize + 1024); // Add some extra space
        }
        
        bool first = true;
        for (const auto& field : data) {
            if (!first) buffer.append(",");
            buffer.append(field);
            first = false;
        }
        buffer.append("\n");
        
        bufferSize++;
    }
    
    void flush() {
        if (file.is_open() && !buffer.empty()) {
            file << buffer;
            file.flush();
            buffer.clear();
            bufferSize = 0;
        }
    }
    
    void close() {
        if (file.is_open()) {
            flush();
            file.close();
        }
    }
    
    size_t getBufferSize() const {
        return bufferSize;
    }
    
    ~CsvWriter() {
        close();
    }
    
private:
    std::ofstream file;
    std::string buffer;
    size_t bufferSize;
    size_t maxBufferSize;
};

/*
-Simple thread-safe queue for market data
-multiple threads can safely push and pop elements without
    causing race conditions or data corruption

How does it work:
-standard std::queue and protects access with a mutex
-push() method adds items to the queue with mutex 
-pop() method waits for items and removes them
-stop mechanism to signal when operations should cease
-condition variable (cv) allows threads to wait until data is available
*/
template <typename T>
class ThreadSafeQueue {
public:
    void push(T&& item) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(std::move(item));
        cv.notify_one();
    }
    
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, std::chrono::milliseconds(100), [this] { return !queue.empty() || stopFlag; });
        
        if (queue.empty()) return false;
        
        item = std::move(queue.front());
        queue.pop();
        return true;
    }
    
    void stop() {
        stopFlag = true;
        cv.notify_all();
    }
    
    size_t size() {
        std::lock_guard<std::mutex> lock(mutex);
        return queue.size();
    }
    
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> stopFlag{false};
};

// Structure for market data record
struct MarketDataRecord {
    std::string exchangeName;
    std::string productName;
    std::string timestamp;
    std::string bestBidPrice;
    std::string bestAskPrice;
};

// Structure for exchange data tracking
struct ExchangeData {
    std::string exchange;
    std::string product;
    std::unique_ptr<CsvWriter> writer;
    std::chrono::time_point<std::chrono::system_clock> lastFlushTime;
    int recordsSinceLastFlush;
    
    ExchangeData() : recordsSinceLastFlush(0) {
        lastFlushTime = std::chrono::system_clock::now();
    }
};

// Global configuration
Config config;
/*
unordered map > vector because of search time
*/
std::unordered_map<std::string, ExchangeData> exchangeDataMap;
ThreadSafeQueue<MarketDataRecord> dataQueue;
std::atomic<bool> running{true};
std::string previousMessageTimeISODate;
std::mutex fileCreationMutex;

/* Loading the CSV file */
bool loadConfig(const std::string& configFile) {
    std::ifstream file(configFile);
    if (!file.is_open()) {
        std::cerr << "Could not open config file: " << configFile << std::endl;
        return false;
    }
    
    // Clear existing configuration
    config.exchangeProducts.clear();
    
    std::string line;
    bool configProcessed = false;
    
    // Process all lines in the file
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // First non-comment line contains general configuration
        if (!configProcessed) {
            std::istringstream iss(line);
            std::string token;
            
            // First value is storage path
            if (std::getline(iss, token, ',')) {
                config.pathToStorage = token;
            }
            
            // Second value is flush interval (optional)
            if (std::getline(iss, token, ',')) {
                try {
                    config.flushIntervalSeconds = std::stoi(token);
                } catch (...) {
                    config.flushIntervalSeconds = 30; // Default if parsing fails
                }
            }
            
            // Third value is buffer size (optional)
            if (std::getline(iss, token, ',')) {
                try {
                    config.bufferFlushSize = std::stoi(token);
                } catch (...) {
                    config.bufferFlushSize = 1000; // Default if parsing fails
                }
            }
            
            // Fourth value is debug flag (optional)
            if (std::getline(iss, token, ',')) {
                config.enableDebugOutput = (token == "1" || token == "true" || token == "yes");
            }
            
            configProcessed = true;  // Mark that we've processed the config line
            continue;  // Skip to the next line
        }
        
        // Process exchange-product pairs
        std::istringstream iss(line);
        std::string exchange, product;
        
        if (std::getline(iss, exchange, ',') && std::getline(iss, product, ',')) {
            // Trim whitespace
            exchange.erase(0, exchange.find_first_not_of(" \t"));
            exchange.erase(exchange.find_last_not_of(" \t") + 1);
            product.erase(0, product.find_first_not_of(" \t"));
            product.erase(product.find_last_not_of(" \t") + 1);
            
            if (!exchange.empty() && !product.empty()) {
                config.exchangeProducts.push_back(std::make_pair(exchange, product));
            }
        }
    }
    
    return !config.exchangeProducts.empty();
}

// Function to compress a file using zlib
bool compressFile(const std::string& sourceFilePath) {
    // Open input file
    std::ifstream inFile(sourceFilePath, std::ios::binary);
    if (!inFile) {
        std::cerr << "Failed to open input file: " << sourceFilePath << std::endl;
        return false;
    }
    
    // Output file path - maintain the same name but add .gz extension
    std::string outFilePath = sourceFilePath + ".gz";
    
    // Open output file
    gzFile outFile = gzopen(outFilePath.c_str(), "wb");
    if (!outFile) {
        std::cerr << "Failed to open output file: " << outFilePath << std::endl;
        inFile.close();
        return false;
    }
    
    // Read input file and compress to output file
    char buffer[4096];
    while (inFile) {
        inFile.read(buffer, sizeof(buffer));
        std::streamsize bytesRead = inFile.gcount();
        
        if (bytesRead > 0) {
            int bytesWritten = gzwrite(outFile, buffer, bytesRead);
            if (bytesWritten != bytesRead) {
                std::cerr << "Failed to write to output file" << std::endl;
                inFile.close();
                gzclose(outFile);
                return false;
            }
        }
    }
    
    // Close files
    inFile.close();
    gzclose(outFile);
    
    // Remove original file if compression succeeded
    std::remove(sourceFilePath.c_str());
    
    std::cout << "Compressed " << sourceFilePath << " to " << outFilePath << std::endl;
    return true;
}

// Function to compress all CSV files in a directory while maintaining the new naming format
void compressAllCsvFiles(const std::string& dirPath) {
    try {
        for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                std::string filePath = entry.path().string();
                // The filename should already be in format: [exchange]_[product]_[linear/inverse].csv
                compressFile(filePath);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error compressing files in directory " << dirPath << ": " << e.what() << std::endl;
    }
}

/*
-Continuously takes market data records from the ThreadSafeQueue
-write to the csv files
-track record since last flush
-flush the writter if bufferflushsize has cumulated or if time_interval has passed

Why flush:
-not holding data in memory
-balance performance
*/
void writerThreadFunction() {
    MarketDataRecord record;
    
    while (running) {
        if (dataQueue.pop(record)) {
            // Process the record
            std::string key = record.exchangeName + "#" + record.productName;
            auto it = exchangeDataMap.find(key);
            
            if (it != exchangeDataMap.end()) {
                auto& data = it->second;
                
                // Write data to the appropriate file
                data.writer->writeRow({
                    record.timestamp,
                    record.bestBidPrice,
                    record.bestAskPrice
                });
                
                // Check if we should flush
                data.recordsSinceLastFlush++;
                auto now = std::chrono::system_clock::now();
                bool shouldFlush = data.recordsSinceLastFlush >= config.bufferFlushSize || 
                                  std::chrono::duration_cast<std::chrono::seconds>(
                                      now - data.lastFlushTime).count() >= config.flushIntervalSeconds;
                
                if (shouldFlush) {
                    data.writer->flush();
                    data.recordsSinceLastFlush = 0;
                    data.lastFlushTime = now;
                }
            }
        }
    }
    
    // Final flush of all writers before exit
    for (auto& pair : exchangeDataMap) {
        pair.second.writer->flush();
    }
}

class MyEventHandler : public EventHandler {
public:
    TimePoint tpnow() {
        auto now = std::chrono::system_clock::now();
        return TimePoint(now);
    }
    
    std::string replaceChar(std::string& str, char ch1, char ch2) {
        std::replace(str.begin(), str.end(), ch1, ch2);
        return str;
    }
    
    int getUnixTimestamp(const TimePoint& tp) {
        auto then = tp.time_since_epoch();
        auto s = std::chrono::duration_cast<std::chrono::seconds>(then);
        return s.count();
    }
    
    bool processEvent(const Event& event, Session* session) override {
        // Get current timestamp
        TimePoint now = tpnow();
        now += std::chrono::hours(1);
        const std::string& messageTimeISO = UtilTime::getISOTimestamp(now);
        const std::string& messageTimeISODate = messageTimeISO.substr(0, 10);

        if (event.getType() == Event::Type::SUBSCRIPTION_STATUS) {
            for (const auto& message : event.getMessageList()) 
            {
                if (message.getType() == Message::Type::SUBSCRIPTION_FAILURE) 
                {
                    for (auto& pair : exchangeDataMap) {
                        auto& data = pair.second;
                        
                        std::string exchange = data.exchange;
                        CCAPI_LOGGER_DEBUG("#########################################################");  
                        std::cout << "SUBSCRIPTION_FAILURE:: " + exchange + "-" + messageTimeISO << std::endl;
                        CCAPI_LOGGER_DEBUG("#########################################################");
                    }
                }
            }
        } else if (event.getType() == Event::Type::SUBSCRIPTION_DATA) {
            // std::cout << "Current time: " << messageTimeISO << std::endl;
            
            // Handle new date - create new files if needed
            if (previousMessageTimeISODate.empty() || messageTimeISODate != previousMessageTimeISODate) {
                std::lock_guard<std::mutex> lock(fileCreationMutex);
                
                // Check again after acquiring the lock
                if (previousMessageTimeISODate.empty() || messageTimeISODate != previousMessageTimeISODate) {
                    if (!previousMessageTimeISODate.empty()) {
                        std::string previousDirName = config.pathToStorage + "/" + previousMessageTimeISODate;
                        std::cout << "Compressing files from previous day: " << previousMessageTimeISODate << std::endl;
                        
                        // Launch compression in a separate thread to avoid blocking
                        std::thread compressionThread([previousDirName]() {
                            compressAllCsvFiles(previousDirName);
                            std::cout << "Compression of " << previousDirName << " completed" << std::endl;
                        });
                        compressionThread.detach();  // Allow it to run independently
                    }
                    std::string rootDirName = config.pathToStorage + "/" + messageTimeISODate;
                    std::filesystem::create_directories(rootDirName);
                    
                    // Create new files for each exchange/product pair
                    for (auto& pair : exchangeDataMap) {
                        auto& data = pair.second;
                        
                        std::string product_name = data.product;
                        std::transform(product_name.begin(), product_name.end(), product_name.begin(), ::tolower);
                        std::replace(product_name.begin(), product_name.end(), '_', '-');

                        std::string fileName = rootDirName + "/" + data.exchange + "_" + product_name + ".csv";
                        
                        // Create a new writer
                        data.writer = std::make_unique<CsvWriter>();
                        
                        // Create header if file doesn't exist
                        struct stat buffer;
                        if (stat(fileName.c_str(), &buffer) != 0) {
                            data.writer->open(fileName, std::ios_base::app);
                            data.writer->writeRow({
                                "timestamp",
                                "best_bid_price",
                                "best_ask_price"
                            });
                            data.writer->flush();
                        } else {
                            data.writer->open(fileName, std::ios_base::app);
                        }
                        
                        data.recordsSinceLastFlush = 0;
                        data.lastFlushTime = std::chrono::system_clock::now();
                    }
                    
                    previousMessageTimeISODate = messageTimeISODate;
                    std::cout << "Created files for date: " << messageTimeISODate << std::endl;
                }
            }
            
            // Process market data for each message
            for (const auto& message : event.getMessageList()) {
                if (message.getType() == Message::Type::MARKET_DATA_EVENTS_MARKET_DEPTH) {
                    // Extract exchange and product from correlation ID
                    const auto& correlationIdList = message.getCorrelationIdList();
                    if (correlationIdList.empty()) continue;
                    
                    auto correlationIdToken = UtilString::split(correlationIdList.at(0), '#');
                    
                    // Skip if not in our expected format
                    if (correlationIdToken.size() < 3) continue;
                    
                    std::string exchangeName = correlationIdToken.at(1);
                    std::string productName = correlationIdToken.at(2);
                    
                    // Get timestamp
                    std::string timestamp = std::to_string(getUnixTimestamp(now));
                    
                    // Extract top of book data - only price, not size
                    std::string bestBidPrice;
                    std::string bestAskPrice;
                    
                    for (const auto& element : message.getElementList()) {
                        const std::map<std::string, std::string>& elementNameValueMap = element.getNameValueMap();
                        
                        // Check for bid price
                        auto bidPriceIt = elementNameValueMap.find(CCAPI_BEST_BID_N_PRICE);
                        if (bidPriceIt != elementNameValueMap.end() && !bidPriceIt->second.empty()) {
                            bestBidPrice = bidPriceIt->second;
                        }
                        
                        // Check for ask price
                        auto askPriceIt = elementNameValueMap.find(CCAPI_BEST_ASK_N_PRICE);
                        if (askPriceIt != elementNameValueMap.end() && !askPriceIt->second.empty()) {
                            bestAskPrice = askPriceIt->second;
                        }
                    }
                    
                    // Only queue if we have data
                    if (!bestBidPrice.empty() || !bestAskPrice.empty()) {
                        // Queue the data for writing in a separate thread
                        dataQueue.push(MarketDataRecord{
                            exchangeName,
                            productName,
                            timestamp,
                            bestBidPrice,
                            bestAskPrice
                        });
                    }
                    
                    // Print to console for debugging only if enabled
                    if (config.enableDebugOutput) {
                        std::cout << "Top of book for " << exchangeName << " " << productName << " at " 
                                  << UtilTime::getISOTimestamp(message.getTime()) << ":" << std::endl;
                        std::cout << "  Bid: " << bestBidPrice << " | Ask: " << bestAskPrice << std::endl;
                    }
                }
            }
        } 
        return true;
    }
};
} /* namespace ccapi */

int main(int argc, char** argv) {
    using namespace ccapi;
    
    // Parse command line arguments for config file
    std::string configFile = "config.csv";
    if (argc > 1) {
        configFile = argv[1];
    }
    
    // Load configuration
    if (!loadConfig(configFile)) {
        std::cout << "Couldn't load the config file";
        return (1);
    }
    
    // make sure data directory exists
    std::filesystem::create_directories(config.pathToStorage);
    
    // Initialize exchange data map
    for (const auto& pair : config.exchangeProducts) {
        std::string key = pair.first + "#" + pair.second;
        ExchangeData data;
        data.exchange = pair.first;
        data.product = pair.second;
        exchangeDataMap[key] = std::move(data);
        std::cout << "Adding subscription for exchange: " << pair.first
                  << " and product: " << pair.second << std::endl;
    }
    
    // start writer thread
    std::thread writerThread(writerThreadFunction);
    
    SessionOptions sessionOptions;
    SessionConfigs sessionConfigs;
    MyEventHandler eventHandler;
    
    // function to set up session and subscriptions
    auto setupSession = [&]() -> Session* {
        // create subscriptions for all exchanges/products
        std::vector<Subscription> subscriptionList;
        for (const auto& pair : exchangeDataMap) {
            // Configure for top of book only with 1-second sampling
            std::string options = std::string(CCAPI_MARKET_DEPTH_MAX) + "=1";
            options += "&" + std::string(CCAPI_CONFLATE_INTERVAL_MILLISECONDS) + "=1000";
            subscriptionList.emplace_back(
                pair.second.exchange,
                pair.second.product,
                "MARKET_DEPTH",
                options,
                "MARKET_DEPTH#" + pair.second.exchange + "#" + pair.second.product
            );
        }
        
        // Create new session
        Session* session = new Session(sessionOptions, sessionConfigs, &eventHandler);
        
        std::cout << "--- Starting orderbook recorder ---" << std::endl;
        session->subscribe(subscriptionList);
        
        return session;
    };
    
    // Handle shutdown signals
    signal(SIGINT, [](int sig) {
        std::cout << "Shutting down..." << std::endl;
        running = false;
    });
    
    // Main monitoring loop with 5-minute reconnection
    Session* session = setupSession();
    auto startTime = std::chrono::steady_clock::now();
    
    std::cout << "Recording data. Will reconnect every 24 hours. Ctrl+C to stop." << std::endl;
    
    while (running) {
        // Get cur time
        auto currentTime = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::hours>(currentTime - startTime);
        
        //check if 24 hours has passed
        if (duration.count() >= 24) {
            std::cout << "24 hours passed. Reconnecting to exchanges..." << std::endl;
            
            // stop the current session
            session->stop();
            std::cout << "Session steopped" << std::endl;
            delete session;
            
            // wait to ensure clean disconnection
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // restart the session
            session = setupSession();
            
            // reset the start time
            startTime = std::chrono::steady_clock::now();
        }    
        // Sleep to prevent tight looping
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    // Clean up
    dataQueue.stop();
    if (writerThread.joinable()) {
        writerThread.join();
    }
    
    // Final session cleanup
    if (session) {
        session->stop();
        delete session;
    }
    
    std::cout << "Recorder stopped" << std::endl;
    return EXIT_SUCCESS;
}
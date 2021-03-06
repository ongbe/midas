#include <utils/convert/StringHelper.h>
#include <utils/convert/TimeHelper.h>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>
#include "DataLoader.h"

std::map<string, std::shared_ptr<CThostFtdcProductField>> DataLoader::load_products(const string &pathName) {
    map<string, std::shared_ptr<CThostFtdcProductField>> products;
    ifstream ifs(pathName, ifstream::in);

    if (ifs.is_open()) {
        string line, key, value;
        while (getline(ifs, line)) {
            if (!line.empty()) {
                std::shared_ptr<CThostFtdcProductField> product(new CThostFtdcProductField);
                stringstream ss;
                ss << line;
                while (ss >> key) {
                    if (key == "ProductID:") {
                        ss >> product->ProductID;
                    } else if (key == "VolumeMultiple:") {
                        ss >> product->VolumeMultiple;
                    } else if (key == "PriceTick:") {
                        ss >> product->PriceTick;
                    } else if (key == "MaxMarketOrderVolume:") {
                        ss >> product->MaxMarketOrderVolume;
                    } else if (key == "MinMarketOrderVolume:") {
                        ss >> product->MinMarketOrderVolume;
                    } else if (key == "MaxLimitOrderVolume:") {
                        ss >> product->MaxLimitOrderVolume;
                    } else if (key == "MinLimitOrderVolume:") {
                        ss >> product->MinLimitOrderVolume;
                    } else {
                        ss >> value;
                    }
                }
                products.insert({std::string(product->ProductID), std::move(product)});
            }
        }

        ifs.close();
    } else {
        throw "no product file!";
    }
    return products;
}

void DataLoader::load(const string &pathName) {
    boost::filesystem::path file(pathName);
    if (boost::filesystem::is_regular_file(file)) {
        load_single_file(pathName);
    } else if (boost::filesystem::is_directory(file)) {
        boost::filesystem::directory_iterator end;
        for (boost::filesystem::directory_iterator i(file); i != end; ++i) {
            boost::filesystem::path p = i->path();

            if (is_regular_file(p)) load_single_file(p.string());
        }
    }
}

void DataLoader::load_single_file(const string &pathName) {
    currentFileName = pathName;
    boost::filesystem::path file(currentFileName);
    instrumentName = file.filename().replace_extension().string();
    rawData.clear();
    ifstream ifs(currentFileName, ifstream::in);

    if (ifs.is_open()) {
        string s;
        while (getline(ifs, s)) {
            if (!s.empty()) {
                rawData.push_back(s);
            }
        }

        ifs.close();
    }

    process_data();
}

void DataLoader::process_data() {
    if (rawData.empty()) return;
    analysis_header(rawData[0]);
    if (hasHeader && rawData.size() == 1) return;
    analysis_format(hasHeader ? rawData[1] : rawData[0]);

    if (format == RawDataFormat::Type1) {
        load_format_type1();
    } else if (format == RawDataFormat::Type2) {
        load_format_type2();
    }
}

void DataLoader::analysis_header(const string &content) {
    long digitCount = count_if(content.begin(), content.end(), [](char c) { return c <= '9' && c >= '0'; });
    long letterCount = count_if(content.begin(), content.end(),
                                [](char c) { return (c <= 'z' && c >= 'a') || (c <= 'Z' && c >= 'A'); });
    hasHeader = (letterCount > digitCount);
}

void DataLoader::analysis_format(const string &content) {
    long commaCount = count(content.begin(), content.end(), ',');
    if (commaCount == 0)
        format = RawDataFormat::Unknown;
    else if (commaCount == 6)
        format = RawDataFormat::Type1;
    else if (commaCount == 5)
        format = RawDataFormat::Type2;
}

void DataLoader::load_format_type1() {
    vector<CandleData> result;
    auto itr = rawData.begin();
    if (hasHeader) ++itr;
    for (; itr != rawData.end(); ++itr) {
        vector<string> lets = midas::string_split(*itr);
        if (lets.size() == 7) {
            int date = midas::cob_from_dash(lets[0]);
            int time = midas::intraday_time_HMS(lets[1]);
            double open = std::atof(lets[2].c_str());
            double high = std::atof(lets[3].c_str());
            double low = std::atof(lets[4].c_str());
            double close = std::atof(lets[5].c_str());
            double volume = std::atof(lets[6].c_str());
            result.push_back(CandleData{date, time, open, high, low, close, volume});
        }
    }
    instrument2candle.insert({instrumentName, std::move(result)});
}

void DataLoader::load_format_type2() {
    vector<CandleData> result;
    auto itr = rawData.begin();
    if (hasHeader) ++itr;
    for (; itr != rawData.end(); ++itr) {
        vector<string> lets = midas::string_split(*itr);
        if (lets.size() == 6) {
            int date = midas::cob_from_dash(lets[0]);
            int time = midas::intraday_time_HMS(lets[0].c_str() + 11);
            double open = std::atof(lets[1].c_str());
            double high = std::atof(lets[2].c_str());
            double low = std::atof(lets[3].c_str());
            double close = std::atof(lets[4].c_str());
            double volume = std::atof(lets[5].c_str());
            result.push_back(CandleData{date, time, open, high, low, close, volume});
        }
    }
    instrument2candle.insert({instrumentName, std::move(result)});
}

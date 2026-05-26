#pragma once
#include <mutex>
#include <iostream>
#include <cstdio>
#include <string>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <memory>
#include <ctime>
#include <unistd.h>
using namespace std;
namespace fs = std::filesystem;

#define LOG(level) _logger(level, __FILE__, __LINE__)
#define ENABLE_CONSOLE() _logger.EnableConsoleLogStrategy()
#define ENABLE_FILE() _logger.EnableFileLogStrategy()

#define DLOG _logger(LogLevel::DEBUG, __FILE__, __LINE__)
#define ILOG _logger(LogLevel::INFO, __FILE__, __LINE__)
#define WLOG _logger(LogLevel::WARNING, __FILE__, __LINE__)
#define ELOG _logger(LogLevel::ERROR, __FILE__, __LINE__)
#define FLOG _logger(LogLevel::FATAL, __FILE__, __LINE__)

const string gsep = "\r\n";

class Strategy
{
public:
    virtual ~Strategy() = default;
    virtual void SyncLog(const string &txt) = 0;
};

class ConsoleLogStrategy : public Strategy
{
public:
    virtual void SyncLog(const string &txt) override
    {
        unique_lock<std::mutex> lock(_mutex);
        cout << txt << gsep;
    }

private:
    std::mutex _mutex;
};

const string defaultpath = "./log";
const string defaultname = "my.log";
class FileLogStrategy : public Strategy
{
public:
    FileLogStrategy()
        : _filepath(defaultpath), _filename(defaultname)
    {
        unique_lock<std::mutex> lock(_mutex);
        if (_filepath.back() != '/')
            _filepath += "/";
        if (!fs::exists(_filepath))
        {
            try
            {
                fs::create_directories(_filepath);
            }
            catch (const fs::filesystem_error &e)
            {
                std::cerr << "创建目录失败: " << e.what() << '\n';
            }
        }
        _name = _filepath + _filename;
    }
    virtual void SyncLog(const string &txt) override
    {
        unique_lock<std::mutex> lock(_mutex);
        ofstream out(_name.c_str(), ios::app);
        if (out.is_open())
        {
            out << txt << gsep;
            out.close();
        }
    }

private:
    string _filepath;
    string _filename;
    string _name;
    std::mutex _mutex;
};

enum class LogLevel
{
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

std::string Level2Str(LogLevel level)
{
    switch (level)
    {
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARNING:
        return "WARNING";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

std::string GetTimeStamp()
{
    time_t curr = time(nullptr);
    struct tm curr_tm;
    localtime_r(&curr, &curr_tm);
    char timebuffer[128];
    snprintf(timebuffer, sizeof(timebuffer), "%4d-%02d-%02d %02d:%02d:%02d",
             curr_tm.tm_year + 1900, curr_tm.tm_mon + 1, curr_tm.tm_mday,
             curr_tm.tm_hour, curr_tm.tm_min, curr_tm.tm_sec);
    return timebuffer;
}

class Logger
{
public:
    class LoggerMessage
    {
    public:
        LoggerMessage(LogLevel level, const string &name, int line, Logger &log)
            : _log(log)
        {
            stringstream ss;
            ss << "[" << GetTimeStamp() << "]"
               << "[" << Level2Str(level) << "]"
               << "[" << getpid() << "]"
               << "[" << name << "]"
               << "[" << line << "]";
            _loginfo += ss.str();
        }
        template <class T>
        LoggerMessage &operator<<(const T &data)
        {
            stringstream ss;
            ss << data;
            _loginfo += ss.str();
            return *this;
        }

        ~LoggerMessage()
        {
            if (_log._s)
            {
                // 调用父类 Logger 选择的策略进行落盘
                _log._s->SyncLog(_loginfo);
            }
        }

    private:
        Logger &_log;
        string _loginfo;
    };

    Logger() { EnableConsoleLogStrategy(); }
    LoggerMessage operator()(LogLevel level, const string &name, int line)
    {
        return LoggerMessage(level, name, line, *this);
    }

    void EnableConsoleLogStrategy() { _s = make_unique<ConsoleLogStrategy>(); }
    void EnableFileLogStrategy() { _s = make_unique<FileLogStrategy>(); }

private:
    unique_ptr<Strategy> _s;
};

Logger _logger;

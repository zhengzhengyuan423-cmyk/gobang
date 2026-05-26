#pragma once
#include "log.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <string>
#include <memory>
#include <vector>
#include <cstdint>
#include <jsoncpp/json/json.h>
#include <mysql/mysql.h>

#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>

class mysql_util
{
public:
    // 1. 连接数据库
    static MYSQL *mysql_create(const std::string &host,
                               const std::string &username,
                               const std::string &password,
                               const std::string &dbname,
                               uint16_t port = 3306)
    {
        //(1) 初始化句柄
        MYSQL *mysql = mysql_init(NULL);
        if (mysql == NULL)
        {
            ELOG << "mysql init failed!";
        }
        // (2) 连接服务器
        if (mysql_real_connect(mysql,
                               host.c_str(),
                               username.c_str(),
                               password.c_str(),
                               dbname.c_str(), port, NULL, 0) == NULL)
        {
            ELOG << "connect mysql server failed :" << mysql_error(mysql);
            mysql_close(mysql);
            return NULL;
        }
        // (3) 设置客户端字符集
        if (mysql_set_character_set(mysql, "utf8") != 0)
        {
            ELOG << "set client character failed :" << mysql_error(mysql);
            mysql_close(mysql);
            return NULL;
        }
        return mysql;
    }
    // 2. 执行查询
    static bool mysql_exec(MYSQL *mysql, const std::string &sql)
    {
        if (mysql == nullptr || sql.empty())
        {
            ELOG << "MySQL exec failed: Invalid arguments.";
            return false;
        }
        int ret = mysql_query(mysql, sql.c_str());
        if (ret != 0)
        {
            ELOG << sql.c_str();
            ELOG << "mysql query failed " << mysql_error(mysql);
            return false;
        }
        return true;
    }
    // 3. 关闭数据库连接
    static void mysql_destroy(MYSQL *mysql)
    {
        if (mysql != NULL)
        {
            mysql_close(mysql);
        }
        return;
    }
};

class json_util
{
public:
    // 序列化：传入一个json串，输出str
    static bool serialize(const Json::Value &value, std::string &str)
    {
        Json::StreamWriterBuilder swb;                                 // 配置和生成真正的 StreamWriter
        std::unique_ptr<Json::StreamWriter> sw(swb.newStreamWriter()); // 工厂方法
        std::stringstream ss;                                          // 字符串流
        int ret = sw->write(value, &ss);                               // 将value，按照配置格式化，然后倒进标准的输出流
        if (ret != 0)
        {
            ELOG << "json serialize failed!!";
            return false;
        }
        str = ss.str();
        return true;
    }
    // 反序列化：传入一个str，输出json串
    static bool unserialize(const std::string &str, Json::Value &value)
    {
        Json::CharReaderBuilder crb;                               // 配置和生成真正的 CharReader
        std::unique_ptr<Json::CharReader> cr(crb.newCharReader()); // 工厂方法
        std::string err;
        bool ret = cr->parse(str.c_str(), str.c_str() + str.size(), &value, &err);
        // bool CharReader::parse(char const* beginDoc, char const* endDoc, Value* root（输出型参数）, std::string* errs（输出型参数）)
        // beginDoc：指向要解析的字符串的起始字符指针（代码中传入 str.c_str()）
        // endDoc：指向要解析的字符串的末尾结束指针（代码中传入 str.c_str() + str.size()）
        // errs：MySQL / 语法错误原因会被写入这个 string
        if (ret == false)
        {
            ELOG << "json unserialize failed:" << err.c_str();
            return false;
        }
        return true;
    }
};

class string_util
{
public:
    static int split(const std::string &in, const std::string &sep, std::vector<std::string> &arry)
    {
        if (in.empty())
            return 0;
        if (sep.empty())
        {
            arry.push_back(in);
            return 1;
        }

        size_t start = 0;
        size_t pos = 0;
        int count = 0;

        // 循环查找分隔符
        while ((pos = in.find(sep, start)) != std::string::npos)
        {
            std::string sub = in.substr(start, pos - start);
            if (!sub.empty())
            {
                arry.push_back(sub);
                count++;
            }
            start = pos + sep.size(); // 跳过当前分隔符
        }

        // 处理最后一个分隔符后面的剩余子串
        if (start < in.size()) // 只有当剩余字符数大于 0 时才切取
        {
            std::string sub = in.substr(start);
            if (!sub.empty())
            {
                arry.push_back(sub);
                count++;
            }
        }

        return count;
    }
};

class file_util
{
public:
    static bool read(const std::string &filename, std::string &body)
    {
        std::ifstream in(filename, std::ios::binary | std::ios::in);
        if (!in.is_open())
        {
            std::cerr << "Open file failed: " << filename << std::endl;
            return false;
        }

        // 利用 stringstream 缓冲区一次性高效倒入 string
        std::stringstream ss;
        ss << in.rdbuf();
        body = ss.str();

        in.close();
        return true;
    }
};
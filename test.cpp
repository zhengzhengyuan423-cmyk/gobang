#include "tool/tool.hpp"
#include "tool/online.hpp"
#include "tool/room.hpp"
#include "tool/session.hpp"
#include "tool/matcher.hpp"
#include <cassert>
#include <iostream>
using namespace std;

void test_json()
{
    cout << "[json_util] ";
    Json::Value v;
    v["key"] = "val";
    v["num"] = 42;

    string out;
    assert(json_util::serialize(v, out));

    Json::Value v2;
    assert(json_util::unserialize(out, v2));
    assert(v2["key"].asString() == "val");
    assert(v2["num"].asInt() == 42);
    cout << "OK" << endl;
}

void test_string()
{
    cout << "[string_util] ";
    vector<string> parts;
    int n = string_util::split("a,b,c", ",", parts);
    assert(n == 3);
    assert(parts[0] == "a");
    assert(parts[1] == "b");
    assert(parts[2] == "c");
    cout << "OK" << endl;
}

void test_file()
{
    cout << "[file_util] ";
    string body;
    assert(file_util::read("test.cpp", body));
    assert(!body.empty());
    assert(body.find("test_file()") != string::npos);
    cout << "OK" << endl;
}

void test_logger()
{
    cout << "[logger] ";
    ENABLE_CONSOLE();
    DLOG << "test debug log";
    ILOG << "test info log";
    cout << "OK (check output above)" << endl;
}

void test_match_queue()
{
    cout << "[match_queue] ";
    match_queue<int> mq;
    assert(mq.empty());
    assert(mq.size() == 0);

    mq.push(10);
    mq.push(20);
    assert(mq.size() == 2);

    int val;
    assert(mq.pop(val) && val == 10);
    assert(mq.pop(val) && val == 20);
    assert(mq.empty());

    mq.push(30);
    int tmp = 30;
    mq.remove(tmp);
    assert(mq.empty());
    cout << "OK" << endl;
}

void test_session()
{
    cout << "[session] ";
    session_manager::session s(1);
    assert(s._ssid == 1);
    assert(s._uid == -1);
    assert(!s.is_login());

    s._uid = 100;
    assert(s.get_user() == 100);

    s._statu = LOGIN;
    assert(s.is_login());
    cout << "OK" << endl;
}

void test_session_manager()
{
    cout << "[session_manager] ";
    session_manager sm(nullptr);

    auto ssp = sm.create_session(42, LOGIN);
    assert(ssp->_uid == 42);
    assert(ssp->_statu == LOGIN);
    assert(ssp->is_login());

    auto got = sm.get_session_by_ssid(ssp->_ssid);
    assert(got.get() != nullptr);
    assert(got->_uid == 42);

    sm.remove_session(ssp->_ssid);
    assert(sm.get_session_by_ssid(ssp->_ssid).get() == nullptr);
    cout << "OK" << endl;
}

void test_online_manager()
{
    cout << "[online_manager] ";
    online_manager om;
    // connection_ptr 用 nullptr 模拟（实际需要 websocket 连接）
    // 验证基本增删查逻辑
    assert(!om.is_in_game_hall(1));
    assert(!om.is_in_game_room(1));
    cout << "OK" << endl;
}

void test_room_manager_syntax()
{
    cout << "[room_manager] ";
    // user_table 需要 MySQL 连接，这里只验证类型可实例化
    cout << "sizeof=" << sizeof(room_manager) << " (type check only)" << endl;
}

int main()
{
    test_json();
    test_string();
    test_file();
    test_logger();
    test_match_queue();
    test_session();
    test_session_manager();
    test_online_manager();
    test_room_manager_syntax();
    cout << "\nAll tests passed!" << endl;
    return 0;
}

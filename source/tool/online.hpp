#pragma once
#include "tool.hpp"

// 在线用户管理器，当多个玩家通过 WebSocket 连接到服务器时，
// 我们必须知道“哪个玩家（UID）对应哪条网络连接（Connection）”，这样才能实现定向的数据推送
class online_manager
{
private:
    std::mutex _mutex;
    // 用于建立游戏大厅用户的用户ID与通信连接的关系
    std::unordered_map<int, wsserver_t::connection_ptr> _hall_user;
    // 用于建立游戏房间用户的用户ID与通信连接的关系
    std::unordered_map<int, wsserver_t::connection_ptr> _room_user;
    // wsserver_t::connection_ptr是一个引用计数型智能指针（std::shared_ptr），指向一条具体的、已经建立好的网络通信管道。
public:
    // websocket连接建立的时候，加入游戏大厅&游戏房间在线用户管理
    void enter_game_hall(int uid, wsserver_t::connection_ptr &conn)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _hall_user.insert(std::make_pair(uid, conn));
    }
    void enter_game_room(int uid, wsserver_t::connection_ptr &conn)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _room_user.insert(std::make_pair(uid, conn));
    }

    // websocket连接断开的时候，移除游戏大厅&游戏房间在线用户管理
    void exit_game_hall(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _hall_user.erase(uid);
    }
    void exit_game_room(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _room_user.erase(uid);
    }

    // 判断当前指定用户是否在游戏大厅/游戏房间
    bool is_in_game_hall(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _hall_user.find(uid);
        if (it == _hall_user.end())
        {
            return false;
        }
        return true;
    }
    bool is_in_game_room(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _room_user.find(uid);
        if (it == _room_user.end())
        {
            return false;
        }
        return true;
    }

    // 通过用户ID在游戏大厅/游戏房间用户管理中获取对应的通信连接
    wsserver_t::connection_ptr get_conn_from_hall(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _hall_user.find(uid);
        if (it == _hall_user.end())
        {
            return wsserver_t::connection_ptr();
        }
        return it->second;
    }

    wsserver_t::connection_ptr get_conn_from_room(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _room_user.find(uid);
        if (it == _room_user.end())
        {
            return wsserver_t::connection_ptr();
        }
        return it->second;
    }
};

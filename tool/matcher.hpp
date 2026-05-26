#pragma once
#include "tool.hpp"
#include "online.hpp"
#include "room.hpp"

// 游戏匹配模块，负责将玩家进行匹配，创建房间，并将玩家加入房间中
template <class T>
class match_queue
{
private:
    std::list<T> _list; // 用于存储匹配玩家的用户ID，使用链表是因为链表在频繁的插入和删除操作中效率较高
    std::mutex _mutex;
    std::condition_variable _cond; // 条件变量，用于在匹配线程中等待队列中玩家数量达到2个以上时被唤醒

public:
    /*获取元素个数*/
    int size()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _list.size();
    }

    /*判断是否为空*/
    bool empty()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _list.empty();
    }

    /*阻塞线程*/
    void wait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _cond.wait(lock);
    }

    /*入队数据，并唤醒线程*/
    void push(const T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.push_back(data);
        _cond.notify_all();
    }

    /*出队数据*/
    bool pop(T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        if (_list.empty() == true)
            return false;
        data = _list.front();
        _list.pop_front();
        return true;
    }

    /*移除指定的数据*/
    void remove(T &data)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _list.remove(data);
    }
};

class matcher
{
private:
    /*普通选手匹配队列*/
    match_queue<uint64_t> _q_Bronze;
    /*高手匹配队列*/
    match_queue<uint64_t> _q_Silver;
    /*大神匹配队列*/
    match_queue<uint64_t> _q_Gold;
    /*对应三个匹配队列的处理线程*/
    std::thread _th_Bronze;
    std::thread _th_Silver;
    std::thread _th_Gold;
    room_manager *_rm;
    user_table *_ut;
    online_manager *_om;

private:
    void handle_match(match_queue<uint64_t> &mq)
    {
        while (1)
        {
            while (mq.size() < 2)
                mq.wait();
            uint64_t uid1, uid2;
            bool ret = mq.pop(uid1);
            if (ret == false)
                continue;
            ret = mq.pop(uid2);
            if (ret == false)
            {
                this->add(uid1);
                continue;
            }
            // 校验两个玩家是否在线，如果有人掉线，则要吧另一个人重新添加入队列
            wsserver_t::connection_ptr conn1 = _om->get_conn_from_hall(uid1);
            if (conn1.get() == nullptr)
            {
                this->add(uid2);
                continue;
            }
            wsserver_t::connection_ptr conn2 = _om->get_conn_from_hall(uid2);
            if (conn2.get() == nullptr)
            {
                this->add(uid1);
                continue;
            }
            // 为两个玩家创建房间，并将玩家加入房间中
            room_ptr rp = _rm->create_room(uid1, uid2);
            if (rp.get() == nullptr)
            {
                this->add(uid1);
                this->add(uid2);
                continue;
            }
            // 对两个玩家进行响应
            Json::Value resp;
            resp["optype"] = "match_success";
            resp["result"] = true;
            std::string body;
            json_util::serialize(resp, body);
            conn1->send(body);
            conn2->send(body);
        }
    }
    void th_Bronze_entry() { return handle_match(_q_Bronze); }
    void th_Silver_entry() { return handle_match(_q_Silver); }
    void th_Gold_entry() { return handle_match(_q_Gold); }

public:
    matcher(room_manager *rm, user_table *ut, online_manager *om)
        : _rm(rm), _ut(ut), _om(om),
          _th_Bronze(std::thread(&matcher::th_Bronze_entry, this)),
          _th_Silver(std::thread(&matcher::th_Silver_entry, this)),
          _th_Gold(std::thread(&matcher::th_Gold_entry, this))
    {
        DLOG << "游戏匹配模块初始化完毕....";
    }
    // 根据玩家的天梯分数，来判定玩家档次，添加到不同的匹配队列
    bool add(uint64_t uid)
    {
        //  1. 根据用户ID，获取玩家信息
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if (ret == false)
        {
            DLOG << "获取玩家:" << uid << " 信息失败！！";
            return false;
        }
        int score = user["score"].asInt();
        // 2. 添加到指定的队列中
        if (score < 2000)
            _q_Bronze.push(uid);
        else if (score >= 2000 && score < 3000)
            _q_Silver.push(uid);
        else
            _q_Gold.push(uid);
        return true;
    }

    bool del(uint64_t uid)
    {
        Json::Value user;
        bool ret = _ut->select_by_id(uid, user);
        if (ret == false)
        {
            DLOG << "获取玩家:" << uid << " 信息失败！！";
            return false;
        }
        int score = user["score"].asInt();
        if (score < 2000)
            _q_Bronze.remove(uid);
        else if (score >= 2000 && score < 3000)
            _q_Silver.remove(uid);
        else
            _q_Gold.remove(uid);
        return true;
    }
};

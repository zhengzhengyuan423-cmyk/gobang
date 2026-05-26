#pragma once
#include "tool.hpp"

typedef enum
{
    UNLOGIN,
    LOGIN
} ss_statu;
// UNLOGIN（未登录）
// LOGIN（已登录）

#define SESSION_TIMEOUT 30000
#define SESSION_FOREVER -1

// session管理器负责session的创建、获取、销毁和生命周期管理
class session_manager
{
public:
    struct session
    {
        int _ssid;
        // session ID，唯一标识一个session，登录成功后创建session，并且将session ID返回给客户端，
        // 客户端在后续的请求中携带这个session ID，服务器通过这个session ID来获取session信息
        int _uid;                  // session对应的用户ID
        ss_statu _statu;           // 用户状态
        wsserver_t::timer_ptr _tp; // session关联的定时器

        session(int ssid) : _ssid(ssid), _uid(-1), _statu(UNLOGIN) {}
        int get_user() { return _uid; }
        bool is_login() { return (_statu == LOGIN); }
    };

private:
    int _next_ssid; // session ID计数器，初始值为1，每创建一个session就自增1
    std::mutex _mutex;
    std::unordered_map<int, std::shared_ptr<session>> _session; // 用于管理session ID与session信息的关系，session信息通过智能指针进行管理
    wsserver_t *_server;                                        // 用于设置session的定时器，管理session的生命周期

public:
    session_manager(wsserver_t *srv) : _next_ssid(1), _server(srv) //
    {
        DLOG << "session管理器初始化完毕";
    }
    ~session_manager() { DLOG << "session管理器即将销毁"; }
    std::shared_ptr<session> create_session(int uid, ss_statu statu)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        std::shared_ptr<session> ssp(new session(_next_ssid));
        ssp->_statu = statu;
        ssp->_uid = uid;
        _session.insert(std::make_pair(_next_ssid, ssp));
        _next_ssid++;
        return ssp;
    }

    void append_session(const std::shared_ptr<session> &ssp)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _session.insert(std::make_pair(ssp->_ssid, ssp));
    }

    std::shared_ptr<session> get_session_by_ssid(int ssid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _session.find(ssid);
        if (it == _session.end())
        {
            return std::shared_ptr<session>();
        }
        return it->second;
    }

    void remove_session(int ssid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _session.erase(ssid);
    }

    void set_session_expire_time(int ssid, int ms)
    {
        std::shared_ptr<session> ssp = get_session_by_ssid(ssid);
        if (ssp.get() == nullptr) // get()是shared_ptr<T> 的成员函数，返回它管理的裸指针 T*
            return;
        wsserver_t::timer_ptr &tp = ssp->_tp;
        // 1. 在session不存在定时删除任务的情况下，设置session永久存在，不需要设置定时器
        if (tp.get() == nullptr && ms == SESSION_FOREVER)
            return;
        // 2. 在session不存在定时删除任务的情况下，设置session在指定时间无通信后被删除，需要设置定时器
        // wsserver_t::timer_ptr set_timer(long ms, callback_t callback);
        else if (tp.get() == nullptr && ms != SESSION_FOREVER)
        {
            wsserver_t::timer_ptr tmp_tp = _server->set_timer(ms, std::bind(&session_manager::remove_session, this, ssid));
            tp = tmp_tp;
        }
        // 3. 在session存在定时删除任务的情况下，设置session永久存在，需要取消定时器，并且重置session关联的定时器
        else if (tp.get() != nullptr && ms == SESSION_FOREVER)
        {
            tp->cancel();                 // 因为这个取消定时任务并不是立即取消的，所以需要重置session关联的定时器
            tp = wsserver_t::timer_ptr(); // 重置session关联的定时器
            _server->set_timer(0, std::bind(&session_manager::append_session, this, ssp));
            // bind绑定类的非静态成员函数必须依赖一个具体的类对象实例
            // 将session重新添加到session管理器中，确保session关联的定时器被重置
        }
        // 4. 在session存在定时删除任务的情况下，设置session在指定时间无通信后被删除，需要取消定时器，并且重置session关联的定时器
        else if (tp.get() != nullptr && ms != SESSION_FOREVER)
        {
            tp->cancel();
            tp = wsserver_t::timer_ptr();
            _server->set_timer(0, std::bind(&session_manager::append_session, this, ssp));

            // 重新给session添加定时销毁任务
            wsserver_t::timer_ptr tmp_tp = _server->set_timer(ms, std::bind(&session_manager::remove_session, this, ssp->_ssid));
            // 重新设置session关联的定时器
            tp = tmp_tp;
        }
    }
};

using session_ptr = std::shared_ptr<session_manager::session>;

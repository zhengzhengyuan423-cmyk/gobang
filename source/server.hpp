#pragma once
#include "tool/db.hpp"
#include "tool/matcher.hpp"
#include "tool/online.hpp"
#include "tool/room.hpp"
#include "tool/session.hpp"
#include "tool/util.hpp"

#define WWWROOT "./client/"
class gobang_server
{
private:
    std::string _web_root;
    wsserver_t _wssrv;
    user_table _ut;
    online_manager _om;
    room_manager _rm;
    matcher _mm;
    session_manager _sm;

private:
    void file_handler(wsserver_t::connection_ptr &conn)
    // 处理HTTP请求，返回静态资源文件
    {
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        std::string realpath = _web_root + uri;
        if (realpath.back() == '/')
            realpath += "login.html";
        Json::Value resp_json;
        std::string body;
        bool ret = file_util::read(realpath, body); // 通过文件工具类读取指定路径的文件内容到body中
        if (ret == false)
        {
            body += "<html>";
            body += "<head>";
            body += "<meta charset='UTF-8'/>";
            body += "</head>";
            body += "<body>";
            body += "<h1> Not Found </h1>";
            body += "</body>";
            conn->set_status(websocketpp::http::status_code::not_found);
            conn->set_body(body);
            return;
        }
        conn->set_body(body);
        conn->set_status(websocketpp::http::status_code::ok);
    }

    void http_resp(wsserver_t::connection_ptr &conn, bool result,
                   websocketpp::http::status_code::value code, const std::string &reason)
    // 处理HTTP请求，返回JSON格式的响应结果
    {
        Json::Value resp_json;
        resp_json["result"] = result;
        resp_json["reason"] = reason;
        std::string resp_body;
        json_util::serialize(resp_json, resp_body);
        conn->set_status(code);
        conn->set_body(resp_body);
        conn->append_header("Content-Type", "application/json");
        // 对应前端的fetch请求中设置的Content-Type，告诉前端响应正文的格式是json格式，前端就会自动进行json反序列化，
        // 否则前端会把响应正文当成普通字符串来处理，导致无法正确解析响应结果
        return;
    }

    void reg(wsserver_t::connection_ptr &conn)
    {
        websocketpp::http::parser::request req = conn->get_request();
        std::string req_body = conn->get_request_body(); // 获取请求正文
        Json::Value login_info;
        bool ret = json_util::unserialize(req_body, login_info);
        if (ret == false)
        {
            DLOG << "反序列化注册信息失败";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请求的正文格式错误");
        }
        if (login_info["username"].isNull() || login_info["password"].isNull())
        {
            DLOG << "用户名密码不完整";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请输入用户名/密码");
        }
        ret = _ut.insert(login_info);
        if (ret == false)
        {
            DLOG << "向数据库插入数据失败";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "用户名已经被占用!");
        }
        //  如果成功了，则返回200
        return http_resp(conn, true, websocketpp::http::status_code::ok, "注册用户成功");
    }

    void login(wsserver_t::connection_ptr &conn)
    {
        // 用户登录功能请求的处理
        // 1. 获取请求正文，并进行json反序列化，得到用户名和密码
        std::string req_body = conn->get_request_body();
        Json::Value login_info;
        bool ret = json_util::unserialize(req_body, login_info);
        if (ret == false)
        {
            DLOG << "反序列化登录信息失败";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请求的正文格式错误");
        }
        // 2. 校验正文完整性，进行数据库的用户信息验证
        if (login_info["username"].isNull() || login_info["password"].isNull())
        {
            DLOG << "用户名密码不完整";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "请输入用户名/密码");
        }
        ret = _ut.login(login_info);
        // login函数会把查询到的用户信息写回login_info中
        if (ret == false)
        {
            DLOG << "用户名密码错误";
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "用户名密码错误");
        }

        // 3. 如果验证成功，给客户端创建session
        int uid = login_info["id"].asInt();
        // 同一个用户不允许多点登录，如果这个用户已经有session了，则认为这个用户已经在别处登录了，返回错误：账号已在别处登录
        if (_sm.has_session_by_uid(uid))
        {
            return http_resp(conn, false, websocketpp::http::status_code::bad_request, "账号已在别处登录");
        }
        session_ptr ssp = _sm.create_session(uid, LOGIN);
        if (ssp.get() == nullptr)
        {
            DLOG << "创建会话失败";
            return http_resp(conn, false, websocketpp::http::status_code::internal_server_error, "创建会话失败");
        }
        _sm.set_session_expire_time(ssp->_ssid, SESSION_TIMEOUT);

        // 4. 设置响应头部：Set-Cookie,将sessionid通过cookie返回
        std::string cookie_ssid = "SSID=" + std::to_string(ssp->_ssid);
        conn->append_header("Set-Cookie", cookie_ssid);
        return http_resp(conn, true, websocketpp::http::status_code::ok, "登录成功");
    }

    // 从cookie字符串中获取指定key的value
    bool get_cookie_val(const std::string &cookie_str, const std::string &key, std::string &val)
    {
        // cookie字符串的格式：key1=value1; key2=value2; key3=value3
        // Cookie: SSID=123456; theme=dark; lang=zh-CN
        // 1. 以 ; 作为间隔，对字符串进行分割，得到各个单个的cookie信息
        std::string sep = "; ";
        std::vector<std::string> cookie_arr;
        string_util::split(cookie_str, sep, cookie_arr);
        for (auto str : cookie_arr)
        {
            // 2. 对单个cookie字符串，以 = 为间隔进行分割，得到key和val
            std::vector<std::string> tmp_arr;
            string_util::split(str, "=", tmp_arr);
            if (tmp_arr.size() != 2)
            {
                continue;
            }
            if (tmp_arr[0] == key)
            {
                val = tmp_arr[1];
                return true;
            }
        }
        return false;
    }

    void info(wsserver_t::connection_ptr &conn)
    {
        Json::Value err_resp;
        std::string cookie_str = conn->get_request_header("Cookie");
        if (cookie_str.empty())
            return http_resp(conn, true, websocketpp::http::status_code::bad_request, "找不到cookie信息，请重新登录");

        std::string ssid_str;
        bool ret = get_cookie_val(cookie_str, "SSID", ssid_str);
        if (ret == false)
            return http_resp(conn, true, websocketpp::http::status_code::bad_request, "找不到ssid信息，请重新登录");

        session_ptr ssp = _sm.get_session_by_ssid(std::stol(ssid_str));
        if (ssp.get() == nullptr)
            return http_resp(conn, true, websocketpp::http::status_code::bad_request, "登录过期，请重新登录");

        int uid = ssp->get_user();
        Json::Value user_info;
        ret = _ut.select_by_id(uid, user_info);
        if (ret == false)
            return http_resp(conn, true, websocketpp::http::status_code::bad_request, "找不到用户信息，请重新登录");

        std::string body;
        json_util::serialize(user_info, body);
        conn->set_body(body);
        conn->append_header("Content-Type", "application/json");
        conn->set_status(websocketpp::http::status_code::ok);
        // 4. 刷新session的过期时间
        _sm.set_session_expire_time(ssp->_ssid, SESSION_TIMEOUT);
    }

    void http_callback(websocketpp::connection_hdl hdl)
    {
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl);
        // 通过连接句柄获取连接指针（弱转强）
        websocketpp::http::parser::request req = conn->get_request();
        // 把客户端发过来的 HTTP 请求报文结构体给抠出来
        std::string method = req.get_method();
        std::string uri = req.get_uri();
        if (method == "POST" && uri == "/reg")
            return reg(conn);
        else if (method == "POST" && uri == "/login")
            return login(conn);
        else if (method == "GET" && uri == "/info")
            return info(conn);
        else
            return file_handler(conn);
    }

    void ws_resp(wsserver_t::connection_ptr conn, Json::Value &resp)
    {
        std::string body;
        json_util::serialize(resp, body);
        conn->send(body);
    }
    session_ptr get_session_by_cookie(wsserver_t::connection_ptr conn)
    {
        Json::Value err_resp;
        // 1. 获取请求信息中的Cookie，从Cookie中获取ssid
        std::string cookie_str = conn->get_request_header("Cookie");
        if (cookie_str.empty())
        {
            // 如果没有cookie，返回错误：没有cookie信息，让客户端重新登录
            err_resp["optype"] = "hall_ready";
            err_resp["reason"] = "没有找到cookie信息，需要重新登录";
            err_resp["result"] = false;
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        // 1.5. 从cookie中取出ssid
        std::string ssid_str;
        bool ret = get_cookie_val(cookie_str, "SSID", ssid_str);
        if (ret == false)
        {
            // cookie中没有ssid，返回错误：没有ssid信息，让客户端重新登录
            err_resp["optype"] = "hall_ready";
            err_resp["reason"] = "没有找到SSID信息，需要重新登录";
            err_resp["result"] = false;
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        // 2. 在session管理中查找对应的会话信息
        session_ptr ssp = _sm.get_session_by_ssid(std::stol(ssid_str));
        if (ssp.get() == nullptr)
        {
            // 没有找到session，则认为登录已经过期，需要重新登录
            err_resp["optype"] = "hall_ready";
            err_resp["reason"] = "没有找到session信息，需要重新登录";
            err_resp["result"] = false;
            ws_resp(conn, err_resp);
            return session_ptr();
        }
        return ssp;
    }
    void wsopen_game_hall(wsserver_t::connection_ptr conn)
    {
        // 游戏大厅长连接建立成功
        Json::Value resp_json;
        // 1. 登录验证--判断当前客户端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            return;
        }
        // 2. 判断当前客户端是否是重复登录
        if (_om.is_in_game_hall(ssp->get_user()) || _om.is_in_game_room(ssp->get_user()))
        {
            resp_json["optype"] = "hall_ready";
            resp_json["reason"] = "玩家重复登录！";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        // 3. 将当前客户端以及连接加入到游戏大厅
        _om.enter_game_hall(ssp->get_user(), conn);
        // 4. 给客户端响应游戏大厅连接建立成功
        resp_json["optype"] = "hall_ready";
        resp_json["result"] = true;
        ws_resp(conn, resp_json);
        // 5. 记得将session设置为永久存在
        _sm.set_session_expire_time(ssp->_ssid, SESSION_FOREVER);
    }
    void wsopen_game_room(wsserver_t::connection_ptr conn)
    {
        Json::Value resp_json;
        // 1. 获取当前客户端的session
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            return;
        }
        // 2. 当前用户是否已经在在线用户管理的游戏房间或者游戏大厅中---在线用户管理
        if (_om.is_in_game_hall(ssp->get_user()) || _om.is_in_game_room(ssp->get_user()))
        {
            resp_json["optype"] = "room_ready";
            resp_json["reason"] = "玩家重复登录！";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        // 3. 判断当前用户是否已经创建好了房间 --- 房间管理
        room_ptr rp = _rm.get_room_by_uid(ssp->get_user());
        if (rp.get() == nullptr)
        {
            resp_json["optype"] = "room_ready";
            resp_json["reason"] = "没有找到玩家的房间信息";
            resp_json["result"] = false;
            return ws_resp(conn, resp_json);
        }
        // 4. 将当前用户添加到在线用户管理的游戏房间中
        _om.enter_game_room(ssp->get_user(), conn);
        // 5. 将session重新设置为永久存在
        _sm.set_session_expire_time(ssp->_ssid, SESSION_FOREVER);
        // 6. 回复房间准备完毕
        resp_json["optype"] = "room_ready";
        resp_json["result"] = true;
        resp_json["room_id"] = (Json::UInt64)rp->id();
        resp_json["uid"] = (Json::UInt64)ssp->get_user();
        resp_json["white_id"] = (Json::UInt64)rp->get_white_user();
        resp_json["black_id"] = (Json::UInt64)rp->get_black_user();
        resp_json["cur_turn"] = (Json::UInt64)rp->cur_turn();
        return ws_resp(conn, resp_json);
    }
    void wsopen_callback(websocketpp::connection_hdl hdl)
    {
        // websocket长连接建立成功之后的处理函数
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/hall")
            return wsopen_game_hall(conn);
        else if (uri == "/room")
            return wsopen_game_room(conn);
    }
    void wsclose_game_hall(wsserver_t::connection_ptr conn)
    {
        // 游戏大厅长连接断开的处理
        // 1. 登录验证--判断当前客户端是否已经成功登录
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            return;
        }
        // 1. 将玩家从游戏大厅中移除
        _om.exit_game_hall(ssp->get_user());
        // 2. 将session恢复生命周期的管理，设置定时销毁
        _sm.set_session_expire_time(ssp->_ssid, SESSION_TIMEOUT);
    }
    void wsclose_game_room(wsserver_t::connection_ptr conn)
    {
        // 获取会话信息，识别客户端
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            return;
        }
        // 1. 将玩家从在线用户管理中移除
        _om.exit_game_room(ssp->get_user());
        // 2. 将session回复生命周期的管理，设置定时销毁
        _sm.set_session_expire_time(ssp->_ssid, SESSION_TIMEOUT);
        // 3. 将玩家从游戏房间中移除，房间中所有用户退出了就会销毁房间
        _rm.remove_room_user(ssp->get_user());
    }
    void wsclose_callback(websocketpp::connection_hdl hdl)
    {
        // websocket连接断开前的处理
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/hall")
        {
            // 建立了游戏大厅的长连接
            return wsclose_game_hall(conn);
        }
        else if (uri == "/room")
        {
            // 建立了游戏房间的长连接
            return wsclose_game_room(conn);
        }
    }

    void wsmsg_game_hall(wsserver_t::connection_ptr conn, wsserver_t::message_ptr msg)
    {
        Json::Value resp_json;
        std::string resp_body;
        // 1. 身份验证，当前客户端到底是哪个玩家
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            return;
        }
        // 2. 获取请求信息
        std::string req_body = msg->get_payload();
        Json::Value req_json;
        bool ret = json_util::unserialize(req_body, req_json);
        if (ret == false)
        {
            resp_json["result"] = false;
            resp_json["reason"] = "请求信息解析失败";
            return ws_resp(conn, resp_json);
        }
        // 3. 对于请求进行处理：
        if (!req_json["optype"].isNull() && req_json["optype"].asString() == "match_start")
        {
            //  开始对战匹配：通过匹配模块，将用户添加到匹配队列中
            _mm.add(ssp->get_user());
            resp_json["optype"] = "match_start";
            resp_json["result"] = true;
            return ws_resp(conn, resp_json);
        }
        else if (!req_json["optype"].isNull() && req_json["optype"].asString() == "match_stop")
        {
            //  停止对战匹配：通过匹配模块，将用户从匹配队列中移除
            _mm.del(ssp->get_user());
            resp_json["optype"] = "match_stop";
            resp_json["result"] = true;
            return ws_resp(conn, resp_json);
        }
        resp_json["optype"] = "unknow";
        resp_json["reason"] = "请求类型未知";
        resp_json["result"] = false;
        return ws_resp(conn, resp_json);
    }
    void wsmsg_game_room(wsserver_t::connection_ptr conn, wsserver_t::message_ptr msg)
    {
        Json::Value resp_json;
        // 1. 获取客户端session，识别客户端身份
        session_ptr ssp = get_session_by_cookie(conn);
        if (ssp.get() == nullptr)
        {
            DLOG << "房间-没有找到会话信息";
            return;
        }
        // 2. 获取客户端房间信息
        room_ptr rp = _rm.get_room_by_uid(ssp->get_user());
        if (rp.get() == nullptr)
        {
            resp_json["optype"] = "unknow";
            resp_json["reason"] = "没有找到玩家的房间信息";
            resp_json["result"] = false;
            DLOG << "房间-没有找到玩家房间信息";
            return ws_resp(conn, resp_json);
        }
        // 3. 对消息进行反序列化
        Json::Value req_json;
        std::string req_body = msg->get_payload();
        bool ret = json_util::unserialize(req_body, req_json);
        if (ret == false)
        {
            resp_json["optype"] = "unknow";
            resp_json["reason"] = "请求解析失败";
            resp_json["result"] = false;
            DLOG << "房间-反序列化请求失败";
            return ws_resp(conn, resp_json);
        }
        DLOG << "房间：收到房间请求，开始处理....";
        // 4. 通过房间模块进行消息请求的处理
        return rp->handle_request(req_json);
    }
    void wsmsg_callback(websocketpp::connection_hdl hdl, wsserver_t::message_ptr msg)
    {
        // websocket长连接通信处理
        wsserver_t::connection_ptr conn = _wssrv.get_con_from_hdl(hdl);
        websocketpp::http::parser::request req = conn->get_request();
        std::string uri = req.get_uri();
        if (uri == "/hall")
        {
            // 建立了游戏大厅的长连接
            return wsmsg_game_hall(conn, msg);
        }
        else if (uri == "/room")
        {
            // 建立了游戏房间的长连接
            return wsmsg_game_room(conn, msg);
        }
    }

public:
    /*进行成员初始化，以及服务器回调函数的设置*/
    gobang_server(const std::string &host,
                  const std::string &user,
                  const std::string &pass,
                  const std::string &dbname,
                  uint16_t port = 3306,
                  const std::string &wwwroot = WWWROOT) : _web_root(wwwroot), _ut(host, user, pass, dbname, port),
                                                          _rm(&_ut, &_om, &_wssrv), _sm(&_wssrv), _mm(&_rm, &_ut, &_om)
    {
        _wssrv.set_access_channels(websocketpp::log::alevel::none); // 关闭websocketpp的日志输出
        _wssrv.init_asio();                                         // 初始化Asio，准备使用Asio的网络功能
        _wssrv.set_reuse_addr(true);
        _wssrv.set_http_handler(std::bind(&gobang_server::http_callback, this, std::placeholders::_1));
        // 设置HTTP请求的回调函数，当服务器接收到HTTP请求时，会调用http_callback函数进行处理
        _wssrv.set_open_handler(std::bind(&gobang_server::wsopen_callback, this, std::placeholders::_1));
        // 设置WebSocket连接建立成功时的回调函数，当服务器成功建立WebSocket连接时，会调用wsopen_callback函数进行处理
        _wssrv.set_close_handler(std::bind(&gobang_server::wsclose_callback, this, std::placeholders::_1));
        // 设置WebSocket连接断开时的回调函数，当服务器的WebSocket连接断开时，会调用wsclose_callback函数进行处理
        _wssrv.set_message_handler(std::bind(&gobang_server::wsmsg_callback, this, std::placeholders::_1, std::placeholders::_2));
        // 设置WebSocket消息处理函数，当服务器接收到WebSocket消息时，会调用wsmsg_callback函数进行处理
    }
    /*启动服务器*/
    void start(int port)
    {
        _wssrv.listen(port);
        _wssrv.start_accept();
        _wssrv.run();
    }
};

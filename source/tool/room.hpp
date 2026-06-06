#pragma once
#include "tool.hpp"
#include "online.hpp"
#define BOARD_ROW 15
#define BOARD_COL 15
#define CHESS_WHITE 1
#define CHESS_BLACK 2

// GAME_START(进行中)
// GAME_OVER(对局结束)
typedef enum
{
    GAME_START,
    GAME_OVER
} room_statu;

// 先描述：房间类，负责单场对局的落子、胜负判定、聊天和广播
class room
{
private:
    int _room_id;                         // 房间ID（自增）
    room_statu _statu;                    // 房间状态
    int _player_count;                    // 房间中玩家数量（0，1，2）
    int _white_id;                        // 房间中白棋玩家的用户ID
    int _black_id;                        // 房间中黑棋玩家的用户ID
    user_table *_tb_user;                 // 用于更新玩家的胜负记录和分数（数据库句柄）
    online_manager *_online_user;         // 用于获取玩家的通信连接，进行消息广播（在线用户管理器句柄）
    wsserver_t *_server;                  // 用于设置落子超时计时器
    std::vector<std::vector<int>> _board; // 棋盘数据结构，二维数组，0表示没有棋子，1表示白棋，2表示黑棋
    int _cur_turn;                        // 当前轮到哪个玩家（用户ID）
    wsserver_t::timer_ptr _turn_timer;    // 落子超时计时器

private:
    // 沿(row_off, col_off)方向检查是否五连珠
    bool five(int row, int col, int row_off, int col_off, int color)
    {
        auto count_in_direction = [&](int dr, int dc)
        {
            int cnt = 0;
            int r = row + dr, c = col + dc;
            while (r >= 0 && r < BOARD_ROW && c >= 0 && c < BOARD_COL && _board[r][c] == color)
            {
                cnt++;
                r += dr;
                c += dc;
            }
            return cnt;
        };
        return 1 + count_in_direction(row_off, col_off) + count_in_direction(-row_off, -col_off) >= 5;
    }

    int check_win(int row, int col, int color)
    {
        std::pair<int, int> dirs[] = {{0, 1}, {1, 0}, {-1, 1}, {-1, -1}};
        for (const auto &dir : dirs)
        {
            if (five(row, col, dir.first, dir.second, color))
                return color == CHESS_WHITE ? _white_id : _black_id;
        }
        return 0;
    }

public:
    room(int room_id, user_table *tb_user, online_manager *online_user, wsserver_t *server)
        : _room_id(room_id), _statu(GAME_START), _player_count(0),
          _tb_user(tb_user), _online_user(online_user), _server(server),
          _board(BOARD_ROW, std::vector<int>(BOARD_COL, 0)), _cur_turn(0)
    {
        DLOG << _room_id << "房间创建成功!!";
    }
    ~room()
    {
        DLOG << _room_id << "房间销毁成功!!";
    }
    int id() { return _room_id; }
    room_statu statu() { return _statu; }
    int player_count() { return _player_count; }
    void add_white_user(int uid)
    {
        _white_id = uid;
        _player_count++;
    }
    void add_black_user(int uid)
    {
        _black_id = uid;
        _player_count++;
    }
    int get_white_user() { return _white_id; }
    int get_black_user() { return _black_id; }
    int opponent_id(int uid) { return uid == _white_id ? _black_id : _white_id; }
    int chess_color(int uid) { return uid == _white_id ? CHESS_WHITE : CHESS_BLACK; }
    int cur_turn() { return _cur_turn; }
    void set_cur_turn(int uid) { _cur_turn = uid; }

    void cancel_turn_timer()
    {
        if (_turn_timer.get() != nullptr)
        {
            _turn_timer->cancel();
            _turn_timer = wsserver_t::timer_ptr();
        }
    }

    void start_turn_timer()
    {
        cancel_turn_timer();
        int expected_turn = _cur_turn;
        _turn_timer = _server->set_timer(20000, [this, expected_turn](const std::error_code &ec)
        {
            if (ec) return;
            if (_statu == GAME_OVER) return;
            if (_cur_turn != expected_turn) return;
            auto_move();
        });
    }

    void auto_move()
    {
        if (_statu == GAME_OVER) return;
        int cur_uid = _cur_turn;
        // 扫描找到第一个空位
        int row = -1, col = -1;
        for (int r = 0; r < BOARD_ROW && row == -1; r++)
            for (int c = 0; c < BOARD_COL && row == -1; c++)
                if (_board[r][c] == 0) { row = r; col = c; }

        int cur_color = chess_color(cur_uid);
        _board[row][col] = cur_color;

        Json::Value json_resp;
        json_resp["optype"] = "put_chess";
        json_resp["result"] = true;
        json_resp["reason"] = "思考超时，系统自动落子";
        json_resp["room_id"] = _room_id;
        json_resp["uid"] = cur_uid;
        json_resp["row"] = row;
        json_resp["col"] = col;

        int winner_id = check_win(row, col, cur_color);
        json_resp["winner"] = winner_id;
        if (winner_id != 0)
        {
            int loser_id = opponent_id(winner_id);
            _tb_user->win(winner_id);
            _tb_user->lose(loser_id);
            _statu = GAME_OVER;
            json_resp["reason"] = "对方思考超时，五星连珠！";
        }
        else
        {
            _cur_turn = opponent_id(cur_uid);
        }
        broadcast(json_resp);
        if (_statu != GAME_OVER)
            start_turn_timer();
    }

    /*处理下棋动作*/
    Json::Value handle_chess(Json::Value &req)
    {
        Json::Value json_resp = req;
        int chess_row = req["row"].asInt();
        int chess_col = req["col"].asInt();
        int cur_uid = req["uid"].asInt();

        // 游戏已结束
        if (_statu == GAME_OVER)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "游戏已结束！";
            return json_resp;
        }

        // 不是当前回合玩家的操作
        if (cur_uid != _cur_turn)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "还没轮到你下棋！";
            return json_resp;
        }

        // 对手不在线则当前玩家直接胜
        if (_online_user->is_in_game_room(opponent_id(cur_uid)) == false)
        {
            json_resp["result"] = true;
            json_resp["reason"] = "运气真好！对方掉线，不战而胜！";
            json_resp["winner"] = cur_uid;
            return json_resp;
        }

        // 位置已被占用
        if (_board[chess_row][chess_col] != 0)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "当前位置已经有了其他棋子！";
            return json_resp;
        }

        int cur_color = chess_color(cur_uid);
        _board[chess_row][chess_col] = cur_color;

        int winner_id = check_win(chess_row, chess_col, cur_color);
        json_resp["result"] = true;
        json_resp["winner"] = winner_id;
        if (winner_id != 0)
            json_resp["reason"] = "五星连珠，牛批格拉斯！";
        else
            _cur_turn = opponent_id(cur_uid);
        return json_resp;
    }

    /*处理聊天动作*/
    Json::Value handle_chat(Json::Value &req)
    {
        Json::Value json_resp = req;
        // 检测消息中是否包含敏感词
        std::string msg = req["message"].asString();
        size_t pos = msg.find("垃圾");
        if (pos != std::string::npos)
        {
            json_resp["result"] = false;
            json_resp["reason"] = "消息中包含敏感词，不能发送！";
            return json_resp;
        }
        // 广播消息---返回消息
        json_resp["result"] = true;
        return json_resp;
    }

    /*处理玩家退出房间动作*/
    void handle_exit(int uid)
    {
        // 如果是下棋中退出，则对方胜利，否则下棋结束了退出，则是正常退出
        Json::Value json_resp;
        if (_statu == GAME_START)
        {
            int winner_id = opponent_id(uid);
            json_resp["optype"] = "put_chess";
            json_resp["result"] = true;
            json_resp["reason"] = "对方掉线，不战而胜！";
            json_resp["room_id"] = _room_id;
            json_resp["uid"] = uid;
            json_resp["row"] = -1;
            json_resp["col"] = -1;
            json_resp["winner"] = winner_id;
            int loser_id = uid;
            _tb_user->win(winner_id);
            _tb_user->lose(loser_id);
            _statu = GAME_OVER;
            cancel_turn_timer();
            broadcast(json_resp);
        }
        // 房间中玩家数量--
        _player_count--;
        return;
    }

    /*总的请求处理函数，在函数内部，区分请求类型，根据不同的请求调用不同的处理函数，得到响应进行广播*/
    void handle_request(Json::Value &req)
    {
        // 1. 校验房间号是否匹配
        Json::Value json_resp;
        int room_id = req["room_id"].asInt();
        if (room_id != _room_id)
        {
            json_resp["optype"] = req["optype"].asString();
            json_resp["result"] = false;
            json_resp["reason"] = "房间号不匹配！";
            return broadcast(json_resp);
        }
        // 2. 根据不同的请求类型调用不同的处理函数
        if (req["optype"].asString() == "put_chess")
        {
            json_resp = handle_chess(req);
            if (json_resp["winner"].asInt() != 0)
            {
                int winner_id = json_resp["winner"].asInt();
                int loser_id = opponent_id(winner_id);
                _tb_user->win(winner_id);
                _tb_user->lose(loser_id);
                _statu = GAME_OVER;
                cancel_turn_timer();
            }
            else if (json_resp["result"].asBool())
            {
                start_turn_timer();
            }
        }
        else if (req["optype"].asString() == "chat")
        {
            json_resp = handle_chat(req);
        }
        else
        {
            json_resp["optype"] = req["optype"].asString();
            json_resp["result"] = false;
            json_resp["reason"] = "未知请求类型";
        }
        std::string body;
        json_util::serialize(json_resp, body);
        DLOG << "房间-广播动作: " << body;
        return broadcast(json_resp);
    }

    /*将指定的信息广播给房间中所有玩家*/
    void broadcast(Json::Value &rsp)
    {
        rsp["cur_turn"] = _cur_turn;
        // 1. 对要响应的信息进行序列化，将Json::Value中的数据序列化成为json格式字符串
        std::string body;
        json_util::serialize(rsp, body);
        // 2. 获取房间中所有用户的通信连接，发送响应信息
        wsserver_t::connection_ptr wconn = _online_user->get_conn_from_room(_white_id);
        if (wconn.get() != nullptr)
            wconn->send(body);
        else
            DLOG << "房间-白棋玩家连接获取失败";
        wsserver_t::connection_ptr bconn = _online_user->get_conn_from_room(_black_id);
        if (bconn.get() != nullptr)
            bconn->send(body);
        else
            DLOG << "房间-黑棋玩家连接获取失败";
        return;
    }
};

using room_ptr = std::shared_ptr<room>; // 定义房间智能指针类型，方便在房间管理器中进行房间信息的管理

// 在组织：管控对局房间
class room_manager
{
private:
    int _next_rid; // 房间ID计数器，初始值为1，每创建一个房间就自增1
    std::mutex _mutex;
    user_table *_tb_user;                     // 用于更新玩家的胜负记录和分数（数据库句柄）
    online_manager *_online_user;             // 用于获取玩家的通信连接，进行消息广播（在线用户管理器句柄）
    wsserver_t *_server;                      // 用于房间内的计时器
    std::unordered_map<int, room_ptr> _rooms; // 用于管理房间ID与房间信息的关系，房间信息通过智能指针进行管理
    std::unordered_map<int, int> _users;      // 用于管理用户ID与房间ID的关系，方便通过用户ID获取房间信息

public:
    /*初始化房间ID计数器*/
    room_manager(user_table *ut, online_manager *om, wsserver_t *srv)
        : _next_rid(1), _tb_user(ut), _online_user(om), _server(srv)
    {
        DLOG << "房间管理模块初始化完毕！";
    }
    ~room_manager() { DLOG << "房间管理模块即将销毁！"; }

    // 为两个用户创建房间，并返回房间的智能指针管理对象
    room_ptr create_room(int uid1, int uid2)
    {
        // 两个用户在游戏大厅中进行对战匹配，匹配成功后创建房间
        // 1. 校验两个用户是否都还在游戏大厅中，只有都在才需要创建房间。
        if (_online_user->is_in_game_hall(uid1) == false)
        {
            DLOG << "用户：" << uid1 << " 不在大厅中，创建房间失败!";
            return room_ptr(); // 返回一个空的智能指针对象，表示创建房间失败
        }
        if (_online_user->is_in_game_hall(uid2) == false)
        {
            DLOG << "用户：" << uid2 << " 不在大厅中，创建房间失败!";
            return room_ptr();
        }

        // 2. 创建房间，将用户信息添加到房间中
        std::unique_lock<std::mutex> lock(_mutex);
        room_ptr rp(new room(_next_rid, _tb_user, _online_user, _server));
        rp->add_white_user(uid1);
        rp->add_black_user(uid2);
        rp->set_cur_turn(uid1); // 白棋先手
        rp->start_turn_timer();
        // 3. 将房间信息管理起来
        _rooms.insert(std::make_pair(_next_rid, rp));
        _users.insert(std::make_pair(uid1, _next_rid));
        _users.insert(std::make_pair(uid2, _next_rid));
        _next_rid++;
        // 4. 返回房间信息
        return rp;
    }

    /*通过房间ID获取房间信息*/
    room_ptr get_room_by_rid(int rid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        auto it = _rooms.find(rid);
        if (it == _rooms.end())
        {
            return room_ptr();
        }
        return it->second;
    }
    /*通过用户ID获取房间信息*/
    room_ptr get_room_by_uid(int uid)
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // 1. 通过用户ID获取房间ID
        auto uit = _users.find(uid);
        if (uit == _users.end())
        {
            return room_ptr();
        }
        int rid = uit->second;
        // 2. 通过房间ID获取房间信息
        auto rit = _rooms.find(rid);
        if (rit == _rooms.end())
        {
            return room_ptr();
        }
        return rit->second;
    }

    /*通过房间ID销毁房间*/
    void remove_room(int rid)
    {
        // 因为房间信息，是通过shared_ptr在_rooms中进行管理，因此只要将shared_ptr从_rooms中移除
        // 则shared_ptr计数器==0，外界没有对房间信息进行操作保存的情况下就会释放
        // 1. 通过房间ID，获取房间信息
        room_ptr rp = get_room_by_rid(rid);
        if (rp.get() == nullptr)
            return;
        rp->cancel_turn_timer();
        // 2. 通过房间信息，获取房间中所有用户的ID
        int uid1 = rp->get_white_user();
        int uid2 = rp->get_black_user();
        // 3. 移除房间管理中的用户信息
        std::unique_lock<std::mutex> lock(_mutex);
        _users.erase(uid1);
        _users.erase(uid2);
        // 4. 移除房间管理信息
        _rooms.erase(rid);
    }

    /*删除房间中指定用户，如果房间中没有用户了，则销毁房间，用户连接断开时被调用*/
    void remove_room_user(int uid)
    {
        room_ptr rp = get_room_by_uid(uid);
        if (rp.get() == nullptr)
            return;
        // 处理房间中玩家退出动作
        rp->handle_exit(uid);
        // 房间中没有玩家了，则销毁房间
        if (rp->player_count() == 0)
            remove_room(rp->id());
        return;
    }
};
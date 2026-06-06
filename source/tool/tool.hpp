#pragma once
#include "log.hpp"
#include "db.hpp"
#include "util.hpp"
#include <mutex>
#include <unordered_map>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <list>
#include <condition_variable>

typedef websocketpp::server<websocketpp::config::asio> wsserver_t;

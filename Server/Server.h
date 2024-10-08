#ifndef REVPROXY_H
#define REVPROXY_H

#include <asio.hpp>
#include <string>
#include <memory>
#include "Session.h"

#define DEFAULT_BACKOFF_MS 100 
#define MAX_RETRIES 5

class Server
{
    public:
    Server(int local_port, const std::string& cert_path = "", const std::string& key_path = "", bool ssl = false);
    void run();

    private:
    asio::io_context _io_context;
    asio::ssl::context _ssl_context;
    std::shared_ptr<asio::ip::tcp::acceptor> _acceptor;
    asio::ip::tcp::endpoint _endpoint;
    int _port;
    std::size_t _retries;
    bool _ssl;

    void load_certificate(const std::string& cert_path, const std::string& key_path);
    void accept_caller(std::shared_ptr<Session> session);
    void accept_handler(const asio::error_code& error, const std::shared_ptr<Session>& session);
    bool is_error(const asio::error_code& error);
    std::unique_ptr<Socket> socket_factory();
};

#endif
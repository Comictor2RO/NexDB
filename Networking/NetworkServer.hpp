#ifndef NETWORK_SERVER_HPP
#define NETWORK_SERVER_HPP

#include <iostream>
#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <string>
#include "Engine.hpp"

using asio::ip::tcp;

class NetworkServer {
    public:
        NetworkServer(size_t port, Engine &engine);

        void start();
        void stop();

    private:
        size_t port;
        asio::io_context io_context;
        tcp::acceptor acceptor;
        Engine &engine;

        void openServer();
        void acceptConnections();
        void handleClient(tcp::socket socket);
        std::string executeQuery(std::string &query);
};

#endif

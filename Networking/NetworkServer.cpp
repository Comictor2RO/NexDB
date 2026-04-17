#include "NetworkServer.hpp"
#include <iostream>

NetworkServer::NetworkServer(size_t port, Engine &engine)
    : port(port), engine(engine), acceptor(io_context)
{};

void NetworkServer::start()
{
    openServer();
    acceptConnections();
    io_context.run();
}

void NetworkServer::stop()
{
    acceptor.close();
    io_context.stop();
    std::cout << "Server stopped." << std::endl;
}

// Opens the server on the specified port
void NetworkServer::openServer()
{
    acceptor.open(tcp::v4());
    acceptor.bind(tcp::endpoint(tcp::v4(), port));
    acceptor.listen();
    std::cout << "Server started at 127.0.0.1:" << port << std::endl;
}

// Accepts incoming connections and handles them async
void NetworkServer::acceptConnections()
{
    acceptor.async_accept(io_context, [this](asio::error_code ec, tcp::socket socket) {
        if (!ec) {
            handleClient(std::move(socket));
            std::cout << "Client connected" << std::endl;
        }
        else {
            std::cerr << "Error accepting connection: " << ec.message() << std::endl;
        }

        acceptConnections();
    });
}

// Handles communication with a connected client
void NetworkServer::handleClient(tcp::socket socket)
{
    auto sharedSocket = std::make_shared<tcp::socket>(std::move(socket));
    auto buffer = std::make_shared<asio::streambuf>();

    asio::async_read_until(*sharedSocket, *buffer, "\n", [this, sharedSocket, buffer](asio::error_code ec, std::size_t bytes_transferred) {
        if (!ec && bytes_transferred > 0) {
            std::istream is(buffer.get());
            std::string message;
            std::getline(is, message);

            auto response = std::make_shared<std::string>(executeQuery(message));
            asio::async_write(*sharedSocket, asio::buffer(*response), [sharedSocket, response](asio::error_code ec, std::size_t) {
                if (ec)
                    std::cerr << "Error sending response: " << ec.message() << std::endl;
            });
        }
        else {
            std::cerr << "Error reading from client: " << ec.message() << std::endl;
        }
    });
}

std::string NetworkServer::executeQuery(std::string &query)
{
    try {
        std::vector<Row> rows = engine.query(query);

        if (rows.empty())
            return "OK\n";

        std::string result;
        for (const Row &row : rows) {
            for (size_t i = 0; i < row.values.size(); i++) {
                if (i > 0) result += "|";
                result += row.values[i];
            }
            result += "\n";
        }
        return result;
    }
    catch (const std::exception &e) {
        return std::string("ERROR: ") + e.what() + "\n";
    }
}



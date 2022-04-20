#include <string>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <cstdlib>
#include <thread>
#include <iostream>
#include "MetaData.pb.h"

// simply parsing data
void parsingData(const std::string& str)
{
    bolean::ipc::MetaData md;
    md.ParseFromString(str);
    std::cout << md.DebugString();
}

int main()
{
    std::string ip = "127.0.0.1";
    std::string target = "8083";

    const auto address = boost::asio::ip::make_address(ip);
    const auto port = static_cast<unsigned short>(std::atoi(target.c_str()));

    boost::asio::io_context ioc{ 1 };

    boost::asio::ip::tcp::endpoint ep{ address, port };
    boost::asio::ip::tcp::acceptor acceptor{ ioc, ep ,false};

    while (true)
    {
        // receive the new connection
        boost::asio::ip::tcp::socket socket{ ioc };
        // block until we get a connection
        acceptor.accept(socket);
        std::cout << "new connection comes in.. \n";
        // launch session
        std::thread(
            [s = std::move(socket)]() mutable
        {
            try
            {
                boost::beast::websocket::stream<boost::asio::ip::tcp::socket> ws{ std::move(s) };
                ws.accept();
                while (true)
                {
                    boost::beast::flat_buffer buffer;
                    ws.read(buffer);
                    auto decode = boost::beast::buffers_to_string(buffer.data());
                    parsingData(decode);
                    // echo message
                    ws.write(buffer.data());
                }
            }
            catch (boost::system::system_error const& se)
            {
                if (se.code() == boost::beast::websocket::error::closed)
                    std::cout << "Closed: " << se.code().message() << std::endl;
            }
            catch (std::exception& e)
            {
                std::cout << e.what() << std::endl;
            }
        }
        ).detach();
    }


}

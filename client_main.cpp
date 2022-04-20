#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include "SessionHandler.hpp"
#include "HeartBeatSession.hpp"

int main()
{

    boost::asio::io_context ioc;
    std::make_shared<HeartBeatSession<SessionHandler>>(ioc, SessionHandler::Get())->Run("localhost", "8083");
    ioc.run();

    //BENCHMARK_END;
}
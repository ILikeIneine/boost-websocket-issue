#include <iostream>
#include <vector>
#include <string>
#include <cassert>
//#include "SessionHandler.hpp"
#include "HeartBeatSession.hpp"

struct SessionHandler {
    static SessionHandler& Get() {
        static SessionHandler s_instance;
        return s_instance;
    }
    void Run() {}

    using Msg = std::string;
    Msg MsgGenerator() const {
        static int unsigned i = 0;
        return "Session Message #" + std::to_string(++i);
    }
};

int main()
{
    boost::asio::io_context ioc;
    std::make_shared<HeartBeatSession<SessionHandler>>(ioc,
                                                       SessionHandler::Get())
        ->Run("localhost", "8083");
    ioc.run();

    //BENCHMARK_END;
}

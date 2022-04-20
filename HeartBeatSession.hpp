#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <iostream>
#include <thread>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <memory>
#include <future>
#include <type_traits>
#include "StepSetter.hpp"
#include <boost/beast/websocket/impl/stream_impl.hpp>

// Detection idioms 
template<class T, class = void>
struct valid_controller : std::false_type {};

template<class T>
struct valid_controller <T, std::void_t< 
    decltype(std::declval<T>().MsgGenerator()) > > : std::true_type {};

template<class T>
constexpr bool valid_controller_v = valid_controller<T>::value;
/********************************************************************/


namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

template<typename T >
class HeartBeatSession : public std::enable_shared_from_this<HeartBeatSession<T> > 
{
    static_assert(valid_controller_v<T>, 
        "T does not contain a member function for seizing message");
public:
    explicit  HeartBeatSession(net::io_context& ioc, T& owner, int interval = 2);
    ~HeartBeatSession();
    void Run(std::string host, std::string port);
    void AsyncConnect();
    void AsyncWrite();
    void AsyncRead();
    void TryReconnect();

private:
    void OnHandshake();
    void OnResolve();
    void OnConnect(tcp::resolver::results_type::endpoint_type ep);
    void OnHeartBeating();

    int CurrentSpan();
    void ResetTimer();


    T& owner_;
    std::string host_;
    std::string port_;
    tcp::resolver::results_type hostSolvingResults_;
    StepSetter ss_;

    tcp::resolver resolver_;
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;

    std::chrono::seconds heartbeatInterval_;
};

template<typename T>
HeartBeatSession<T>::HeartBeatSession(net::io_context& ioc, T& owner, const int interval)
    :owner_{ owner }, resolver_{ net::make_strand(ioc) }, ws_{ net::make_strand(ioc) }
{
    heartbeatInterval_ = std::chrono::seconds(interval);
}


template<typename T>
HeartBeatSession<T>::~HeartBeatSession()
{
}

template<typename T>
void HeartBeatSession<T>::Run(std::string host, std::string port)
{
    host_ = std::move(host);
    port_ = std::move(port);

    resolver_.async_resolve(host_, port_,
        [this, self{ this->shared_from_this() }](beast::error_code ec, tcp::resolver::results_type results)
        ->void
    {
        if (ec)
        {
            std::cout << "[Resolve]: error, " << ec.what() << std::endl;
            return;
        }

        hostSolvingResults_ = results;
        self->OnResolve();
    });

}


template <typename T>
void HeartBeatSession<T>::OnResolve()
{
    beast::get_lowest_layer(ws_).expires_never();
    AsyncConnect();
}


template <typename T>
void HeartBeatSession<T>::TryReconnect()
{
    std::cout << "session has been disconnected, trying to reconnect...\n";
    if(ws_.is_open())
    {
        ws_.close(websocket::close_code::normal);
    }
    
    // Exponential backoff to avoid peaking connections
    const auto this_step = CurrentSpan();
    
    std::cout << "next trial will start after :" << this_step << "ms \n";
    
    std::this_thread::sleep_for(std::chrono::milliseconds(this_step));

    AsyncConnect();
}

template <typename T>
int HeartBeatSession<T>::CurrentSpan()
{
    return ss_.current_span();
}

template <typename T>
void HeartBeatSession<T>::ResetTimer()
{
    ss_.reset();
}


template <typename T>
void HeartBeatSession<T>::AsyncConnect()
{
    beast::get_lowest_layer(ws_).async_connect(hostSolvingResults_,
        [self{ this->shared_from_this() }](beast::error_code ec, tcp::resolver::results_type::endpoint_type ep)
        ->void
    {
        if (ec)
        {
            std::cout << "[Connect]: error, " << ec.what() << std::endl;
            self->TryReconnect();
            return;
        }

        // successfully connected
        self->ResetTimer();
        self->OnConnect(ep);
    });
}


template <typename T> void HeartBeatSession<T>::OnConnect(tcp::resolver::results_type::endpoint_type ep)
{
    std::cout << "Session Connection Established...\n";

    beast::get_lowest_layer(ws_).expires_never();

    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req)->void
        {
            req.set(http::field::user_agent, std::string(BOOST_BEAST_VERSION_STRING) +
                "async client websocket");
        }));

    auto host = host_ + ':' + std::to_string(ep.port());

    ws_.async_handshake(host, "/",
        [self{ this->shared_from_this() }](beast::error_code ec)
        ->void
    {
        if (ec)
        {
            std::cout << "[Handshake]: error, " << ec.what() << std::endl;
            return;
        }
        self->OnHandshake();
    });
}


template<typename T>
void HeartBeatSession<T>::OnHandshake()
{
    // todo: operations may add before heartbeating, just do it here
    std::cout << "Handshake Finished... \nStart HeartBeating...\n";
    this->OnHeartBeating();
}



template<typename T>
void HeartBeatSession<T>::OnHeartBeating()
{
    AsyncWrite();
    AsyncRead();
}


template <typename T>
void HeartBeatSession<T>::AsyncWrite()
{

    // async prepare protobuf message
    std::future<std::string> future = std::async(std::launch::async, [this]
        {
			// get a protobuf string
            return owner_.MsgGenerator();
        });

    std::this_thread::sleep_for(std::chrono::seconds(heartbeatInterval_));

    // fetch the string above
    std::string msgstr = future.get();

	/********** I'm not sure if here is the problem ************/
    ws_.async_write(net::buffer(msgstr),
        [this, self{ this->shared_from_this() }](beast::error_code ec, std::size_t byteTransferred)
        ->void
    {
        if (ec)
        {
            std::cout << "[Write]: error, " << ec.what() << std::endl;
            return;
        }
        AsyncWrite();
    });
	/************************************************************/
}

template<typename T>
void HeartBeatSession<T>::AsyncRead()
{
    buffer_.consume(buffer_.size());

	/********** I'm not sure if here is the problem ************/
    ws_.async_read(buffer_,
        [this, self{ this->shared_from_this() }](beast::error_code ec, std::size_t byteTransferred)
        ->void
    {
        if (ec)
        {
            std::cout << "[Read]: error, " << ec.what() << std::endl;
            TryReconnect();
            return;
        }
        std::cout << "Echo: " << beast::make_printable(buffer_.data()) << std::endl;
        AsyncRead();
    });
    /************************************************************/

}



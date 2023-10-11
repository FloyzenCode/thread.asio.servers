#include <iostream>
#include <thread>
#include <fstream>
#include <string_view>
#include <cassert>
// include boost
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/streambuf.hpp>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

const int port = 5000;

class Client
{
    class Sequence
    {
        template <typename Char, typename Traits>
        friend std::basic_ostream<Char, Traits> &operator<<(
            std::basic_ostream<Char, Traits> &os, Sequence &);

        std::size_t m_sequenceNo = 1;
    };

    template <typename Char, typename Traits>
    friend std::basic_ostream<Char, Traits> &operator<<(
        std::basic_ostream<Char, Traits> &os, Client::Sequence &);

public:
    Client(std::string_view prefix, std::string_view name);
    ~Client();

    Client(Client &) = delete;
    Client(Client &&) = default;

    Client &operator=(const Client &) = delete;
    Client &operator=(Client &&) = default;

    void putRecord(std::string_view record);

private:
    Sequence m_sequence;
    std::ofstream m_file;
};

namespace
{
    auto openFile(const std::string_view prefix, const std::string_view clientName)
        -> std::ofstream
    {
        const auto fileName =
            std::string{prefix} + "_" + std::string{clientName} + ".log";

        std::ofstream result{fileName, std::ios::app | std::ios::out};
        if (!result)
        {
            throw std::runtime_error{"Filed to open file"};
        }

        return result;
    }

}

template <typename Char, typename Traits>
std::basic_ostream<Char, Traits> &operator<<(
    std::basic_ostream<Char, Traits> &os, Client::Sequence &sequence)
{
    using namespace std::chrono;

    const auto now = system_clock::now();
    auto tp = now.time_since_epoch();
    const auto s = duration_cast<seconds>(tp);

    tp -= s;
    auto ms = duration_cast<milliseconds>(tp);

    const auto tt = system_clock::to_time_t(now);
    const std::tm &tm = *std::localtime(&tt);

    os << std::setw(5) << std::setfill(' ') << sequence.m_sequenceNo << ". ";

    os << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    os << '.' << std::setw(3) << std::setfill('0') << ms.count();
    os << " ";

    ++sequence.m_sequenceNo;

    return os;
}

Client::Client(const std::string_view prefix, const std::string_view name)
    : m_file{openFile(prefix, name)}
{
    m_file << m_sequence << "---=== Client \"" << name << "\" stated ===---"
           << std::endl;
}

Client::~Client()
{
    try
    {
        m_file << m_sequence << "^^^ ^^^ ^^^ Client stopped ^^^ ^^^ ^^^"
               << std::endl;
    }
    catch (...)
    {
        assert(false);
    }
}

void Client::putRecord(std::string_view record)
{
    m_file << m_sequence << boost::algorithm::trim_copy(record) << std::endl;
}

namespace
{

    asio::awaitable<void> workWithClient(tcp::socket socket)
    {
        asio::streambuf buffer;
        co_await asio::async_read_until(socket, buffer, "\n", asio::use_awaitable);

        std::string name{asio::buffer_cast<const char *>(buffer.data()),
                         buffer.size()};
        boost::trim(name);

        std::cout << std::this_thread::get_id() << " Client \"" << name
                  << "\": Connected." << std::endl;

        Client client("co", name);

        for (;;)
        {
            buffer.consume(buffer.size());

            try
            {
                co_await asio::async_read_until(socket, buffer, "\n",
                                                asio::use_awaitable);
            }
            catch (const boost::system::system_error &ex)
            {
                if (ex.code() != asio::error::eof)
                {
                    throw;
                }
                break;
            }

            const std::string_view logRecord{
                asio::buffer_cast<const char *>(buffer.data()), buffer.size()};

            std::cout << std::this_thread::get_id() << " Client \"" << name
                      << "\": New log record received." << std::endl;

            client.putRecord(logRecord);
        }

        std::cout << std::this_thread::get_id() << " Client \"" << name
                  << "\": Disconnected." << std::endl;
    }

    asio::awaitable<void> listen()
    {
        const auto executor = co_await asio::this_coro::executor;

        std::cout << std::this_thread::get_id() << " Listing TCP v4 port " << port
                  << " for new log clients..." << std::endl;
        tcp::acceptor acceptor{executor, {tcp::v4(), port}};

        for (;;)
        {
            auto socket = co_await acceptor.async_accept(asio::use_awaitable);
            std::cout << std::this_thread::get_id() << " Accepting new connection..."
                      << std::endl;

            asio::co_spawn(executor, workWithClient(std::move(socket)), asio::detached);
        }
    }

    void runServer()
    {
        std::cout << std::this_thread::get_id() << " Running server..." << std::endl;

        asio::io_context ioContext;

        asio::signal_set signals{ioContext, SIGINT, SIGTERM};
        signals.async_wait([&](auto, auto)
                           { ioContext.stop(); });

        asio::co_spawn(ioContext, listen, asio::detached);

        ioContext.run();

        std::cout << std::this_thread::get_id() << " Server stopped." << std::endl;
    }

}

auto main() -> int
{
    try
    {
        runServer();
        return EXIT_SUCCESS;
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Fatal error \"" << ex.what() << "\"." << std::endl;
    }
    catch (...)
    {
        std::cerr << "Fatal UNKNOWN error." << std::endl;
    }

    return EXIT_FAILURE;
}
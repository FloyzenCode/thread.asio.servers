#include <iostream>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket) : socket_(std::move(socket)) {}

    void start()
    {
        do_read(); // начинаем чтение сообщений в сокете
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        socket_.async_read_some(boost::asio::buffer(data_, max_length),
                                [this, self](boost::system::error_code ec, std::size_t length)
                                {
                                    if (!ec)
                                    {
                                        std::cout << "Received message: " << data_ << std::endl;
                                        do_read(); // продолжаем чтение
                                    }
                                });
    }

    tcp::socket socket_;
    enum
    {
        max_length = 1024
    }; // максимальный размер сообщения
    char data_[max_length];
};

class Server
{
public:
    Server(boost::asio::io_context &io_context, short port)
        : io_context_(io_context), acceptor_(io_context, tcp::endpoint(tcp::v4(), port))
    {
        do_accept();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](boost::system::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    std::make_shared<Session>(std::move(socket))->start(); // создаем новую сессию
                }

                do_accept(); // ожидаем новое соединение
            });
    }

    boost::asio::io_context &io_context_;
    tcp::acceptor acceptor_;
};

int main()
{
    try
    {
        boost::asio::io_context io_context;
        Server server(io_context, 5000); // создаем сервер, слушающий порт 12345
        io_context.run();
        std::cout << "Server run http://localhost:5000/" << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
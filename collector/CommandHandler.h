#ifndef __COMMANDHANDLER_H__
#define __COMMANDHANDLER_H__

#include <set>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include "EmsMessage.h"
#include "TcpHandler.h"

class CommandHandler;

class CommandConnection : public boost::enable_shared_from_this<CommandConnection>,
			  private boost::noncopyable
{
    public:
	typedef boost::shared_ptr<CommandConnection> Ptr;

    public:
	CommandConnection(CommandHandler& handler);

    public:
	boost::asio::ip::tcp::socket& socket() {
	    return m_socket;
	}
	void startRead() {
	    boost::asio::async_read_until(m_socket, m_request, "\n",
		boost::bind(&CommandConnection::handleRequest, shared_from_this(),
			    boost::asio::placeholders::error));
	}
	void close() {
	    m_socket.close();
	}
	void handlePcMessage(const EmsMessage& message);

    private:
	void handleRequest(const boost::system::error_code& error);
	void handleWrite(const boost::system::error_code& error);

	typedef enum {
	    Ok,
	    InvalidCmd,
	    InvalidArgs
	} CommandResult;

	CommandResult handleCommand(std::istream& request);
	CommandResult handleGetErrorsCommand(unsigned int offset);
	CommandResult handleHkCommand(std::istream& request, uint8_t base);
	CommandResult handleHkTemperatureCommand(std::istream& request, uint8_t base, uint8_t cmd);
	CommandResult handleWwCommand(std::istream& request);
	CommandResult handleThermDesinfectCommand(std::istream& request);
	CommandResult handleZirkPumpCommand(std::istream& request);

	std::string buildErrorMessageResponse(const EmsMessage::ErrorRecord *record);

	void respond(const std::string& response) {
	    boost::asio::async_write(m_socket, boost::asio::buffer(response + "\n"),
		boost::bind(&CommandConnection::handleWrite, shared_from_this(),
			    boost::asio::placeholders::error));
	}
	void scheduleResponseTimeout();
	void responseTimeout(const boost::system::error_code& error);
	void sendCommand(uint8_t dest, uint8_t type,
			 const uint8_t *data, size_t count,
			 bool expectResponse = false);

    private:
	boost::asio::ip::tcp::socket m_socket;
	boost::asio::streambuf m_request;
	CommandHandler& m_handler;
	bool m_waitingForResponse;
	boost::asio::deadline_timer m_responseTimeout;
	unsigned int m_responseCounter;
};

class CommandHandler : private boost::noncopyable
{
    public:
	CommandHandler(TcpHandler& handler,
		       boost::asio::ip::tcp::endpoint& endpoint);
	~CommandHandler();

    public:
	void startConnection(CommandConnection::Ptr connection);
	void stopConnection(CommandConnection::Ptr connection);
	void handlePcMessage(const EmsMessage& message);
	TcpHandler& getHandler() const {
	    return m_handler;
	}

    private:
	void handleAccept(CommandConnection::Ptr connection,
			  const boost::system::error_code& error);
	void startAccepting();

    private:
	TcpHandler& m_handler;
	boost::asio::ip::tcp::acceptor m_acceptor;
	std::set<CommandConnection::Ptr> m_connections;
};

#endif /* __COMMANDHANDLER_H__ */
#include "RequestHandler.h"
#include "Session.h"

std::string GetHandler::buildHeader(int filefd, const std::string& content_type, long& file_len) {
    file_len = (long)lseek(filefd, (off_t)0, SEEK_END);
    if(file_len <= 0) {
        return "";
    }
    lseek(filefd, 0, SEEK_SET);
    
    std::string header = "HTTP/1.1 200 OK\r\nContent-Type: " + content_type + "\r\n"
                            "Content-Length: " + std::to_string(file_len) + "\r\n"
                            "Connection: close\r\n\r\n";
    return header;
}

asio::awaitable<void> GetHandler::sendResource(int filefd, long file_len) {
    auto this_session = session.lock();
    if(!this_session) {
        ERROR("Get Handler", 0, "NULL", "session observer is null");
        co_return;
    }
    
    asio::posix::stream_descriptor file_desc(co_await asio::this_coro::executor, filefd);
    
    TransferState state{.total_bytes = file_len};
    while (state.bytes_sent < file_len) {
        std::size_t bytes_to_read = std::min(buffer.size(), static_cast<std::size_t>(file_len - state.bytes_sent));
        std::size_t bytes_to_write = co_await file_desc.async_read_some(asio::buffer(buffer.data(), bytes_to_read), asio::use_awaitable);
        if (bytes_to_write == 0) {
            LOG("WARN", "Get Handler", "EOF reached prematurely while sending resource");
            break;
        }

        state.retry_count = 1;
        std::size_t total_bytes_written = 0;
        while (total_bytes_written < bytes_to_write) {
            auto [ec, bytes_written] = co_await sock->co_write(buffer.data() + total_bytes_written, bytes_to_write - total_bytes_written);
            if (ec == asio::error::connection_reset || ec == asio::error::broken_pipe || ec == asio::error::eof) {
                this_session->onError(http::error(http::code::Client_Closed_Request, "Connection reset by client"));
                close(filefd);
                co_return;
            }

            if (ec && state.retry_count <= TransferState::MAX_RETRIES) {
                LOG("WARN", "Get Handler", "Retry %d of %d for sending resource", state.retry_count, TransferState::MAX_RETRIES);
                state.retry_count++;

                asio::steady_timer timer(co_await asio::this_coro::executor);
                timer.expires_after(asio::chrono::milliseconds(TransferState::RETRY_DELAY * state.retry_count));
                co_await timer.async_wait(asio::use_awaitable);
                continue;
            }
            total_bytes_written += bytes_written;
            state.retry_count = 0;
        }

        state.bytes_sent += total_bytes_written;
    }
    
    close(filefd);
    LOG("INFO", "Get Handler", "Finished sending file, file size: %ld, bytes sent: %ld", file_len, state.bytes_sent);
    this_session->onCompletion(response_header, state.bytes_sent);
}

asio::awaitable<void> GetHandler::handle() {
    auto this_session = session.lock();
    if(!this_session) {
        ERROR("Get Handler", 0, "NULL", "session observer is null");
        co_return;
    }

    http::code code;
    LOG("INFO", "Get Handler", "REQUEST: %s", buffer.data());
    http::clean_buffer(buffer);
    std::string resource, content_type;
    if((code = http::extract_endpoint(buffer, resource)) != http::code::OK || (code = http::determine_content_type(resource, content_type)) != http::code::OK) {
        this_session->onError(http::error(code));
        co_return;
    }
    
    auto config = cfg::Config::getInstance(); 
    auto route = config->findRoute(resource);
    if(route && route->is_protected) {
        // authenticate the user
    }
    else {
        resource = "public/" + resource;
    }

    LOG("INFO", "Get Handler", "PARSED RESULTS\nResource: %s\nContent_Type: %s", resource.c_str(), content_type.c_str());
    
    int filefd =  open(resource.c_str(), O_RDONLY | O_NONBLOCK);
    if(filefd == -1) {
        this_session->onError(http::error(http::code::Not_Found, std::format("Failed to open resource: {}, errno={} ({})", resource.c_str(), errno, strerror(errno))));
        co_return;
    }
    
    long file_len;
    response_header = buildHeader(filefd, content_type, file_len);
    if(file_len < 0) {
        this_session->onError(http::error(http::code::Not_Found, "404 File not found: " + resource));
        close(filefd);
        co_return;
    }

    asio::error_code error;
    TransferState state;
    while(state.retry_count < TransferState::MAX_RETRIES) {
        auto [error, bytes_written] = co_await sock->co_write(response_header.data(), response_header.length());
        
        if (!error) {
            co_await sendResource(filefd, file_len);
            co_return;
        }

        state.retry_count++;
        asio::steady_timer timer = asio::steady_timer(co_await asio::this_coro::executor, TransferState::RETRY_DELAY * state.retry_count);
        co_await timer.async_wait(asio::use_awaitable);
    }
    this_session->onError(http::error(http::code::Internal_Server_Error, std::format("MAX_RETRIES reached sending {} to {} with header {}\nERROR INFO: error={} ({})", resource, sock->getIP(), response_header, error.value(), error.message())));
}

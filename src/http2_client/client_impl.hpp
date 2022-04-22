#include <atomic>
#include <boost/asio.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>

#pragma once

#include "client.hpp"
#include "client_utils.hpp"
#include "connection.hpp"
#include "script_queue.hpp"

namespace stats
{
class stats_if;
}

namespace http2_client
{
class client_impl : public client
{
public:
    client_impl(std::shared_ptr<stats::stats_if> stats, boost::asio::io_context& io_ctx,
                std::unique_ptr<traffic::script_queue_if> q, const std::string& h,
                const std::string& p, const bool secure_session = false,
                const int the_number_of_connections = 1);

    ~client_impl() final = default;

    void send() override;
    bool has_finished() const override { return !queue->has_pending_scripts(); };
    void close_window() override { queue->close_window(); };
    bool is_connected() const override
    {
        for (int i = 0; i < number_of_conns; i++)
        {
            if (!is_connected(i))
            {
                return false;
            }
        }
        return true;
    };

private:
    void handle_timeout(const std::shared_ptr<race_control>& control,
                        const std::string& msg_name) const;
    void handle_timeout_cancelled(const std::shared_ptr<race_control>& control,
                                  const std::string& msg_name) const;
    void on_timeout(const boost::system::error_code& e, std::shared_ptr<race_control> control,
                    const std::string& msg_name) const;

    void open_new_connection(int conn_index);
    int get_next_connection_index();
    bool is_connected(int conn_index) const;

    std::shared_ptr<stats::stats_if> stats;
    boost::asio::io_context& io_ctx;
    std::unique_ptr<traffic::script_queue_if> queue;
    std::string host;
    std::string port;
    bool secure_session;
    std::shared_timed_mutex mtx;

    std::vector<std::unique_ptr<connection>> conns;
    std::atomic<int> connection_round_robin_counter{0};
    int number_of_conns;
};

}  // namespace http2_client

#include <boost/asio/ip/udp.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace novemus::wormhole {

typedef boost::asio::ip::udp::endpoint udp_endpoint;
typedef boost::asio::ip::tcp::endpoint tcp_endpoint;

struct router
{
    virtual ~router() {}
    virtual void employ() noexcept(true) = 0;
    virtual void launch() noexcept(true) = 0;
    virtual void cancel() noexcept(true) = 0;
};

typedef std::shared_ptr<router> router_ptr;

router_ptr create_exporter(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(true);
router_ptr create_importer(const tcp_endpoint& server, const udp_endpoint& gateway, const udp_endpoint& faraway, uint64_t secret) noexcept(true);

}

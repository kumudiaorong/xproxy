#include "api/inbound.h"
#include "api/inbound/http.h"
#include "api/outbound.h"
#include "api/outbound/nconn.h"

#include <xsl/coro/task.h>
#include <xsl/logctl.h>

#include <memory>
using namespace xsl;

coro::Task<void> run(std::unique_ptr<Inbound> inbound, std::unique_ptr<Outbound> outbound) {
  while (true) {
    InConnection in;
    auto [size, err] = co_await inbound->read(std::span{&in, 1});
    if (err) {
      co_return;
    }
    WARN("Received connection request to {}:{}", in.host, in.port);
    OutConnection out;
    out.ip = in.host;
    out.port = in.port;
    auto [o_size, o_err] = co_await outbound->read(std::span{&out, 1});
    if (o_err) {
      co_return;
    }
    WARN("Connected to {}:{}", out.ip, out.port);

    [](auto in, auto out) -> coro::Task<void> {
      INFO("Start copy data from client to server");
      co_await in->write(*out);
      INFO("Finish copy data from client to server");
    }(std::move(in.in), std::move(out.out))
                                 .detach();
    [](auto in, auto out) -> coro::Task<void> {
      INFO("Start copy data from server to client");
      co_await io::splice(std::move(in), std::move(out), std::string(4096, '\0'));
      INFO("Finish copy data from server to client");
    }(std::move(out.in), std::move(in.out))
                                 .detach();
  }
}

int main() {
  xsl::set_log_level(xsl::LogLevel::LOG5);
  auto poller = std::make_shared<sync::Poller>();

  auto inbound = HttpInbound::make_unique("127.0.0.1", "8080", poller).value();
  auto outbound = std::make_unique<Nconn>("127.0.0.1", "8081", poller);

  run(std::move(inbound), std::move(outbound)).detach();
  while (true) {
    poller->poll();
  }
  return 0;
}

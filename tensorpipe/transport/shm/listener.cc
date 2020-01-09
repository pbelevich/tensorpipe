#include <tensorpipe/transport/shm/listener.h>

#include <tensorpipe/common/defs.h>

namespace tensorpipe {
namespace transport {
namespace shm {

Listener::Listener(std::shared_ptr<Loop> loop, const Sockaddr& addr)
    : loop_(std::move(loop)), listener_(Socket::createForFamily(AF_UNIX)) {
  // Bind socket to abstract socket address.
  listener_->bind(addr);
  listener_->block(false);
  listener_->listen(128);
}

Listener::~Listener() {
  if (fn_.has_value()) {
    loop_->unregisterDescriptor(listener_->fd());
  }
}

void Listener::accept(accept_callback_fn fn) {
  fn_.emplace(std::move(fn));

  // Register with loop for readability events.
  loop_->registerDescriptor(listener_->fd(), EPOLLIN, this);
}

void Listener::handleEvents(int events) {
  TP_ARG_CHECK_EQ(events, EPOLLIN);

  if (!fn_.has_value()) {
    return;
  }

  auto socket = listener_->accept();
  if (!socket) {
    return;
  }

  auto fn = std::move(fn_).value();
  loop_->unregisterDescriptor(listener_->fd());
  fn_.reset();
  fn(std::make_shared<Connection>(loop_, socket));
}

} // namespace shm
} // namespace transport
} // namespace tensorpipe

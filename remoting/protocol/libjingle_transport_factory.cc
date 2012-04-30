// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/protocol/libjingle_transport_factory.h"

#include "base/message_loop_proxy.h"
#include "jingle/glue/channel_socket_adapter.h"
#include "jingle/glue/pseudotcp_adapter.h"
#include "jingle/glue/utils.h"
#include "net/base/net_errors.h"
#include "remoting/base/constants.h"
#include "remoting/protocol/channel_authenticator.h"
#include "remoting/protocol/transport_config.h"
#include "third_party/libjingle/source/talk/base/basicpacketsocketfactory.h"
#include "third_party/libjingle/source/talk/base/network.h"
#include "third_party/libjingle/source/talk/p2p/base/p2ptransportchannel.h"
#include "third_party/libjingle/source/talk/p2p/client/basicportallocator.h"

namespace remoting {
namespace protocol {

namespace {

// Value is choosen to balance the extra latency against the reduced
// load due to ACK traffic.
const int kTcpAckDelayMilliseconds = 10;

// Values for the TCP send and receive buffer size. This should be tuned to
// accomodate high latency network but not backlog the decoding pipeline.
const int kTcpReceiveBufferSize = 256 * 1024;
const int kTcpSendBufferSize = kTcpReceiveBufferSize + 30 * 1024;

class LibjingleStreamTransport : public StreamTransport,
                                 public sigslot::has_slots<> {
 public:
  LibjingleStreamTransport(cricket::PortAllocator* port_allocator,
                           bool incoming_only);
  virtual ~LibjingleStreamTransport();

  // StreamTransport interface.
  virtual void Initialize(
      const std::string& name,
      const TransportConfig& config,
      Transport::EventHandler* event_handler,
      scoped_ptr<ChannelAuthenticator> authenticator) OVERRIDE;
  virtual void Connect(
      const StreamTransport::ConnectedCallback& callback) OVERRIDE;
  virtual void AddRemoteCandidate(const cricket::Candidate& candidate) OVERRIDE;
  virtual const std::string& name() const OVERRIDE;
  virtual bool is_connected() const OVERRIDE;

 private:
  void OnRequestSignaling(cricket::TransportChannelImpl* channel);
  void OnCandidateReady(cricket::TransportChannelImpl* channel,
                        const cricket::Candidate& candidate);
  void OnRouteChange(cricket::TransportChannel* channel,
                     const cricket::Candidate& candidate);

  void OnTcpConnected(int result);
  void OnAuthenticationDone(net::Error error,
                            scoped_ptr<net::StreamSocket> socket);

  void OnChannelDestroyed();

  void NotifyConnected(scoped_ptr<net::StreamSocket> socket);
  void NotifyConnectFailed();

  cricket::PortAllocator* port_allocator_;
  bool incoming_only_;

  std::string name_;
  TransportConfig config_;
  EventHandler* event_handler_;
  StreamTransport::ConnectedCallback callback_;
  scoped_ptr<ChannelAuthenticator> authenticator_;

  scoped_ptr<cricket::P2PTransportChannel> channel_;

  // We own |socket_| until it is connected.
  scoped_ptr<jingle_glue::PseudoTcpAdapter> socket_;

  DISALLOW_COPY_AND_ASSIGN(LibjingleStreamTransport);
};

LibjingleStreamTransport::LibjingleStreamTransport(
    cricket::PortAllocator* port_allocator,
    bool incoming_only)
    : port_allocator_(port_allocator),
      incoming_only_(incoming_only),
      event_handler_(NULL) {
}

LibjingleStreamTransport::~LibjingleStreamTransport() {
  DCHECK(event_handler_);
  event_handler_->OnTransportDeleted(this);
  // Channel should be already destroyed if we were connected.
  DCHECK(!is_connected() || socket_.get() == NULL);

  if (channel_.get()) {
    base::MessageLoopProxy::current()->DeleteSoon(
        FROM_HERE, channel_.release());
  }
}

void LibjingleStreamTransport::Initialize(
    const std::string& name,
    const TransportConfig& config,
    Transport::EventHandler* event_handler,
    scoped_ptr<ChannelAuthenticator> authenticator) {
  DCHECK(CalledOnValidThread());

  DCHECK(!name.empty());
  DCHECK(event_handler);

  // Can be initialized only once.
  DCHECK(name_.empty());

  name_ = name;
  config_ = config;
  event_handler_ = event_handler;
  authenticator_ = authenticator.Pass();
}

void LibjingleStreamTransport::Connect(
    const StreamTransport::ConnectedCallback& callback) {
  DCHECK(CalledOnValidThread());

  callback_ = callback;

  DCHECK(!channel_.get());

  // Create P2PTransportChannel, attach signal handlers and connect it.
  // TODO(sergeyu): Specify correct component ID for the channel.
  channel_.reset(new cricket::P2PTransportChannel(
      name_, 0, NULL, port_allocator_));
  channel_->SignalRequestSignaling.connect(
      this, &LibjingleStreamTransport::OnRequestSignaling);
  channel_->SignalCandidateReady.connect(
      this, &LibjingleStreamTransport::OnCandidateReady);
  channel_->SignalRouteChange.connect(
      this, &LibjingleStreamTransport::OnRouteChange);
  channel_->set_incoming_only(incoming_only_);

  channel_->Connect();

  // Create net::Socket adapter for the P2PTransportChannel.
  scoped_ptr<jingle_glue::TransportChannelSocketAdapter> channel_adapter(
      new jingle_glue::TransportChannelSocketAdapter(channel_.get()));

  channel_adapter->SetOnDestroyedCallback(base::Bind(
      &LibjingleStreamTransport::OnChannelDestroyed, base::Unretained(this)));

  // Configure and connect PseudoTCP adapter.
  socket_.reset(
      new jingle_glue::PseudoTcpAdapter(channel_adapter.release()));
  socket_->SetSendBufferSize(kTcpSendBufferSize);
  socket_->SetReceiveBufferSize(kTcpReceiveBufferSize);
  socket_->SetNoDelay(true);
  socket_->SetAckDelay(kTcpAckDelayMilliseconds);

  // TODO(sergeyu): This is a hack to improve latency of the video
  // channel. Consider removing it once we have better flow control
  // implemented.
  if (name_ == kVideoChannelName)
    socket_->SetWriteWaitsForSend(true);

  int result = socket_->Connect(
      base::Bind(&LibjingleStreamTransport::OnTcpConnected,
                 base::Unretained(this)));
  if (result != net::ERR_IO_PENDING)
    OnTcpConnected(result);
}

void LibjingleStreamTransport::AddRemoteCandidate(
    const cricket::Candidate& candidate) {
  DCHECK(CalledOnValidThread());
  channel_->OnCandidate(candidate);
}

const std::string& LibjingleStreamTransport::name() const {
  DCHECK(CalledOnValidThread());
  return name_;
}

bool LibjingleStreamTransport::is_connected() const {
  DCHECK(CalledOnValidThread());
  return callback_.is_null();
}

void LibjingleStreamTransport::OnRequestSignaling(
    cricket::TransportChannelImpl* channel) {
  DCHECK(CalledOnValidThread());
  channel_->OnSignalingReady();
}

void LibjingleStreamTransport::OnCandidateReady(
    cricket::TransportChannelImpl* channel,
    const cricket::Candidate& candidate) {
  DCHECK(CalledOnValidThread());
  event_handler_->OnTransportCandidate(this, candidate);
}

void LibjingleStreamTransport::OnRouteChange(
    cricket::TransportChannel* channel,
    const cricket::Candidate& candidate) {
  TransportRoute route;

  if (candidate.type() == "local") {
    route.type = TransportRoute::DIRECT;
  } else if (candidate.type() == "stun") {
    route.type = TransportRoute::STUN;
  } else if (candidate.type() == "relay") {
    route.type = TransportRoute::RELAY;
  } else {
    LOG(FATAL) << "Unknown candidate type: " << candidate.type();
  }

  if (!jingle_glue::SocketAddressToIPEndPoint(
          candidate.address(), &route.remote_address)) {
    LOG(FATAL) << "Failed to convert peer IP address.";
  }

  DCHECK(channel_->best_connection());
  const cricket::Candidate& local_candidate =
      channel_->best_connection()->local_candidate();
  if (!jingle_glue::SocketAddressToIPEndPoint(
          local_candidate.address(), &route.local_address)) {
    LOG(FATAL) << "Failed to convert local IP address.";
  }

  event_handler_->OnTransportRouteChange(this, route);
}

void LibjingleStreamTransport::OnTcpConnected(int result) {
  DCHECK(CalledOnValidThread());

  if (result != net::OK) {
    NotifyConnectFailed();
    return;
  }

  authenticator_->SecureAndAuthenticate(
      socket_.PassAs<net::StreamSocket>(),
      base::Bind(&LibjingleStreamTransport::OnAuthenticationDone,
                 base::Unretained(this)));
}

void LibjingleStreamTransport::OnAuthenticationDone(
    net::Error error,
    scoped_ptr<net::StreamSocket> socket) {
  if (error != net::OK) {
    NotifyConnectFailed();
    return;
  }

  NotifyConnected(socket.Pass());
}

void LibjingleStreamTransport::OnChannelDestroyed() {
  if (is_connected()) {
    // The connection socket is being deleted, so delete the transport too.
    delete this;
  }
}

void LibjingleStreamTransport::NotifyConnected(
    scoped_ptr<net::StreamSocket> socket) {
  DCHECK(!is_connected());
  StreamTransport::ConnectedCallback callback = callback_;
  callback_.Reset();
  callback.Run(socket.Pass());
}

void LibjingleStreamTransport::NotifyConnectFailed() {
  DCHECK(!is_connected());

  socket_.reset();

  // This method may be called in response to a libjingle signal, so
  // libjingle objects must be deleted asynchronously.
  if (channel_.get()) {
    base::MessageLoopProxy::current()->DeleteSoon(
        FROM_HERE, channel_.release());
  }

  authenticator_.reset();

  NotifyConnected(scoped_ptr<net::StreamSocket>(NULL));
}

}  // namespace

LibjingleTransportFactory::LibjingleTransportFactory(
    scoped_ptr<talk_base::NetworkManager> network_manager,
    scoped_ptr<talk_base::PacketSocketFactory> socket_factory,
    scoped_ptr<cricket::PortAllocator> port_allocator,
    bool incoming_only)
    : network_manager_(network_manager.Pass()),
      socket_factory_(socket_factory.Pass()),
      port_allocator_(port_allocator.Pass()),
      incoming_only_(incoming_only) {
}

LibjingleTransportFactory::LibjingleTransportFactory()
    : network_manager_(new talk_base::BasicNetworkManager()),
      socket_factory_(new talk_base::BasicPacketSocketFactory()),
      port_allocator_(new cricket::BasicPortAllocator(
          network_manager_.get(), socket_factory_.get())),
      incoming_only_(false) {
}

LibjingleTransportFactory::~LibjingleTransportFactory() {
  // This method may be called in response to a libjingle signal, so
  // libjingle objects must be deleted asynchronously.
  base::MessageLoopProxy::current()->DeleteSoon(
      FROM_HERE, port_allocator_.release());
  base::MessageLoopProxy::current()->DeleteSoon(
      FROM_HERE, socket_factory_.release());
  base::MessageLoopProxy::current()->DeleteSoon(
      FROM_HERE, network_manager_.release());
}

scoped_ptr<StreamTransport> LibjingleTransportFactory::CreateStreamTransport() {
  return scoped_ptr<StreamTransport>(
      new LibjingleStreamTransport(port_allocator_.get(), incoming_only_));
}

scoped_ptr<DatagramTransport>
LibjingleTransportFactory::CreateDatagramTransport() {
  NOTIMPLEMENTED();
  return scoped_ptr<DatagramTransport>(NULL);
}

}  // namespace protocol
}  // namespace remoting

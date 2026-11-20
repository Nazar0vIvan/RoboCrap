#pragma once

#include "devices/abstractdevice.h"

#include <QVariant>
#include <QVariantMap>

#include <optional>

// Configured ports share this rule; socket binding and peer discovery do not.
inline std::optional<quint16> parseSocketPort(const QVariant& value)
{
  bool ok = false;
  const uint port = value.toUInt(&ok);
  if (!ok || port == 0 || port > 65535) return std::nullopt;
  return static_cast<quint16>(port);
}

// localAddress, localPort and peerAddress must be valid; a present peerPort must be valid too.
inline std::optional<SocketEndpoints> parseSocketEndpoints(const QVariantMap& config)
{
  const QHostAddress localAddress(config.value("localAddress").toString());
  const QHostAddress peerAddress(config.value("peerAddress").toString());
  const auto localPort = parseSocketPort(config.value("localPort"));
  if (localAddress.isNull() || peerAddress.isNull() || !localPort) return std::nullopt;
  if (!config.contains("peerPort")) return SocketEndpoints{localAddress, *localPort, peerAddress, std::nullopt};
  const auto peerPort = parseSocketPort(config.value("peerPort"));
  if (!peerPort) return std::nullopt;
  return SocketEndpoints{localAddress, *localPort, peerAddress, peerPort};
}

// The connectionConfig value published to QML; peerPort only when configured.
inline QVariantMap connectionConfigMap(const SocketEndpoints& endpoints)
{
  QVariantMap config{{"localAddress", endpoints.localAddress.toString()}, {"localPort", endpoints.localPort},
                     {"peerAddress", endpoints.peerAddress.toString()}};
  if (endpoints.peerPort) config.insert("peerPort", *endpoints.peerPort);
  return config;
}

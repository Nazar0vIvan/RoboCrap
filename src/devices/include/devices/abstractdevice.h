#pragma once

#include <QAbstractSocket>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QVariantHash>
#include <QVariantMap>
#include <QHash>
#include <QMetaMethod>

#include <optional>

// A socket device's configured endpoints (parsed by the library's parseSocketEndpoints).
struct SocketEndpoints
{
  QHostAddress localAddress;
  quint16 localPort = 0;
  QHostAddress peerAddress;
  std::optional<quint16> peerPort; // Absent when the peer port is learned (RSI).
};

class AbstractDevice : public QObject
{
  Q_OBJECT

public:
  explicit AbstractDevice(const QString& name, QObject* parent = nullptr);
  ~AbstractDevice() override = default;

signals:
  void stateReady(const QVariantHash& vals);
  void socketStateReady(QAbstractSocket::SocketState state);

public slots:
  void start();
  void stop();
  void invoke(const QString& method, const QVariantMap& args = {});

protected:
  virtual void startDevice() = 0;
  virtual void stopDevice() = 0;

  void attachSocket(QAbstractSocket* sock);

private:
  void buildApi();
  void onSocketStateChanged(QAbstractSocket::SocketState state);
  void logSocketError(QAbstractSocket* socket, QAbstractSocket::SocketError error);

  QHash<QString, QMetaMethod> m_api;
  bool m_apiReady = false;

  bool m_started = false;
};

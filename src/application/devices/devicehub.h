#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QThread>

class AbstractDevice;
class DeviceRunner;

// The only object QML needs in order to reach a device. QML asks for a runner
// by key; adding a device requires no new QML registration and no new context
// property.
//
// DeviceHub is the parent of every DeviceRunner it holds. AbstractDevice
// objects live in their group's I/O thread (ControlIO or GeneralIO) and are
// deleted there.
class DeviceHub : public QObject
{
  Q_OBJECT

public:
  enum class DeviceGroup {
    Control,
    General
  };

  explicit DeviceHub(QObject* parent = nullptr);

  ~DeviceHub() override;

  void add(const QString& key, AbstractDevice* dev, DeviceGroup group);

  Q_INVOKABLE DeviceRunner* device(const QString& key) const;

public slots:
  void startAll();
  void stopAll();

private:
  struct Entry {
    AbstractDevice* dev = nullptr;
    DeviceRunner* runner = nullptr;
    QThread* io = nullptr;
  };

  QThread m_controlIo;
  QThread m_generalIo;
  QHash<QString, Entry> m_devs;
};

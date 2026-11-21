#pragma once

#include <QObject>
#include <QUrl>

class SceneModel;
class SceneObject;

// Synchronous GUI-thread import into the Scene Abstraction Layer.
class SceneSurfaceImporter : public QObject
{
  Q_OBJECT

public:
  explicit SceneSurfaceImporter(QObject* parent = nullptr);
  Q_INVOKABLE void load(const QUrl& sourceUrl, SceneModel* scene);
  Q_INVOKABLE void loadCylinder(const QUrl& sourceUrl, SceneModel* scene);

signals:
  void loaded(SceneObject* object);
  void failed(const QString& message);

private:
  enum class Surface { Plane, Cylinder };
  void importSurface(const QUrl& sourceUrl, SceneModel* scene, Surface surface);
  bool m_busy = false; // Rejects an import started by a signal handler during an import.
};

#ifdef USE_FELGO_HOT_RELOAD
#include <FelgoApplication>
#include <FelgoHotReload>

#include <QDir>
#include <QStandardPaths>
#endif

#include <QApplication>

#include <QFontDatabase>
#include <QLocale>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlEngine>

#include "viewport/viewportqmltypes.h"
#include "assetpaths.h"
#include "robot/robotcontroller.h"

#include "backendqmltypes.h"
#include "workspacecontroller.h"
#include "devices/devicehub.h"
#include "records/sessionrecorder.h"

#include "devices/fts/rdtmessage.h"
#include "devices/fts/ftsdevice.h"
#include "devices/plc/plcdevice.h"
#include "devices/rsi/rsidevice.h"

#include <QDebug>

namespace {

void logViewportMessage(const QString& text, bool error)
{
  qInfo().noquote() << (error ? "[ERROR]" : "[INFO]") << "3D:" << text;
}

} // namespace

int main(int argc, char* argv[])
{
  QLocale::setDefault(QLocale::English);
  QApplication app(argc, argv);
  qSetMessagePattern(QStringLiteral("[%{time yyyy-MM-dd hh:mm:ss}] %{message}"));

#ifdef USE_FELGO_HOT_RELOAD
  FelgoApplication felgo;
#endif

  for (const char* path : {"://fonts/roboto/Roboto-Regular.ttf", "://fonts/roboto/Roboto-Medium.ttf",
                           "://fonts/roboto/Roboto-Bold.ttf"}) {
    if (QFontDatabase::addApplicationFont(path) == -1)
      qWarning() << "Failed to load font from resources:" << path;
  }

  qRegisterMetaType<RDTResponse>("RDTResponse");
  qRegisterMetaType<QVector<RDTResponse>>("QVector<RDTResponse>");
  qRegisterMetaType<QVector<QVector<V6d>>>("QVector<QVector<V6d>>");
  qRegisterMetaType<V6d>("V6d"); // RsiDevice::touchDetected crosses from ControlIO.
  qRegisterMetaType<RsiCapture>("RsiCapture");
  qRegisterMetaType<PlcIoRecord>("PlcIoRecord");

  auto* plcDev = new PlcDevice(QStringLiteral("PLC_AS332T"));
  auto* ftsDev = new FtsDevice(QStringLiteral("FTS_Delta"));
  auto* rsiDev = new RsiDevice(QStringLiteral("KRC4_RSI"));

  DeviceHub hub;

  hub.add(QStringLiteral("plc"), plcDev, DeviceHub::DeviceGroup::General);
  hub.add(QStringLiteral("fts"), ftsDev, DeviceHub::DeviceGroup::Control);
  hub.add(QStringLiteral("rsi"), rsiDev, DeviceHub::DeviceGroup::Control);

  DeviceHubQml::s_inst = &hub;

  hub.startAll();

  // The scene outlives the QML engine and survives workspace recreation.
  SceneModel scene;
  SceneSingletonQml::s_inst = &scene;
  SceneEndEffectors endEffectors;
  SceneEndEffectorsQml::s_inst = &endEffectors;

  ScenePlayback playback(&scene);
  ScenePlaybackQml::s_inst = &playback;
  SceneMachining machining(&scene, &endEffectors, &playback);
  SceneMachiningQml::s_inst = &machining;

  SceneCalibration calibration(&scene, &endEffectors, &playback);
  SceneCalibrationQml::s_inst = &calibration;

  // Declared before preparation, which calls it until its own destruction.
  SessionRecorder recorder;
  QObject::connect(rsiDev, &RsiDevice::captureReady, &recorder, &SessionRecorder::addRsiCapture,
                   Qt::QueuedConnection);
  QObject::connect(plcDev, &PlcDevice::ioRecorded, &recorder, &SessionRecorder::addPlcRecord, Qt::QueuedConnection);
  QObject::connect(&recorder, &SessionRecorder::captureRequested, rsiDev, &RsiDevice::flushCapture,
                   Qt::QueuedConnection);

  SceneMachiningPreparation preparation(&scene, &machining, &calibration);
  SceneMachiningPreparationQml::s_inst = &preparation;
  preparation.attachRecorder(&recorder);
  // Implicitly shared corrections and their log identifier cross to ControlIO together.
  QObject::connect(&preparation, &SceneMachiningPreparation::correctionsReady,
                   rsiDev, &RsiDevice::setCorrections, Qt::QueuedConnection);
  // Calibration run: program 1 through the PLC, one detection per touch from ControlIO.
  preparation.attachPlc(hub.device(QStringLiteral("plc")));
  QObject::connect(rsiDev, &RsiDevice::touchDetected, &preparation,
                   &SceneMachiningPreparation::recordTouch, Qt::QueuedConnection);
  // Disable result dispatch and finish the worker before shutting down ControlIO.
  QObject::connect(&app, &QApplication::aboutToQuit, &preparation, &SceneMachiningPreparation::shutdown);
  QObject::connect(&app, &QApplication::aboutToQuit, &hub, &DeviceHub::stopAll);
  // After stopAll: the devices' last captures and PLC rows, then the record writes.
  QObject::connect(&app, &QApplication::aboutToQuit, &recorder, &SessionRecorder::shutdown);

  RoboCrap3D::OccController viewportController(
      RoboCrap3D::ViewportAssets::applicationAssets().occtResources);
  scene.setViewport(&viewportController);
  QObject::connect(&machining, &SceneMachining::draftAxisChanged,
                   &viewportController, &RoboCrap3D::OccController::setDraftAxis);
  OccControllerQml::s_inst = &viewportController;
  RoboCrap3D::RobotController robotController(&viewportController, &endEffectors, &playback);
  playback.setRobotController(&robotController);
  WorkspaceController workspaceController(&scene, &machining, &endEffectors);
  QObject::connect(&workspaceController, &WorkspaceController::calibrationActiveChanged,
                   &calibration, &SceneCalibration::setActive);
  QObject::connect(&workspaceController, &WorkspaceController::preciseActiveChanged,
                   &calibration, &SceneCalibration::setPreciseActive);
  QObject::connect(&workspaceController, &WorkspaceController::selectionChanged,
                   &viewportController, &RoboCrap3D::OccController::setSelectedObjects);
  QObject::connect(&viewportController, &RoboCrap3D::OccController::applicationSelectionRequested,
                   &workspaceController, &WorkspaceController::pickObject);
  QObject::connect(&viewportController, &RoboCrap3D::OccController::toolSelectionRequested,
                   &workspaceController, &WorkspaceController::selectTool);
  QObject::connect(&workspaceController, &WorkspaceController::toolSelectedChanged,
                   &viewportController, &RoboCrap3D::OccController::setToolSelected);
  QObject::connect(&viewportController, &RoboCrap3D::OccController::deleteSelectionRequested,
                   &workspaceController, &WorkspaceController::deleteSelected);

  QObject::connect(&viewportController, &RoboCrap3D::OccController::message, &app, &logViewportMessage);
  QObject::connect(&robotController, &RoboCrap3D::RobotController::message, &app, &logViewportMessage);

  robotController.loadRobot();

  QQmlApplicationEngine engine;

#ifdef USE_FELGO_HOT_RELOAD
  felgo.initialize(&engine);
#endif

#ifdef USE_FELGO_HOT_RELOAD

  const QString appName = QCoreApplication::applicationName();
  const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  const QString hotReloadModulesPath =
      QDir(appDataPath).filePath(
        QStringLiteral("FelgoDevApp/%1/%1/qml/Modules").arg(appName)
      );

  engine.addImportPath(hotReloadModulesPath);

  qWarning() << "Application name:"
             << QCoreApplication::applicationName();

  qWarning() << "App data:"
             << QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);

  FelgoHotReload felgoHotReload(&engine);

#else

  engine.loadFromModule("robocrap_qml_module", "Main");

  if (engine.rootObjects().isEmpty())
    return 77;

#endif

  return app.exec();
}

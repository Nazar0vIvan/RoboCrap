#include "scenecalibration.h"

#include "assetpaths.h"
#include "geometry/spherefit.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace {

const QString SettingsPath = QStringLiteral(ROBOCRAP_SETTINGS_DIR "/probe_calibration.json");

struct ProbeSettingsResult
{
  ProbeSettings settings; // Empty for a missing or invalid file.
  QString error;
};

struct SphereFileResult
{
  std::optional<SceneSphere> sphere;
  QString error;
};

std::optional<V3d> roughPoint(const QJsonValue& entry)
{
  const QJsonObject point = entry.toObject();
  const QJsonValue x = point.value(QStringLiteral("X"));
  const QJsonValue y = point.value(QStringLiteral("Y"));
  const QJsonValue z = point.value(QStringLiteral("Z"));
  if (!x.isDouble() || !y.isDouble() || !z.isDouble()) return std::nullopt;
  const V3d result{x.toDouble(), y.toDouble(), z.toDouble()};
  return result.allFinite() ? std::optional<V3d>{result} : std::nullopt;
}

std::optional<double> positiveNumber(const QJsonObject& root, const QString& key)
{
  const QJsonValue value = root.value(key);
  if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() <= 0.0) return std::nullopt;
  return value.toDouble();
}

// [x, y, z] with three finite numbers.
std::optional<V3d> vectorValue(const QJsonValue& entry)
{
  const QJsonArray array = entry.toArray();
  if (array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble())
    return std::nullopt;
  const V3d result{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
  return result.allFinite() ? std::optional<V3d>{result} : std::nullopt;
}

// A nonnegative integer.
std::optional<int> countValue(const QJsonObject& object, const QString& key)
{
  const double value = object.value(key).toDouble(-1.0);
  if (value < 0.0 || value > std::numeric_limits<int>::max() || value != std::floor(value)) return std::nullopt;
  return static_cast<int>(value);
}

QJsonArray vectorArray(const V3d& value)
{
  return {value.x(), value.y(), value.z()};
}

QString vectorText(const V3d& value)
{
  return QStringLiteral("[%1; %2; %3]").arg(value.x(), 0, 'f', 4).arg(value.y(), 0, 'f', 4).arg(value.z(), 0, 'f', 4);
}

QVariantMap resultRow(const QString& label, const QString& value)
{
  return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

int setsOutsideBound(const ProbeCalibrationFit& fit)
{
  int count = 0;
  for (const ProbeResidualSet& set : fit.residualSets)
    if (std::abs(set.mean) > set.bound) ++count;
  return count;
}

// Every lastCalibration key is required.
std::optional<CalibrationSummary> calibrationSummary(const QJsonObject& object)
{
  const QString id = object.value(QStringLiteral("id")).toString();
  const auto ball = vectorValue(object.value(QStringLiteral("ballCentreF")));
  const auto sphere = vectorValue(object.value(QStringLiteral("sphereCentreB")));
  const auto verification = vectorValue(object.value(QStringLiteral("verificationSphereCentreB")));
  const auto vertical = positiveNumber(object, QStringLiteral("contactVertical"));
  const auto horizontal = positiveNumber(object, QStringLiteral("contactHorizontal"));
  const auto quantile = positiveNumber(object, QStringLiteral("quantile95"));
  const auto repeatability = positiveNumber(object, QStringLiteral("repeatability"));
  const auto singularValue = positiveNumber(object, QStringLiteral("smallestSingularValue"));
  const auto calibrationTouches = countValue(object, QStringLiteral("calibrationTouches"));
  const auto verificationTouches = countValue(object, QStringLiteral("verificationTouches"));
  const auto outside = countValue(object, QStringLiteral("setsOutsideBound"));
  const auto sets = countValue(object, QStringLiteral("setCount"));
  if (id.isEmpty() || !ball || !sphere || !verification || !vertical || !horizontal || !quantile || !repeatability
      || !singularValue || !calibrationTouches || !verificationTouches || !outside || !sets)
    return std::nullopt;
  return CalibrationSummary{id, *ball, *sphere, *verification, *vertical, *horizontal, *quantile, *repeatability,
                            *singularValue, *calibrationTouches, *verificationTouches, *outside, *sets};
}

QJsonObject summaryObject(const CalibrationSummary& summary)
{
  return {{QStringLiteral("id"), summary.id},
          {QStringLiteral("ballCentreF"), vectorArray(summary.ballCentre)},
          {QStringLiteral("sphereCentreB"), vectorArray(summary.sphereCentre)},
          {QStringLiteral("verificationSphereCentreB"), vectorArray(summary.verificationSphereCentre)},
          {QStringLiteral("contactVertical"), summary.contactVertical},
          {QStringLiteral("contactHorizontal"), summary.contactHorizontal},
          {QStringLiteral("quantile95"), summary.quantile95},
          {QStringLiteral("repeatability"), summary.repeatability},
          {QStringLiteral("smallestSingularValue"), summary.smallestSingularValue},
          {QStringLiteral("calibrationTouches"), summary.calibrationTouches},
          {QStringLiteral("verificationTouches"), summary.verificationTouches},
          {QStringLiteral("setsOutsideBound"), summary.setsOutsideBound},
          {QStringLiteral("setCount"), summary.setCount}};
}

// Only the saved values; an absent key keeps its built-in default.
QJsonObject primaryObject(const PrimaryCentres& primary)
{
  QJsonObject result;
  if (!primary.source.isEmpty()) result.insert(QStringLiteral("source"), primary.source);
  if (primary.ballCentre) result.insert(QStringLiteral("ballCentreF"), vectorArray(*primary.ballCentre));
  if (primary.sphereCentre) result.insert(QStringLiteral("sphereCentreB"), vectorArray(*primary.sphereCentre));
  if (primary.contactVertical) result.insert(QStringLiteral("contactVertical"), *primary.contactVertical);
  if (primary.contactHorizontal) result.insert(QStringLiteral("contactHorizontal"), *primary.contactHorizontal);
  return result;
}

// Version 1. A missing file gives empty settings without an error; every key but version is
// optional, and a present key must be valid.
ProbeSettingsResult readProbeSettings()
{
  const QString prefix = SceneCalibration::tr("Probe settings file %1: ").arg(QDir::toNativeSeparators(SettingsPath));
  QFile file(SettingsPath);
  if (!file.exists()) return {};
  if (!file.open(QIODevice::ReadOnly))
    return {{}, prefix + SceneCalibration::tr("cannot be read (%1).").arg(file.errorString())};
  QJsonParseError parse;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject())
    return {{}, prefix + SceneCalibration::tr("is not a JSON object (%1).").arg(parse.errorString())};
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("version")).toInt() != 1)
    return {{}, prefix + SceneCalibration::tr("version must be 1.")};
  const QJsonValue primaryValue = root.value(QStringLiteral("primary"));
  const QJsonValue lastValue = root.value(QStringLiteral("lastCalibration"));
  if (!(primaryValue.isUndefined() || primaryValue.isObject()) || !(lastValue.isUndefined() || lastValue.isObject()))
    return {{}, prefix + SceneCalibration::tr("primary and lastCalibration must be objects.")};
  const QJsonObject primary = primaryValue.toObject();
  const auto ball = vectorValue(primary.value(QStringLiteral("ballCentreF")));
  const auto sphere = vectorValue(primary.value(QStringLiteral("sphereCentreB")));
  const auto vertical = positiveNumber(primary, QStringLiteral("contactVertical"));
  const auto horizontal = positiveNumber(primary, QStringLiteral("contactHorizontal"));
  if ((primary.contains(QStringLiteral("ballCentreF")) && !ball)
      || (primary.contains(QStringLiteral("sphereCentreB")) && !sphere)
      || (primary.contains(QStringLiteral("contactVertical")) && !vertical)
      || (primary.contains(QStringLiteral("contactHorizontal")) && !horizontal))
    return {{}, prefix + SceneCalibration::tr("primary ballCentreF and sphereCentreB need three finite numbers, "
                                              "contactVertical and contactHorizontal finite positive numbers.")};
  ProbeSettings settings{{primary.value(QStringLiteral("source")).toString(), ball, sphere, vertical, horizontal}, {}};
  if (lastValue.isObject()) {
    settings.last = calibrationSummary(lastValue.toObject());
    if (!settings.last)
      return {{}, prefix + SceneCalibration::tr("lastCalibration needs id, three centres, five finite positive "
                                                "numbers and four nonnegative integer counts.")};
  }
  return {settings, {}};
}

// Atomically replaces the settings file; empty on success.
QString writeProbeSettings(const ProbeSettings& settings)
{
  QJsonObject root{{QStringLiteral("version"), 1}};
  if (const QJsonObject primary = primaryObject(settings.primary); !primary.isEmpty())
    root.insert(QStringLiteral("primary"), primary);
  if (settings.last) root.insert(QStringLiteral("lastCalibration"), summaryObject(*settings.last));
  const QByteArray contents = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (!QDir().mkpath(QStringLiteral(ROBOCRAP_SETTINGS_DIR)))
    return SceneCalibration::tr("Cannot create %1.").arg(QDir::toNativeSeparators(QStringLiteral(ROBOCRAP_SETTINGS_DIR)));
  QSaveFile file(SettingsPath);
  if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    return SceneCalibration::tr("Cannot save %1: %2").arg(QDir::toNativeSeparators(SettingsPath), file.errorString());
  return {};
}

// The table object: finite positive lengthX, widthY, sphereFromMinusX, sphereFromMinusY and
// sphereAboveTop (mm), with the sphere center over the table.
std::optional<CalibrationTable> calibrationTable(const QJsonObject& root)
{
  const QJsonObject table = root.value(QStringLiteral("table")).toObject();
  const auto lengthX = positiveNumber(table, QStringLiteral("lengthX"));
  const auto widthY = positiveNumber(table, QStringLiteral("widthY"));
  const auto fromMinusX = positiveNumber(table, QStringLiteral("sphereFromMinusX"));
  const auto fromMinusY = positiveNumber(table, QStringLiteral("sphereFromMinusY"));
  const auto aboveTop = positiveNumber(table, QStringLiteral("sphereAboveTop"));
  if (!lengthX || !widthY || !fromMinusX || !fromMinusY || !aboveTop
      || *fromMinusX >= *lengthX || *fromMinusY >= *widthY) return std::nullopt;
  return CalibrationTable{*lengthX, *widthY, *fromMinusX, *fromMinusY, *aboveTop};
}

// Version 1: finite positive sphereDiameter, strutDiameter and strutRecess (mm, the recess below
// the sphere diameter), the table object and roughPoints entries with X, Y, Z (mm) of the ruby
// ball centers; other keys are ignored. ballRadius (mm) compensates the fitted sphere.
SphereFileResult readSphereFile(const QString& path, double ballRadius)
{
  const QString prefix = SceneCalibration::tr("Calibration sphere file %1: ").arg(QDir::toNativeSeparators(path));
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {{}, prefix + SceneCalibration::tr("cannot be read (%1).").arg(file.errorString())};
  QJsonParseError parse;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject())
    return {{}, prefix + SceneCalibration::tr("is not a JSON object (%1).").arg(parse.errorString())};
  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("version")).toInt() != 1)
    return {{}, prefix + SceneCalibration::tr("version must be 1.")};
  const auto diameter = positiveNumber(root, QStringLiteral("sphereDiameter"));
  const auto strutDiameter = positiveNumber(root, QStringLiteral("strutDiameter"));
  const auto strutRecess = positiveNumber(root, QStringLiteral("strutRecess"));
  if (!diameter || !strutDiameter || !strutRecess)
    return {{}, prefix + SceneCalibration::tr("sphereDiameter, strutDiameter and strutRecess must be finite positive numbers.")};
  if (*strutRecess >= *diameter)
    return {{}, prefix + SceneCalibration::tr("strutRecess must be smaller than sphereDiameter.")};
  const auto table = calibrationTable(root);
  if (!table)
    return {{}, prefix + SceneCalibration::tr("table needs finite positive lengthX, widthY, sphereFromMinusX, "
                                              "sphereFromMinusY and sphereAboveTop, with the sphere over the table.")};
  QList<V3d> points;
  bool finite = true;
  for (const QJsonValue& entry : root.value(QStringLiteral("roughPoints")).toArray()) {
    const auto point = roughPoint(entry);
    if (!point) { finite = false; break; }
    points.append(*point);
  }
  if (!finite || points.size() < 4)
    return {{}, prefix + SceneCalibration::tr("roughPoints needs at least 4 points with finite X, Y, Z.")};
  const auto fit = fitSphere(points);
  if (!fit)
    return {{}, prefix + SceneCalibration::tr("roughPoints do not determine a sphere (coplanar or degenerate).")};
  const double compensatedRadius = fit->radius - ballRadius;
  if (!(compensatedRadius > 0.0))
    return {{}, prefix + SceneCalibration::tr("the ball-center radius %1 mm does not exceed the ruby ball radius %2 mm.")
                    .arg(fit->radius, 0, 'f', 4).arg(ballRadius, 0, 'f', 4)};
  return {SceneSphere{fit->center, fit->center, *diameter / 2.0, fit->radius, compensatedRadius, fit->rms, points,
                      *strutDiameter / 2.0, *strutRecess, *table}, {}};
}

bool calibrationObject(const SceneObject* object)
{
  return object->kind() == SceneObject::Sphere || object->kind() == SceneObject::CalibrationPath;
}

// Worker entry: the plan and motion from numerical copies only.
ProbeTouchMotionResult calibrationMotion(const ProbeTouchPlanParameters& parameters, const ChamferRobotSetup& robot)
{
  const auto plan = createProbeTouchPlan(parameters);
  if (!plan.plan) return {{}, plan.error};
  return ProbeTouchMotion::create(*plan.plan, robot);
}

} // namespace

SceneCalibration::SceneCalibration(SceneModel* scene, SceneEndEffectors* effectors, ScenePlayback* playback)
  : m_scene(scene), m_effectors(effectors), m_playback(playback)
{
  auto loaded = readProbeSettings();
  m_settings = std::move(loaded.settings);
  m_settingsReadError = loaded.error;
  setSettingsError(loaded.error);
  // Applied before the save connection below, so the start load writes nothing.
  if (m_settings.primary.ballCentre) applyBallCentre(*m_settings.primary.ballCentre);
  connect(scene, &SceneModel::objectAdded, this, &SceneCalibration::onObjectAdded);
  connect(effectors, &SceneEndEffectors::measuringTcpChanged, this, &SceneCalibration::invalidatePath);
  connect(effectors, &SceneEndEffectors::measuringTcpChanged, this, &SceneCalibration::onMeasuringTcpChanged);
  connect(playback, &ScenePlayback::robotModelChanged, this, &SceneCalibration::invalidatePath);
  connect(playback, &ScenePlayback::robotAvailabilityChanged, this, &SceneCalibration::availabilityChanged);
  connect(playback, &ScenePlayback::stateChanged, this, &SceneCalibration::availabilityChanged);
  connect(playback, &ScenePlayback::failed, this, &SceneCalibration::onPlaybackFailed);
  m_calculationTimer.setInterval(30);
  connect(&m_calculationTimer, &QTimer::timeout, this, &SceneCalibration::finishCalculation);
}

void SceneCalibration::setActive(bool active)
{
  if (m_active == active) return;
  m_active = active;
  if (active) activate();
  else deactivate();
}

void SceneCalibration::activate()
{
  auto file = readSphereFile(QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("resources/json/calibration_sphere.json")),
                             m_effectors->rubyBallDiameter() / 2.0);
  setError(file.error);
  if (file.sphere) {
    if (m_settings.primary.sphereCentre) file.sphere->center = *m_settings.primary.sphereCentre;
    // Converted from resources/deploy/cad/fixtures/CALIBRATION_STAND.stp at build time.
    file.sphere->standModel = QDir(RoboCrap3D::ViewportAssets::applicationAssets().cadDirectory)
                                  .filePath(QStringLiteral("CALIBRATION_STAND.brep"));
    replaceSphere(*file.sphere);
  }
  for (auto* object : m_scene->objectList()) hideOther(object);
  for (quint32 id : {m_sphereId, m_pathId})
    if (auto* object = m_scene->findObject(id)) m_scene->setObjectVisible(object, true);
}

void SceneCalibration::deactivate()
{
  if (ownsPlayback()) m_playback->stop();
  for (quint32 id : {m_sphereId, m_pathId})
    if (auto* object = m_scene->findObject(id)) m_scene->setObjectVisible(object, false);
  for (quint32 id : std::exchange(m_hiddenIds, {}))
    if (auto* object = m_scene->findObject(id)) m_scene->setObjectVisible(object, true);
}

const SceneSphere* SceneCalibration::sphere() const
{
  const auto* object = m_scene->findObject(m_sphereId);
  return object ? object->sphere() : nullptr;
}

const SceneTouchPath* SceneCalibration::path() const
{
  const auto* object = m_scene->findObject(m_pathId);
  return object ? object->touchPath() : nullptr;
}

bool SceneCalibration::ownsPlayback() const
{
  return m_pathId != 0 && m_playback->active() && m_playback->ownerId() == m_pathId;
}

void SceneCalibration::onPlaybackFailed(quint32 ownerId, const QString& error)
{
  if (ownerId != 0 && ownerId == m_pathId) setError(error);
}

bool SceneCalibration::canPlay() const
{
  return !calculating() && m_playback->canPlay(ownsPlayback(), path() != nullptr);
}

void SceneCalibration::dryRun()
{
  if (!canPlay()) { setError(tr("Generate the calibration path before Dry Run.")); return; }
  if (m_playback->active()) { m_playback->resume(); return; }
  setError({});
  const auto& motion = path()->motion;
  // Aliasing handle: points at the stored timed motion and keeps the path's motion alive.
  m_playback->start(std::shared_ptr<const TimedMotion>(motion, &motion->timed()), m_pathId);
}

void SceneCalibration::replaceSphere(const SceneSphere& sphere)
{
  const auto* existing = this->sphere();
  if (existing && *existing == sphere) return;
  auto* inserted = m_scene->addCalibrationObject(sphere, tr("Calibration sphere"));
  if (!inserted) {
    setError(tr("Cannot insert the calibration sphere: scene IDs or drawing installation failed."));
    return;
  }
  if (existing) m_scene->removeObjects({m_sphereId});
  m_sphereId = inserted->objectId();
  invalidatePath();
}

void SceneCalibration::invalidatePath()
{
  ++m_revision;
  if (m_scene->findObject(m_pathId)) m_scene->removeObjects({m_pathId});
  m_pathId = 0;
  emit availabilityChanged();
}

QString SceneCalibration::unavailableReason() const
{
  if (calculating()) return tr("Calculating trajectory…");
  if (m_playback->active()) return tr("Stop Dry Run before generating a trajectory.");
  if (!m_playback->robotModel() || !m_playback->robotAvailable()) return tr("Robot preview is not ready.");
  if (!m_effectors->measuringTcpFrame()) return tr("Apply the measuring TCP first.");
  if (!sphere()) return tr("The calibration sphere is missing: check the sphere file.");
  return {};
}

void SceneCalibration::generate()
{
  const QString reason = unavailableReason();
  if (!reason.isEmpty()) { setError(reason); return; }
  const SceneSphere& value = *sphere();
  const ProbeTouchPlanParameters parameters{value.center, value.radius, m_effectors->rubyBallDiameter() / 2.0,
                                            value.strutRadius, value.strutRecess};
  ChamferRobotSetup robot{*m_playback->robotModel(), *m_effectors->measuringTcpFrame(), {}};
  m_calculationRevision = m_revision;
  setError({});
  // The worker owns numerical copies only; its result returns through the future.
  m_calculation = std::async(std::launch::async, &calibrationMotion, parameters, std::move(robot));
  m_calculationTimer.start();
  emit availabilityChanged();
}

void SceneCalibration::finishCalculation()
{
  if (m_calculation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
  m_calculationTimer.stop();
  auto result = m_calculation.get();
  if (result.motion)
    qInfo().noquote() << QStringLiteral("Probe calibration preparation: %1 calibration and %2 verification touches, "
                                        "%3 points, %4 s movement, minimum transfer clearance %5 mm, "
                                        "minimum strut clearance %6 mm")
                             .arg(result.motion->plan().touchCount(ProbeTouchPurpose::Calibration))
                             .arg(result.motion->plan().touchCount(ProbeTouchPurpose::Verification))
                             .arg(result.motion->points().size())
                             .arg(result.motion->duration(), 0, 'g', 12)
                             .arg(result.motion->minimumTransferClearance(), 0, 'f', 4)
                             .arg(result.motion->minimumStrutClearance(), 0, 'f', 4);
  else
    qInfo().noquote() << "Probe calibration preparation failed:" << result.error;
  if (m_revision != m_calculationRevision) setError(tr("Sphere, probe TCP or robot changed. Generate again."));
  else if (!result.motion) setError(result.error);
  else if (auto* inserted = m_scene->addCalibrationObject(
               SceneTouchPath{std::make_shared<const ProbeTouchMotion>(std::move(*result.motion))},
               tr("Calibration path"))) {
    if (!m_active) m_scene->setObjectVisible(inserted, false);
    if (m_scene->findObject(m_pathId)) m_scene->removeObjects({m_pathId});
    m_pathId = inserted->objectId();
  } else setError(tr("Cannot insert the calibration path: scene IDs or drawing installation failed."));
  emit availabilityChanged();
}

void SceneCalibration::hideOther(SceneObject* object)
{
  if (calibrationObject(object) || !object->visible()) return;
  m_hiddenIds.append(object->objectId());
  m_scene->setObjectVisible(object, false);
}

void SceneCalibration::onObjectAdded(SceneObject* object)
{
  if (m_active) hideOther(object);
}

void SceneCalibration::setError(const QString& error)
{
  if (m_error == error) return;
  m_error = error;
  emit errorChanged();
}

void SceneCalibration::onMeasuringTcpChanged()
{
  const QList<double>& pose = m_effectors->measuringTcp();
  const V3d ball{pose[0], pose[1], pose[2]};
  // A change of only A, B or C, or a ball centre that is already saved, writes nothing.
  if (m_settings.primary.ballCentre == ball) return;
  m_settings.primary.ballCentre = ball;
  m_settings.primary.source = QStringLiteral("manual");
  setSettingsError(saveSettings());
}

void SceneCalibration::applyBallCentre(const V3d& ball)
{
  QList<double> pose = m_effectors->measuringTcp();
  pose[0] = ball.x();
  pose[1] = ball.y();
  pose[2] = ball.z();
  m_effectors->applyMeasuringTcp(pose); // Finite values (file check or fit) are accepted.
}

// Four decimals like the Properties panel.
QVariantList SceneCalibration::lastCalibrationRows() const
{
  if (!m_settings.last) return {};
  const CalibrationSummary& last = *m_settings.last;
  return {
    resultRow(tr("Ball centre in {F}, mm"), vectorText(last.ballCentre)),
    resultRow(tr("Sphere centre in {B}, mm"), vectorText(last.sphereCentre)),
    resultRow(tr("Contact distance, stylus down, mm"), QString::number(last.contactVertical, 'f', 4)),
    resultRow(tr("Contact distance, stylus horizontal, mm"), QString::number(last.contactHorizontal, 'f', 4)),
    resultRow(tr("Quantile χ95, mm"), QString::number(last.quantile95, 'f', 4)),
    resultRow(tr("Repeatability s_rep, mm"), QString::number(last.repeatability, 'f', 4)),
    resultRow(tr("Sphere centre from verification, mm"), vectorText(last.verificationSphereCentre)),
    resultRow(tr("Smallest singular value"), QString::number(last.smallestSingularValue, 'f', 4)),
    resultRow(tr("Touches (calibration / verification)"),
              QStringLiteral("%1 / %2").arg(last.calibrationTouches).arg(last.verificationTouches)),
    resultRow(tr("Model check: sets outside 3·s_rep/√m"), tr("%1 of %2").arg(last.setsOutsideBound).arg(last.setCount)),
  };
}

QString SceneCalibration::recordCalibrationResult(const ProbeCalibrationFit& fit, const QString& id)
{
  const CalibrationSummary summary{id, fit.ballCentre, fit.sphereCentre, fit.verificationSphereCentre,
                                   fit.contactVertical, fit.contactHorizontal, fit.quantile95, fit.repeatability,
                                   fit.smallestSingularValue, fit.calibrationTouches, fit.verificationTouches,
                                   setsOutsideBound(fit), static_cast<int>(fit.residualSets.size())};
  // The reader's own check: a result that would make the next start reject the file is not kept.
  if (!calibrationSummary(summaryObject(summary)))
    return tr("Calibration result not saved: the fit gave non-finite or non-positive values.");
  m_settings.last = summary;
  setSettingsError(saveSettings());
  emit lastCalibrationChanged();
  return m_settingsError;
}

bool SceneCalibration::reassignPrimaryCentres()
{
  if (!m_settings.last) return false;
  const CalibrationSummary& last = *m_settings.last;
  // Set first, so onMeasuringTcpChanged finds the saved ball centre and keeps source = the run id.
  m_settings.primary = {last.id, last.ballCentre, last.sphereCentre, last.contactVertical, last.contactHorizontal};
  applyBallCentre(last.ballCentre);
  setSettingsError(saveSettings());
  // Outside the submode the next activate() applies the saved sphere centre.
  if (const auto* current = sphere(); current && m_active) {
    SceneSphere moved = *current;
    moved.center = last.sphereCentre;
    replaceSphere(moved);
  }
  return m_settingsError.isEmpty();
}

void SceneCalibration::setSettingsError(const QString& error)
{
  if (m_settingsError == error && error.isEmpty()) return;
  m_settingsError = error;
  if (!error.isEmpty()) qInfo().noquote() << "[ERROR]" << error;
  emit settingsErrorChanged();
}

QString SceneCalibration::saveSettings() const
{
  // An unreadable file stays as it is until the user fixes or deletes it.
  if (!m_settingsReadError.isEmpty())
    return m_settingsReadError + tr(" Not saved: fix or delete the file, then restart RoboCrap.");
  return writeProbeSettings(m_settings);
}

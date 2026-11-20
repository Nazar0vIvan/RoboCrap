#include "occworldaxes.h"
#include "occt/occpartprops.h"

#include <AIS_Line.hxx>

#include <Geom_CartesianPoint.hxx>

#include <Graphic3d_ZLayerId.hxx>

#include <Quantity_Color.hxx>

namespace RoboCrap3D {

OccWorldAxes::OccWorldAxes(const double initialLength)
{
  createAxes(initialLength);
  configureAxes();
}

void OccWorldAxes::setLength(const double length)
{
  if (length <= 0.0) return;

  m_length = length;
  m_xEndPoint->SetCoord(m_length, 0.0, 0.0);
  m_yEndPoint->SetCoord(0.0, m_length, 0.0);
  m_zEndPoint->SetCoord(0.0, 0.0, m_length);
}

const Handle(AIS_Line)& OccWorldAxes::xAxis() const
{
  return m_xAxis;
}

const Handle(AIS_Line)& OccWorldAxes::yAxis() const
{
  return m_yAxis;
}

const Handle(AIS_Line)& OccWorldAxes::zAxis() const
{
  return m_zAxis;
}

void OccWorldAxes::createAxes(const double initialLength)
{
  m_length = initialLength > 0.0 ? initialLength : kDefaultAxisLength;

  m_originPoint = new Geom_CartesianPoint(0.0, 0.0, 0.0);

  m_xEndPoint = new Geom_CartesianPoint(m_length, 0.0, 0.0);
  m_yEndPoint = new Geom_CartesianPoint(0.0, m_length, 0.0);
  m_zEndPoint = new Geom_CartesianPoint(0.0, 0.0, m_length);

  m_xAxis = new AIS_Line(m_originPoint, m_xEndPoint);
  m_yAxis = new AIS_Line(m_originPoint, m_yEndPoint);
  m_zAxis = new AIS_Line(m_originPoint, m_zEndPoint);
}

void OccWorldAxes::configureAxes()
{
  constexpr double width = 2.0;

  // Reference axes must not contribute to camera-fit bounds.
  m_xAxis->SetInfiniteState(true);
  m_yAxis->SetInfiniteState(true);
  m_zAxis->SetInfiniteState(true);

  m_xAxis->SetColor(rgb(255, 0, 0));
  m_yAxis->SetColor(rgb(0, 180, 0));
  m_zAxis->SetColor(rgb(0, 0, 255));

  m_xAxis->SetWidth(width);
  m_yAxis->SetWidth(width);
  m_zAxis->SetWidth(width);

  m_xAxis->SetZLayer(Graphic3d_ZLayerId_Topmost);
  m_yAxis->SetZLayer(Graphic3d_ZLayerId_Topmost);
  m_zAxis->SetZLayer(Graphic3d_ZLayerId_Topmost);
}
} // namespace RoboCrap3D

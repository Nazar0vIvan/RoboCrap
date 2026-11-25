#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <TopoDS_Shape.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <NCollection_IndexedMap.hxx>

namespace RoboCrap3D {

struct CadLoadResult
{
  QList<TopoDS_Shape> shapes;
  QString error;
  NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces; // 1-based, in the source frame.
  QByteArray modelSha256; // Single DrawingModel loads only.
};

} // namespace RoboCrap3D

#pragma once

#include <QString>
#include <optional>

class QWidget;

struct OnvifCamera {
  QString name;
  QString mainStream;
  QString subStream;
};

std::optional<OnvifCamera> discoverOnvifCamera(QWidget *parent);

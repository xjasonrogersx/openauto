/*
*  This file is part of openauto project.
*  Copyright (C) 2018 f1x.studio (Michal Szwaj)
*
*  openauto is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation; either version 3 of the License, or
*  (at your option) any later version.

*  openauto is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with openauto. If not, see <http://www.gnu.org/licenses/>.
*/

#include <QApplication>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <fstream>

#include "ui_mainwindow.h"

namespace f1x {
namespace openauto {
namespace autoapp {
namespace ui {

MainWindow::MainWindow(configuration::IConfiguration::Pointer configuration,
                       QWidget* parent)
    : QMainWindow(parent), ui_(new Ui::MainWindow) {
  // Force a fixed window size.
  const QSize windowSize(800, 300);
  this->resize(windowSize);

  // Scale background image to fill the window and apply via palette
  QPixmap bg(":/1777450215290.png");
  bg = bg.scaled(windowSize, Qt::KeepAspectRatioByExpanding,
                 Qt::SmoothTransformation);
  QPalette palette;
  palette.setBrush(QPalette::Window, QBrush(bg));
  this->setPalette(palette);
  this->setAutoFillBackground(true);

  // Set default font and size
  int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
  QString family = QFontDatabase::applicationFontFamilies(id).at(0);
  QFont _font(family, 11);
  qApp->setFont(_font);

  this->configuration_ = configuration;

  ui_->setupUi(this);

  watcher_tmp = new QFileSystemWatcher(this);
  watcher_tmp->addPath("/tmp");
  connect(watcher_tmp, &QFileSystemWatcher::directoryChanged, this,
          &MainWindow::tmpChanged);
}

MainWindow::~MainWindow() { delete ui_; }

}  // namespace ui
}  // namespace autoapp
}  // namespace openauto
}  // namespace f1x

void f1x::openauto::autoapp::ui::MainWindow::tmpChanged() {
  OPENAUTO_LOG(info) << "tmpChanged";

  try {
    if (std::ifstream("/tmp/entityexit")) {
      MainWindow::TriggerAppStop();
      std::remove("/tmp/entityexit");
    }
  } catch (...) {
    OPENAUTO_LOG(error) << "[OpenAuto] Error in entityexit";
  }

  if (std::ifstream("/tmp/external_exit")) {
    f1x::openauto::autoapp::ui::MainWindow::MainWindow::exit();
  }
}

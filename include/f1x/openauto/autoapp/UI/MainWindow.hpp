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

#pragma once

#include <QFileSystemWatcher>
#include <QMainWindow>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <memory>

namespace Ui {
class MainWindow;
}

namespace f1x {
namespace openauto {
namespace autoapp {
namespace ui {

class MainWindow : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(configuration::IConfiguration::Pointer configuration,
                      QWidget* parent = nullptr);
  ~MainWindow() override;
  QFileSystemWatcher* watcher_tmp;

 signals:
  void exit();
  void reboot();
  void openSettings();
  void toggleCursor();
  void TriggerScriptDay();
  void TriggerScriptNight();
  void openConnectDialog();
  void openWifiDialog();
  void showAlphaSlider();
  void TriggerAppStart();
  void TriggerAppStop();
  void CloseAllDialogs();

 private slots:
  void tmpChanged();

 private:
  Ui::MainWindow* ui_;
  configuration::IConfiguration::Pointer configuration_;
};

}  // namespace ui
}  // namespace autoapp
}  // namespace openauto
}  // namespace f1x

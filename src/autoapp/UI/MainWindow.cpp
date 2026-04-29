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

#include <unistd.h>

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QMessageBox>
#include <QNetworkInterface>
#include <QPushButton>
#include <QRect>
#include <QScreen>
#include <QStandardItemModel>
#include <QTextStream>
#include <QTimer>
#include <QVideoWidget>
#include <cstdio>
#include <f1x/openauto/Common/Log.hpp>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <fstream>
#include <iostream>

#include "ui_mainwindow.h"

namespace f1x {
namespace openauto {
namespace autoapp {
namespace ui {

MainWindow::MainWindow(configuration::IConfiguration::Pointer configuration,
                       QWidget* parent)
    : QMainWindow(parent),
      ui_(new Ui::MainWindow),
      localDevice(new QBluetoothLocalDevice) {
  // Set background image for the main window, fallback to black if missing.
  const QString backgroundImage = "/home/pi/openauto/images/1777450215290.png";
  if (QFileInfo::exists(backgroundImage)) {
    this->setStyleSheet(QString("QMainWindow {"
                                "background-color: rgb(0,0,0);"
                                "background-image: url(%1);"
                                "background-position: center;"
                                "background-repeat: no-repeat;"
                                "}")
                            .arg(backgroundImage));
  } else {
    this->setStyleSheet("QMainWindow {background-color: rgb(0,0,0);}");
  }

  // Set default font and size
  int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
  QString family = QFontDatabase::applicationFontFamilies(id).at(0);
  QFont _font(family, 11);
  qApp->setFont(_font);

  this->configuration_ = configuration;

  // trigger files

  this->wifiButtonForce = check_file_exist(this->wifiButtonFile);
  this->systemDebugmode = check_file_exist(this->debugModeFile);
  this->lightsensor = check_file_exist(this->lsFile);

  ui_->setupUi(this);

  // Configure window attributes to prevent ghosting
  this->setAttribute(Qt::WA_OpaquePaintEvent, true);
  this->setAttribute(Qt::WA_NoSystemBackground, false);
  this->setAutoFillBackground(true);

  ui_->btDevice->hide();

  // check if a device is connected via bluetooth
  if (std::ifstream("/tmp/btdevice")) {
    if (ui_->btDevice->isVisible() == false ||
        ui_->btDevice->text().simplified() == "") {
      QString btdevicename = configuration_->readFileContent("/tmp/btdevice");
      ui_->btDevice->setText(btdevicename);
      ui_->btDevice->show();
    }
  } else {
    if (ui_->btDevice->isVisible() == true) {
      ui_->btDevice->hide();
    }
  }

  // hide wifi if not forced
  if (!this->wifiButtonForce &&
      !std::ifstream("/tmp/mobile_hotspot_detected")) {
    ui_->AAWIFIWidget->hide();
    ui_->AAWIFIWidget2->hide();
  } else {
    ui_->AAUSBWidget->hide();
    ui_->AAUSBWidget2->hide();
  }

  // init alpha values
  MainWindow::updateAlpha();

  watcher_tmp = new QFileSystemWatcher(this);
  watcher_tmp->addPath("/tmp");
  connect(watcher_tmp, &QFileSystemWatcher::directoryChanged, this,
          &MainWindow::tmpChanged);

  // Experimental test code
  localDevice = new QBluetoothLocalDevice(this);

  connect(localDevice,
          SIGNAL(hostModeStateChanged(QBluetoothLocalDevice::HostMode)), this,
          SLOT(hostModeStateChanged(QBluetoothLocalDevice::HostMode)));

  // Remove all push buttons from the main window UI.
  for (auto* button : this->findChildren<QPushButton*>()) {
    button->hide();
    button->setEnabled(false);
  }

  hostModeStateChanged(localDevice->hostMode());
  updateNetworkInfo();
}

MainWindow::~MainWindow() { delete ui_; }

}  // namespace ui
}  // namespace autoapp
}  // namespace openauto
}  // namespace f1x

void f1x::openauto::autoapp::ui::MainWindow::hostModeStateChanged(
    QBluetoothLocalDevice::HostMode mode) {
  if (mode != QBluetoothLocalDevice::HostPoweredOff) {
    this->bluetoothEnabled = true;
    if (std::ifstream("/tmp/bluetooth_pairable")) {
      ui_->labelBluetoothPairable->show();
    } else {
      ui_->labelBluetoothPairable->hide();
    }
  } else {
    this->bluetoothEnabled = false;
    ui_->labelBluetoothPairable->hide();
  }
}

void f1x::openauto::autoapp::ui::MainWindow::updateNetworkInfo() {
  QNetworkInterface wlan0if = QNetworkInterface::interfaceFromName("wlan0");
  if (wlan0if.flags().testFlag(QNetworkInterface::IsUp)) {
    QList<QNetworkAddressEntry> entrieswlan0 = wlan0if.addressEntries();
    if (!entrieswlan0.isEmpty()) {
      QNetworkAddressEntry wlan0 = entrieswlan0.first();
      // qDebug() << "wlan0: " << wlan0.ip();
      ui_->value_ip->setText(wlan0.ip().toString().simplified());
      ui_->value_mask->setText(wlan0.netmask().toString().simplified());
      if (std::ifstream("/tmp/hotspot_active")) {
        ui_->value_ssid->setText(configuration_->getParamFromFile(
            "/etc/hostapd/hostapd.conf", "ssid"));
      } else {
        ui_->value_ssid->setText(
            configuration_->readFileContent("/tmp/wifi_ssid"));
      }
      ui_->value_gw->setText(
          configuration_->readFileContent("/tmp/gateway_wlan0"));
    }
  } else {
    // qDebug() << "wlan0: down";
    ui_->value_ip->setText("");
    ui_->value_mask->setText("");
    ui_->value_gw->setText("");
    ui_->value_ssid->setText("wlan0: down");
  }
}

void f1x::openauto::autoapp::ui::MainWindow::updateAlpha() {
  int value = configuration_->getAlphaTrans();
  this->alpha_current_str = value;
}

void f1x::openauto::autoapp::ui::MainWindow::setRetryUSBConnect() {
  ui_->SysinfoTopLeft->setText("Trying USB connect ...");
  ui_->SysinfoTopLeft->show();

  QTimer::singleShot(10000, this, SLOT(resetRetryUSBMessage()));
}

void f1x::openauto::autoapp::ui::MainWindow::resetRetryUSBMessage() {
  ui_->SysinfoTopLeft->setText("");
  ui_->SysinfoTopLeft->hide();
}

bool f1x::openauto::autoapp::ui::MainWindow::check_file_exist(
    const char* fileName) {
  std::ifstream ifile(fileName, std::ios::in);
  // file not ok - checking if symlink
  if (!ifile.good()) {
    QFileInfo linkFile = QString(fileName);
    if (linkFile.isSymLink()) {
      QFileInfo linkTarget(linkFile.symLinkTarget());
      return linkTarget.exists();
    } else {
      return ifile.good();
    }
  } else {
    return ifile.good();
  }
}

void f1x::openauto::autoapp::ui::MainWindow::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_Return) {
    QApplication::postEvent(
        QApplication::focusWidget(),
        new QKeyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier));
    QApplication::postEvent(
        QApplication::focusWidget(),
        new QKeyEvent(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier));
  }
  if (event->key() == Qt::Key_1) {
    QApplication::postEvent(
        QApplication::focusWidget(),
        new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier));
  }
  if (event->key() == Qt::Key_2) {
    QApplication::postEvent(
        QApplication::focusWidget(),
        new QKeyEvent(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier));
  }
  if (event->key() == Qt::Key_Escape) {
  }
}

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

  if (std::ifstream("/tmp/blankscreen")) {
    if (ui_->centralWidget->isVisible()) {
      CloseAllDialogs();
      ui_->centralWidget->hide();
    }
  } else if (!ui_->centralWidget->isVisible()) {
    ui_->centralWidget->show();
  }

  if (std::ifstream("/tmp/external_exit")) {
    f1x::openauto::autoapp::ui::MainWindow::MainWindow::exit();
  }

  updateNetworkInfo();
}

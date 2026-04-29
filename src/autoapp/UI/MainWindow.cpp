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
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <QFileInfo>
#include "ui_mainwindow.h"
#include <QTimer>
#include <QDateTime>
#include <QMessageBox>
#include <QTextStream>
#include <QFontDatabase>
#include <QFont>
#include <QScreen>
#include <QRect>
#include <QVideoWidget>
#include <QNetworkInterface>
#include <QStandardItemModel>
#include <QPushButton>
#include <iostream>
#include <fstream>
#include <cstdio>
#include <unistd.h>
#include <f1x/openauto/Common/Log.hpp>

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace ui
{

MainWindow::MainWindow(configuration::IConfiguration::Pointer configuration, QWidget *parent)
    : QMainWindow(parent)
    , ui_(new Ui::MainWindow)
    , localDevice(new QBluetoothLocalDevice)
{
    // set default bg color to black
    this->setStyleSheet("QMainWindow {background-color: rgb(0,0,0);}");

    // Set default font and size
    int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
    QString family = QFontDatabase::applicationFontFamilies(id).at(0);
    QFont _font(family, 11);
    qApp->setFont(_font);

    this->configuration_ = configuration;

    // trigger files
   
    this->devModeEnabled = check_file_exist(this->devModeFile);
    this->wifiButtonForce = check_file_exist(this->wifiButtonFile);
    this->systemDebugmode = check_file_exist(this->debugModeFile);
    this->lightsensor = check_file_exist(this->lsFile);
   


    ui_->setupUi(this);
    
    // Configure window attributes to prevent ghosting
    this->setAttribute(Qt::WA_OpaquePaintEvent, true);
    this->setAttribute(Qt::WA_NoSystemBackground, false);
    this->setAutoFillBackground(true);

    // Button click handlers removed.

    ui_->clockOnlyWidget->hide();

    ui_->labelBluetoothPairable->hide();

    // by default hide media player
    ui_->mediaWidget->hide();

    ui_->SysinfoTopLeft->hide();

    ui_->ButtonAndroidAuto->hide();

    ui_->SysinfoTopLeft2->hide();

    ui_->label_dummy_right->hide();

    ui_->dcRecording->hide();

    if (!configuration->showNetworkinfo()) {
        ui_->networkInfo->hide();
    }

    if (!this->devModeEnabled) {
        ui_->labelLock->hide();
        ui_->labelLockDummy->hide();
    }



    QTimer *timer=new QTimer(this);
    connect(timer, SIGNAL(timeout()),this,SLOT(showTime()));
    timer->start(1000);

    ui_->btDevice->hide();

    // check if a device is connected via bluetooth
    if (std::ifstream("/tmp/btdevice")) {
        if (ui_->btDevice->isVisible() == false || ui_->btDevice->text().simplified() == "") {
            QString btdevicename = configuration_->readFileContent("/tmp/btdevice");
            ui_->btDevice->setText(btdevicename);
            ui_->btDevice->show();
        }
    } else {
        if (ui_->btDevice->isVisible() == true) {
            ui_->btDevice->hide();
        }
    }

    // as default hide power buttons
    ui_->exitWidget->hide();
    ui_->horizontalWidgetPower->hide();

    // hide wifi if not forced
    if (!this->wifiButtonForce && !std::ifstream("/tmp/mobile_hotspot_detected")) {
        ui_->AAWIFIWidget->hide();
        ui_->AAWIFIWidget2->hide();
    } else {
        ui_->AAUSBWidget->hide();
        ui_->AAUSBWidget2->hide();
    }

    // show dev labels if dev mode activated
    if (!this->devModeEnabled) {
        ui_->devlabel_left->hide();
        ui_->devlabel_right->hide();
    }

    // switch to old menu if set in settings
    if (!configuration->oldGUI()) {
        this->oldGUIStyle = false;
        ui_->menuWidget->show();
        ui_->oldmenuWidget->hide();
    } else {
        this->oldGUIStyle = true;
        ui_->oldmenuWidget->show();
        ui_->menuWidget->hide();
    }



    // use big clock in classic gui?
    if (configuration->showBigClock()) {
        this->UseBigClock = true;
    } else {
        this->UseBigClock = false;
    }

    // clock viibility by settings
    if (!configuration->showClock()) {
        ui_->Digital_clock->hide();
        ui_->bigClock->hide();
        this->NoClock = true;
    } else {
        this->NoClock = false;
        if (this->UseBigClock) {
            ui_->oldmenuDummy->hide();
            ui_->bigClock->show();
            if (oldGUIStyle) {
                ui_->Digital_clock->hide();
            }
        } else {
            ui_->oldmenuDummy->show();
            ui_->Digital_clock->show();
            ui_->bigClock->hide();
        }
    }

    // hide gui toggle if enabled in settings
    (void)configuration->hideMenuToggle();

    // init alpha values
    MainWindow::updateAlpha();

    watcher_tmp = new QFileSystemWatcher(this);
    watcher_tmp->addPath("/tmp");
    connect(watcher_tmp, &QFileSystemWatcher::directoryChanged, this, &MainWindow::tmpChanged);

    // Experimental test code
    localDevice = new QBluetoothLocalDevice(this);

    connect(localDevice, SIGNAL(hostModeStateChanged(QBluetoothLocalDevice::HostMode)),
            this, SLOT(hostModeStateChanged(QBluetoothLocalDevice::HostMode)));

    // Remove all push buttons from the main window UI.
    for (auto* button : this->findChildren<QPushButton*>())
    {
        button->hide();
        button->setEnabled(false);
    }

    hostModeStateChanged(localDevice->hostMode());
    updateNetworkInfo();
}

MainWindow::~MainWindow()
{
    delete ui_;
}

}
}
}
}

void f1x::openauto::autoapp::ui::MainWindow::hostModeStateChanged(QBluetoothLocalDevice::HostMode mode)
{
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

void f1x::openauto::autoapp::ui::MainWindow::updateNetworkInfo()
{
    QNetworkInterface wlan0if = QNetworkInterface::interfaceFromName("wlan0");
    if (wlan0if.flags().testFlag(QNetworkInterface::IsUp)) {
        QList<QNetworkAddressEntry> entrieswlan0 = wlan0if.addressEntries();
        if (!entrieswlan0.isEmpty()) {
            QNetworkAddressEntry wlan0 = entrieswlan0.first();
            //qDebug() << "wlan0: " << wlan0.ip();
            ui_->value_ip->setText(wlan0.ip().toString().simplified());
            ui_->value_mask->setText(wlan0.netmask().toString().simplified());
            if (std::ifstream("/tmp/hotspot_active")) {
                ui_->value_ssid->setText(configuration_->getParamFromFile("/etc/hostapd/hostapd.conf","ssid"));
            } else {
                ui_->value_ssid->setText(configuration_->readFileContent("/tmp/wifi_ssid"));
            }
            ui_->value_gw->setText(configuration_->readFileContent("/tmp/gateway_wlan0"));
        }
    } else {
        //qDebug() << "wlan0: down";
        ui_->value_ip->setText("");
        ui_->value_mask->setText("");
        ui_->value_gw->setText("");
        ui_->value_ssid->setText("wlan0: down");
    }
}

void f1x::openauto::autoapp::ui::MainWindow::updateAlpha()
{
    int value = configuration_->getAlphaTrans();
    this->alpha_current_str = value;
}


void f1x::openauto::autoapp::ui::MainWindow::showTime()
{
    QTime time=QTime::currentTime();
    QDate date=QDate::currentDate();
    QString time_text=time.toString("hh : mm : ss");
    this->date_text=date.toString("MM/dd");

    if ((time.second() % 2) == 0) {
        time_text[3] = ' ';
        time_text[8] = ' ';
    }

    ui_->Digital_clock->setText(time_text);
    ui_->bigClock->setText(time_text);
    ui_->bigClock2->setText(time_text);


    // check connected devices
    if (localDevice->isValid()) {
        QString localDeviceName = localDevice->name();
        QString localDeviceAddress = localDevice->address().toString();
        QList<QBluetoothAddress> btdevices;
        btdevices = localDevice->connectedDevices();

        int count = btdevices.count();
        if (count > 0) {
            //QBluetoothAddress btdevice = btdevices[0];
            //QString btmac = btdevice.toString();
            //ui_->btDeviceCount->setText(QString::number(count));
            if (ui_->btDevice->isVisible() == false) {
                ui_->btDevice->show();
            }
            if (std::ifstream("/tmp/btdevice")) {
                ui_->btDevice->setText(configuration_->readFileContent("/tmp/btdevice"));
            }
        } else {
            if (ui_->btDevice->isVisible() == true) {
                ui_->btDevice->hide();
                ui_->btDevice->setText("BT-Device");
            }
        }
    }
}

void f1x::openauto::autoapp::ui::MainWindow::setRetryUSBConnect()
{
    ui_->SysinfoTopLeft->setText("Trying USB connect ...");
    ui_->SysinfoTopLeft->show();

    QTimer::singleShot(10000, this, SLOT(resetRetryUSBMessage()));
}

void f1x::openauto::autoapp::ui::MainWindow::resetRetryUSBMessage()
{
    ui_->SysinfoTopLeft->setText("");
    ui_->SysinfoTopLeft->hide();
}

bool f1x::openauto::autoapp::ui::MainWindow::check_file_exist(const char *fileName)
{
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

void f1x::openauto::autoapp::ui::MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier));
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier));
    }
    if (event->key() == Qt::Key_1) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Tab, Qt::ShiftModifier));
    }
    if (event->key() == Qt::Key_2) {
        QApplication::postEvent (QApplication::focusWidget(), new QKeyEvent ( QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier));
    }
    if(event->key() == Qt::Key_Escape)
    {
    }
}

void f1x::openauto::autoapp::ui::MainWindow::tmpChanged()
{
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

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
#include <QFile>
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
    this->cameraButtonForce = check_file_exist(this->cameraButtonFile);
    this->brightnessButtonForce = check_file_exist(this->brightnessButtonFile);
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

    if (std::ifstream("/etc/crankshaft.branch")) {
        QString branch = configuration_->readFileContent("/etc/crankshaft.branch");
        if (branch != "crankshaft-ng") {
            if (branch == "csng-dev") {
                ui_->Header_Label->setText("<html><head/><body><p><span style=' font-style:normal; color:#ffffff;'>crank</span><span style=' font-style:normal; color:#5ce739;'>shaft </span><span style=' font-style:normal; color:#40bfbf;'>NG </span><span style=' font-style:normal; color:#888a85;'>- </span><span style=' font-style:normal; color:#cc0000;'>Dev-Build</span></p></body></html>");
            } else {
                ui_->Header_Label->setText("<html><head/><body><p><span style=' font-style:normal; color:#ffffff;'>crank</span><span style=' font-style:normal; color:#5ce739;'>shaft </span><span style=' font-style:normal; color:#40bfbf;'>NG </span><span style=' font-style:normal; color:#888a85;'>- </span><span style=' font-style:normal; color:#ce5c00;'>Custom-Build</span></p></body></html>");
            }
        }
    }

    QTimer *timer=new QTimer(this);
    connect(timer, SIGNAL(timeout()),this,SLOT(showTime()));
    timer->start(1000);

    // enable connects while cam is enabled
    if (this->cameraButtonForce) {
        this->camera_ycorection=configuration->getCSValue("RPICAM_YCORRECTION").toInt();
        this->camera_zoom=configuration->getCSValue("RPICAM_ZOOM").toInt();
    }

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

    // hide brightness slider of control file is not existing
    QFileInfo brightnessFile(brightnessFilename);
    Q_UNUSED(brightnessFile);

    // as default hide brightness slider
    ui_->BrightnessSliderControl->hide();

    // as default hide volume slider player
    ui_->VolumeSliderControlPlayer->hide();

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

    // as default hide camera controls
    ui_->cameraWidget->hide();

   

    // show dev labels if dev mode activated
    if (!this->devModeEnabled) {
        ui_->devlabel_left->hide();
        ui_->devlabel_right->hide();
    }

    // set brightness slider attribs from cs config
    ui_->horizontalSliderBrightness->setMinimum(configuration->getCSValue("BR_MIN").toInt());
    ui_->horizontalSliderBrightness->setMaximum(configuration->getCSValue("BR_MAX").toInt());
    ui_->horizontalSliderBrightness->setSingleStep(configuration->getCSValue("BR_STEP").toInt());
    ui_->horizontalSliderBrightness->setTickInterval(configuration->getCSValue("BR_STEP").toInt());

    // run monitor for custom brightness command if enabled in crankshaft_env.sh
    if (std::ifstream("/tmp/custombrightness")) {
        this->customBrightnessControl = true;
    }

    // read param file
    if (std::ifstream("/boot/crankshaft/volume")) {
        // init volume
        QString vol=QString::number(configuration_->readFileContent("/boot/crankshaft/volume").toInt());
        ui_->volumeValueLabel->setText(vol+"%");
        ui_->horizontalSliderVolume->setValue(vol.toInt());
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

    // hide brightness button if eanbled in settings
    if (configuration->hideBrightnessControl()) {
        ui_->BrightnessSliderControl->hide();
    }

    // init alpha values
    MainWindow::updateAlpha();

    // Hide auto day/night if needed
    if (this->lightsensor) {
        ui_->BrightnessSliderControl->hide();
        ui_->VolumeSliderControl->hide();
    }

    player = new QMediaPlayer(this);
    playlist = new QMediaPlaylist(this);
    connect(player, &QMediaPlayer::positionChanged, this, &MainWindow::on_positionChanged);
    connect(player, &QMediaPlayer::durationChanged, this, &MainWindow::on_durationChanged);
    connect(player, &QMediaPlayer::metaDataAvailableChanged, this, &MainWindow::metaDataChanged);
    connect(player, &QMediaPlayer::stateChanged, this, &MainWindow::on_StateChanged);

    ui_->PlayerPlayingWidget->hide();

    this->musicfolder = QString::fromStdString(configuration->getMp3MasterPath());
    this->albumfolder = QString::fromStdString(configuration->getMp3SubFolder());

    ui_->labelFolderpath->setText(this->musicfolder);
    ui_->labelAlbumpath->setText(this->albumfolder);

    ui_->labelFolderpath->hide();
    ui_->labelAlbumpath->hide();
    ui_->comboBoxAlbum->hide();

    MainWindow::scanFolders();
    ui_->comboBoxAlbum->setCurrentText(QString::fromStdString(configuration->getMp3SubFolder()));
    MainWindow::scanFiles();
    player->setPlaylist(this->playlist);
    ui_->mp3List->setCurrentRow(configuration->getMp3Track());
    this->currentPlaylistIndex = configuration->getMp3Track();

    if (configuration->mp3AutoPlay()) {
        MainWindow::playerShow();
        MainWindow::playerHide();
        if (configuration->showAutoPlay()) {
            MainWindow::playerShow();
        }
    }

    watcher = new QFileSystemWatcher(this);
    watcher->addPath("/media/USBDRIVES");
    connect(watcher, &QFileSystemWatcher::directoryChanged, this, &MainWindow::setTrigger);

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

void f1x::openauto::autoapp::ui::MainWindow::on_horizontalSliderBrightness_valueChanged(int value)
{
    int n = snprintf(this->brightness_str, 5, "%d", value);
    this->brightnessFile = new QFile(this->brightnessFilename);
    this->brightnessFileAlt = new QFile(this->brightnessFilenameAlt);

    if (!this->customBrightnessControl) {
        if (this->brightnessFile->open(QIODevice::WriteOnly)) {
            this->brightness_str[n] = '\n';
            this->brightness_str[n+1] = '\0';
            this->brightnessFile->write(this->brightness_str);
            this->brightnessFile->close();
        }
    } else {
        if (this->brightnessFileAlt->open(QIODevice::WriteOnly)) {
            this->brightness_str[n] = '\n';
            this->brightness_str[n+1] = '\0';
            this->brightnessFileAlt->write(this->brightness_str);
            this->brightnessFileAlt->close();
        }
    }
    QString bri=QString::number(value);
    ui_->brightnessValueLabel->setText(bri);
}

void f1x::openauto::autoapp::ui::MainWindow::on_horizontalSliderVolume_valueChanged(int value)
{
    QString vol=QString::number(value);
    ui_->volumeValueLabel->setText(vol+"%");
    system(("/usr/local/bin/autoapp_helper setvolume " + std::to_string(value) + "&").c_str());
}

void f1x::openauto::autoapp::ui::MainWindow::updateAlpha()
{
    int value = configuration_->getAlphaTrans();
    this->alpha_current_str = value;
}


void f1x::openauto::autoapp::ui::MainWindow::cameraControlHide()
{
    if (this->cameraButtonForce) {
        ui_->cameraWidget->hide();
        if (!this->oldGUIStyle) {
            ui_->menuWidget->show();
        } else {
            ui_->oldmenuWidget->show();
        }
    }
}

void f1x::openauto::autoapp::ui::MainWindow::cameraControlShow()
{
    if (this->cameraButtonForce) {
        if (!this->oldGUIStyle) {
            ui_->menuWidget->hide();
        } else {
            ui_->oldmenuWidget->hide();
        }
        ui_->cameraWidget->show();
    }
}

void f1x::openauto::autoapp::ui::MainWindow::playerShow()
{

    if (!this->oldGUIStyle) {
        ui_->menuWidget->hide();
    } else {
        ui_->oldmenuWidget->hide();
    }
    ui_->mediaWidget->show();
    ui_->VolumeSliderControlPlayer->show();
    ui_->VolumeSliderControl->hide();
    ui_->BrightnessSliderControl->hide();
    ui_->networkInfo->hide();
    ui_->Info->hide();
    ui_->horizontalSliderProgressPlayer->hide();
    ui_->VolumeSliderControlPlayer->hide();
}

void f1x::openauto::autoapp::ui::MainWindow::playerHide()
{
    if (!this->oldGUIStyle) {
        ui_->menuWidget->show();
    } else {
        ui_->oldmenuWidget->show();
    }
    ui_->mediaWidget->hide();
    ui_->VolumeSliderControl->show();
    ui_->VolumeSliderControlPlayer->hide();
    ui_->BrightnessSliderControl->hide();
    if (configuration_->showNetworkinfo()) {
        ui_->networkInfo->hide();
    }

    f1x::openauto::autoapp::ui::MainWindow::tmpChanged();
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

void f1x::openauto::autoapp::ui::MainWindow::on_horizontalSliderProgressPlayer_sliderMoved(int position)
{
    player->setPosition(position);
}

void f1x::openauto::autoapp::ui::MainWindow::on_horizontalSliderVolumePlayer_sliderMoved(int position)
{
    player->setVolume(position);
    ui_->volumeValueLabelPlayer->setText(QString::number(position) + "%");
}

void f1x::openauto::autoapp::ui::MainWindow::on_positionChanged(qint64 position)
{
    ui_->horizontalSliderProgressPlayer->setValue(position);

    //Setting the time
    QString time_elapsed, time_total;

    int total_seconds, total_minutes;

    total_seconds = (player->duration()/1000) % 60;
    total_minutes = (player->duration()/1000) / 60;

    if(total_minutes >= 60){
        int total_hours = (total_minutes/60);
        total_minutes = total_minutes - (total_hours*60);
        time_total = QString("%1").arg(total_hours, 2,10,QChar('0')) + ':' + QString("%1").arg(total_minutes, 2,10,QChar('0')) + ':' + QString("%1").arg(total_seconds, 2,10,QChar('0'));

    }else{
        time_total = QString("%1").arg(total_minutes, 2,10,QChar('0')) + ':' + QString("%1").arg(total_seconds, 2,10,QChar('0'));

    }

    //calculate time elapsed
    int seconds, minutes;

    seconds = (position/1000) % 60;
    minutes = (position/1000) / 60;

    //if minutes is over 60 then we should really display hours
    if(minutes >= 60){
        int hours = (minutes/60);
        minutes = minutes - (hours*60);
        time_elapsed = QString("%1").arg(hours, 2,10,QChar('0')) + ':' + QString("%1").arg(minutes, 2,10,QChar('0')) + ':' + QString("%1").arg(seconds, 2,10,QChar('0'));
    }else{
        time_elapsed = QString("%1").arg(minutes, 2,10,QChar('0')) + ':' + QString("%1").arg(seconds, 2,10,QChar('0'));
    }
    ui_->playerPositionTime->setText(time_elapsed + " / " + time_total);
}

void f1x::openauto::autoapp::ui::MainWindow::on_durationChanged(qint64 position)
{
    ui_->horizontalSliderProgressPlayer->setMaximum(position);
}

void f1x::openauto::autoapp::ui::MainWindow::on_mp3List_itemClicked(QListWidgetItem *item)
{
    this->selectedMp3file = item->text();
}

void f1x::openauto::autoapp::ui::MainWindow::metaDataChanged()
{
    QString fullpathplaying = player->currentMedia().request().url().toString();

    try {
        // use metadata from mp3list widget (prescanned id3 by taglib)
        if (playlist->currentIndex() != -1 && fullpathplaying != "") {
            QString currentsong = ui_->mp3List->item(playlist->currentIndex())->text();
            ui_->labelCurrentPlaying->setText(currentsong);
            if (currentsong.length() > 48) {
                int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
                QString family = QFontDatabase::applicationFontFamilies(id).at(0);
                QFont _font(family, 12, QFont::Bold);
                _font.setItalic(true);
                ui_->labelCurrentPlaying->setFont(_font);
            } else {
                int id = QFontDatabase::addApplicationFont(":/Roboto-Regular.ttf");
                QString family = QFontDatabase::applicationFontFamilies(id).at(0);
                QFont _font(family, 16, QFont::Bold);
                _font.setItalic(true);
                ui_->labelCurrentPlaying->setFont(_font);
            }
        }
    } catch (...) {
        // use metadata from player
        QString AlbumInterpret = player->metaData(QMediaMetaData::AlbumArtist).toString();
        QString Title = player->metaData(QMediaMetaData::Title).toString();

        if (AlbumInterpret == "" && ui_->comboBoxAlbum->currentText() != ".") {
            AlbumInterpret = ui_->comboBoxAlbum->currentText();
        }
        QString currentPlaying;

        if (AlbumInterpret != "") {
            currentPlaying.append(AlbumInterpret);
        }
        if (Title != "" && AlbumInterpret != "") {
            currentPlaying.append(" - ");
        }
        if (Title != "") {
            currentPlaying.append(Title);
        }
        ui_->labelCurrentPlaying->setText(currentPlaying);
    }
    ui_->labelTrack->setText(QString::number(playlist->currentIndex()+1));
    ui_->labelTrackCount->setText(QString::number(playlist->mediaCount()));

    if (playlist->currentIndex() == -1) {
        ui_->labelCurrentPlaying->setText(ui_->comboBoxAlbum->currentText());
    }

    // Write current playing album and track to config
    this->configuration_->setMp3Track(playlist->currentIndex());
    this->configuration_->setMp3SubFolder(ui_->comboBoxAlbum->currentText().toStdString());
    this->configuration_->save();
}

void f1x::openauto::autoapp::ui::MainWindow::on_comboBoxAlbum_currentIndexChanged(const QString &arg1)
{
    this->albumfolder = arg1;
    MainWindow::scanFiles();
    ui_->labelCurrentPlaying->setText("");
    ui_->playerPositionTime->setText("");
}

void f1x::openauto::autoapp::ui::MainWindow::setTrigger()
{
    this->mediacontentchanged = true;

    ui_->SysinfoTopLeft->setText("Media changed - Scanning ...");
    ui_->SysinfoTopLeft->show();

    QTimer::singleShot(10000, this, SLOT(scanFolders()));
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

void f1x::openauto::autoapp::ui::MainWindow::scanFolders()
{
    try {
        if (this->mediacontentchanged == true) {
            this->mediacontentchanged = false;
            int cleaner = ui_->comboBoxAlbum->count();
            while (cleaner > -1) {
                ui_->comboBoxAlbum->removeItem(cleaner);
                cleaner--;
            }
            QDir directory(this->musicfolder);
            QStringList folders = directory.entryList(QStringList() << "*", QDir::AllDirs, QDir::Name);
            QStandardItemModel *model = new QStandardItemModel(this);
            foreach (QString foldername, folders) {
                if (foldername != ".." and foldername != ".") {
                    ui_->comboBoxAlbum->addItem(foldername);
                    ui_->labelAlbumCount->setText(QString::number(ui_->comboBoxAlbum->count()));                 

                    QString coverpng = this->musicfolder + "/" + foldername + "/folder.png";
                    QString coverjpg = this->musicfolder + "/" + foldername + "/folder.jpg";
                    QString coverpngcs = "/media/USBDRIVES/CSSTORAGE/COVERCACHE/" + foldername + ".png";
                    QString coverjpgcs = "/media/USBDRIVES/CSSTORAGE/COVERCACHE/" + foldername + ".jpg";

                    if (check_file_exist(coverpng.toStdString().c_str())) {
                        QPixmap img = coverpng;
                        QStandardItem *item = new QStandardItem(QIcon(img.scaled(270,270,Qt::KeepAspectRatio)),foldername);
                        model->setItem(ui_->comboBoxAlbum->count(),0,item);
                    } else if (check_file_exist(coverjpg.toStdString().c_str())) {
                        QPixmap img = coverjpg;
                        QStandardItem *item = new QStandardItem(QIcon(img.scaled(270,270,Qt::KeepAspectRatio)),foldername);
                        model->setItem(ui_->comboBoxAlbum->count(),0,item);
                    } else if (check_file_exist(coverpngcs.toStdString().c_str())) {
                        QPixmap img = coverpngcs;
                        QStandardItem *item = new QStandardItem(QIcon(img.scaled(270,270,Qt::KeepAspectRatio)),foldername);
                        model->setItem(ui_->comboBoxAlbum->count(),0,item);
                    } else if (check_file_exist(coverjpgcs.toStdString().c_str())) {
                        QPixmap img = coverjpgcs;
                        QStandardItem *item = new QStandardItem(QIcon(img.scaled(270,270,Qt::KeepAspectRatio)),foldername);
                        model->setItem(ui_->comboBoxAlbum->count(),0,item);
                    } else {
                        QStandardItem *item = new QStandardItem(QIcon(":/coverlogo.png"),foldername);
                        model->setItem(ui_->comboBoxAlbum->count(),0,item);
                    }
                }
            }
            ui_->AlbumCoverListView->setModel(model);
            this->currentPlaylistIndex = 0;
            ui_->SysinfoTopLeft->hide();
        }
    }
    catch(...) {
        ui_->SysinfoTopLeft->hide();
    }
    ui_->mp3List->hide();
}

void f1x::openauto::autoapp::ui::MainWindow::scanFiles()
{
    if (this->mediacontentchanged == false) {
        int cleaner = ui_->mp3List->count();
        while (cleaner > -1) {
            ui_->mp3List->takeItem(cleaner);
            cleaner--;
        }
        this->playlist->clear();

        QList<QMediaContent> content;
        QDir directory(this->musicfolder + "/" + this->albumfolder);
        QStringList mp3s = directory.entryList(QStringList() << "*.mp3" << "*.flac" << "*.aac" << "*.ogg" << "*.mp4" << "*.mp4a" << "*.wma" << "*.strm",QDir::Files, QDir::Name);
        foreach (QString filename, mp3s) {
            // add to mediacontent
            if (filename.endsWith(".strm")) {
                QString url=configuration_->readFileContent(this->musicfolder + "/" + this->albumfolder + "/" + filename);
                content.push_back(QMediaContent(QUrl(url)));
                ui_->mp3List->addItem(filename.replace(".strm",""));
            } else {
                // add items to gui
                content.push_back(QMediaContent(QUrl::fromLocalFile(this->musicfolder + "/" + this->albumfolder + "/" + filename)));
                // read metadata using taglib
                try {
                    TagLib::FileRef file((this->musicfolder + "/" + this->albumfolder + "/" + filename).toUtf8(),true);
                    TagLib::String artist_string = file.tag()->artist();
                    TagLib::String title_string = file.tag()->title();
                    unsigned int track_string = file.tag()->track();
                    QString artistid3 = QString::fromStdWString(artist_string.toCWString());
                    QString titleid3 = QString::fromStdWString(title_string.toCWString());
                    QString trackid3 = QString::number(track_string);
                    int tracklength = trackid3.length();
                    if (tracklength < 2) {
                        trackid3 = "0" + trackid3;
                    }
                    QString ID3Entry = trackid3 + ": " + artistid3 + " - " + titleid3;
                    ui_->mp3List->addItem(ID3Entry);
                } catch (...) {
                    // old way only adding filename to list
                    ui_->mp3List->addItem(filename);
                }
            }
        }
        // set playlist
        this->playlist->addMedia(content);
    }
}

void f1x::openauto::autoapp::ui::MainWindow::on_mp3List_currentRowChanged(int currentRow)
{
    ui_->labelFolderpath->setText(QString::number(currentRow));
    this->currentPlaylistIndex = currentRow;
}

void f1x::openauto::autoapp::ui::MainWindow::on_StateChanged(QMediaPlayer::State state)
{
    if (state == QMediaPlayer::StoppedState || state == QMediaPlayer::PausedState) {
        std::remove("/tmp/media_playing");
    } else {
        std::ofstream("/tmp/media_playing");
    }
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

void f1x::openauto::autoapp::ui::MainWindow::on_AlbumCoverListView_clicked(const QModelIndex &index)
{
    QString foldertext = index.data(Qt::DisplayRole).toString();
    ui_->labelAlbumpath->setText(foldertext);
    ui_->comboBoxAlbum->setCurrentIndex(ui_->comboBoxAlbum->findText(foldertext));

    ui_->mp3List->show();
    ui_->AlbumCoverListView->hide();
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

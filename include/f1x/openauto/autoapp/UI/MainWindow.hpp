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

#include <memory>
#include <QMainWindow>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>

#include <QFileSystemWatcher>
#include <QKeyEvent>

#include <QBluetoothLocalDevice>
//#include <QtBluetooth>

namespace Ui
{
class MainWindow;
}

namespace f1x
{
namespace openauto
{
namespace autoapp
{
namespace ui
{

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(configuration::IConfiguration::Pointer configuration, QWidget *parent = nullptr);
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
    void openUpdateDialog();
    void showAlphaSlider();
    void TriggerAppStart();
    void TriggerAppStop();
    void CloseAllDialogs();

private slots:
    void updateAlpha();

private slots:
 
    void tmpChanged();
    void setRetryUSBConnect();
    void resetRetryUSBMessage();
    void updateNetworkInfo();
    bool check_file_exist(const char *filename);
    void hostModeStateChanged(QBluetoothLocalDevice::HostMode);

private:
    Ui::MainWindow* ui_;
    configuration::IConfiguration::Pointer configuration_;

    int alpha_current_str;
    QString bversion;
    QString bdate;

    char wifiButtonFile[32] = "/etc/button_wifi_visible";
    char debugModeFile[32] = "/tmp/usb_debug_mode";
    char lsFile[32] = "/etc/cs_lightsensor";

    char custom_button_file_c1[26] = "/boot/crankshaft/button_1";
    char custom_button_file_c2[26] = "/boot/crankshaft/button_2";
    char custom_button_file_c3[26] = "/boot/crankshaft/button_3";
    char custom_button_file_c4[26] = "/boot/crankshaft/button_4";
    char custom_button_file_c5[26] = "/boot/crankshaft/button_5";
    char custom_button_file_c6[26] = "/boot/crankshaft/button_6";

    QString custom_button_command_c1;
    QString custom_button_command_c2;
    QString custom_button_command_c3;
    QString custom_button_command_c4;
    QString custom_button_command_c5;
    QString custom_button_command_c6;

    QString custom_button_color_c1 = "186,189,192";
    QString custom_button_color_c2 = "186,189,192";
    QString custom_button_color_c3 = "186,189,192";
    QString custom_button_color_c4 = "186,189,192";
    QString custom_button_color_c5 = "186,189,192";
    QString custom_button_color_c6 = "186,189,192";

    bool wifiButtonForce = false;


    bool exitMenuVisible = false;

    bool rearCamEnabled = false;
    bool rearCamVisible = false;

    bool dashCamRecording = false;
    bool systemDebugmode = false;

    bool bluetoothEnabled = false;

    bool toggleMute = false;

    bool hotspotActive = false;
    bool background_set = false;

    bool lightsensor = false;

    bool csmtupdate = false;
    bool udevupdate = false;
    bool openautoupdate = false;
    bool systemupdate = false;

    QBluetoothLocalDevice *localDevice;

protected:
    void keyPressEvent(QKeyEvent *event);

};

}
}
}
}

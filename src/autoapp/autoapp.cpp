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

#include <thread>
#include <algorithm>
#include <cctype>
#include <string>
#include <fstream>
#include <optional>
#include <QApplication>
#include <QScreen>
#include <QDesktopWidget>
#include <QMetaObject>
#include <aasdk/USB/USBHub.hpp>
#include <aasdk/USB/ConnectedAccessoriesEnumerator.hpp>
#include <aasdk/USB/AccessoryModeQueryChain.hpp>
#include <aasdk/USB/AccessoryModeQueryChainFactory.hpp>
#include <aasdk/USB/AccessoryModeQueryFactory.hpp>
#include <aasdk/TCP/TCPWrapper.hpp>
#include <aap_protobuf/service/media/sink/message/KeyCode.pb.h>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/utility/setup.hpp>
#include <f1x/openauto/autoapp/App.hpp>
#include <f1x/openauto/autoapp/Configuration/IConfiguration.hpp>
#include <f1x/openauto/autoapp/Configuration/RecentAddressesList.hpp>
#include <f1x/openauto/autoapp/MQTT/MQTTPublisher.hpp>
#include <f1x/openauto/autoapp/Service/AndroidAutoEntityFactory.hpp>
#include <f1x/openauto/autoapp/Service/ServiceFactory.hpp>
#include <f1x/openauto/autoapp/Configuration/Configuration.hpp>
#include <f1x/openauto/autoapp/UI/MainWindow.hpp>
#include <f1x/openauto/autoapp/UI/SettingsWindow.hpp>
#include <f1x/openauto/autoapp/UI/ConnectDialog.hpp>
#include <f1x/openauto/autoapp/UI/WarningDialog.hpp>
#include <f1x/openauto/autoapp/UI/UpdateDialog.hpp>
#include <f1x/openauto/Common/Log.hpp>

namespace autoapp = f1x::openauto::autoapp;
using ThreadPool = std::vector<std::thread>;

namespace {

std::optional<boost::log::trivial::severity_level> parseLogLevel(const std::string& value)
{
    std::string normalized = value;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "trace") {
        return boost::log::trivial::trace;
    }
    if (normalized == "debug") {
        return boost::log::trivial::debug;
    }
    if (normalized == "info") {
        return boost::log::trivial::info;
    }
    if (normalized == "warning") {
        return boost::log::trivial::warning;
    }
    if (normalized == "error") {
        return boost::log::trivial::error;
    }
    if (normalized == "fatal") {
        return boost::log::trivial::fatal;
    }

    return std::nullopt;
}

const char* logLevelToString(const boost::log::trivial::severity_level level)
{
    switch (level) {
        case boost::log::trivial::trace: return "trace";
        case boost::log::trivial::debug: return "debug";
        case boost::log::trivial::info: return "info";
        case boost::log::trivial::warning: return "warning";
        case boost::log::trivial::error: return "error";
        case boost::log::trivial::fatal: return "fatal";
        default: return "unknown";
    }
}

std::optional<boost::log::trivial::severity_level> parseLogLevelFromArgs(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);

        if (arg == "--log_level") {
            if (i + 1 < argc) {
                return parseLogLevel(argv[i + 1]);
            }

            OPENAUTO_LOG(warning) << "[OpenAuto] --log_level provided without a value.";
            return std::nullopt;
        }

        const std::string prefix = "--log_level=";
        if (arg.compare(0, prefix.size(), prefix) == 0) {
            return parseLogLevel(arg.substr(prefix.size()));
        }
    }

    return std::nullopt;
}

} // namespace

void startUSBWorkers(boost::asio::io_service& ioService, libusb_context* usbContext, ThreadPool& threadPool)
{
    auto usbWorker = [&ioService, usbContext]() {
        timeval libusbEventTimeout{180, 0};

        while(!ioService.stopped())
        {
            libusb_handle_events_timeout_completed(usbContext, &libusbEventTimeout, nullptr);
        }
    };

    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
    threadPool.emplace_back(usbWorker);
}

void startIOServiceWorkers(boost::asio::io_service& ioService, ThreadPool& threadPool)
{
    auto ioServiceWorker = [&ioService]() {
        ioService.run();
    };

    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
    threadPool.emplace_back(ioServiceWorker);
}

void configureLogging(int argc, char* argv[]) {
    boost::log::core::get()->set_filter(boost::log::trivial::severity >= boost::log::trivial::warning);

    const std::string logIni = "openauto-logs.ini";
    std::ifstream logSettings(logIni);
    if (logSettings.good()) {
        try {
            // For boost < 1.71 the severity types are not automatically parsed so lets register them.
            boost::log::register_simple_filter_factory<boost::log::trivial::severity_level>("Severity");
            boost::log::register_simple_formatter_factory<boost::log::trivial::severity_level, char>("Severity");
            boost::log::init_from_stream(logSettings);
        } catch (std::exception const & e) {
            OPENAUTO_LOG(warning) << "[OpenAuto] " << logIni << " was provided but was not valid.";
        }
    }

    const auto cliLogLevel = parseLogLevelFromArgs(argc, argv);
    if (cliLogLevel.has_value()) {
        boost::log::core::get()->set_filter(boost::log::trivial::severity >= cliLogLevel.value());
        OPENAUTO_LOG(info) << "[OpenAuto] CLI log level override applied: "
                           << logLevelToString(cliLogLevel.value());
    }
}

int main(int argc, char* argv[])
{
    configureLogging(argc, argv);

    libusb_context* usbContext;
    if(libusb_init(&usbContext) != 0)
    {
        OPENAUTO_LOG(error) << "[AutoApp] libusb_init failed.";
        return 1;
    }

    boost::asio::io_service ioService;
    boost::asio::io_service::work work(ioService);
    std::vector<std::thread> threadPool;
    startUSBWorkers(ioService, usbContext, threadPool);
    startIOServiceWorkers(ioService, threadPool);

    QApplication qApplication(argc, argv);
    int width = QApplication::desktop()->width();
    int height = QApplication::desktop()->height();

    for (QScreen *screen : qApplication.screens()) {
      OPENAUTO_LOG(info) << "[AutoApp] Screen name: " << screen->name().toStdString();
      OPENAUTO_LOG(info) << "[AutoApp] Screen geometry: " << screen->geometry().width(); // This includes position and size
      OPENAUTO_LOG(info) << "[AutoApp] Screen physical size: " << screen->physicalSize().width(); // Size in millimeters
    }

    QScreen *primaryScreen = QGuiApplication::primaryScreen();

    // Check if a primary screen was found
    if (primaryScreen) {
      // Get the geometry of the primary screen
      QRect screenGeometry = primaryScreen->geometry();
      width = screenGeometry.width();
      height = screenGeometry.height();
      OPENAUTO_LOG(info) << "[AutoApp] Using gemoetry from primary screen.";
    } else {
      OPENAUTO_LOG(info) << "[AutoApp] Unable to find primary screen, using default values.";
    }

    OPENAUTO_LOG(info) << "[AutoApp] Display width: " << width;
    OPENAUTO_LOG(info) << "[AutoApp] Display height: " << height;

    auto configuration = std::make_shared<autoapp::configuration::Configuration>();

    autoapp::ui::MainWindow mainWindow(configuration);
    //mainWindow.setWindowFlags(Qt::WindowStaysOnTopHint);

    autoapp::ui::SettingsWindow settingsWindow(configuration);
    //settingsWindow.setWindowFlags(Qt::WindowStaysOnTopHint);

    settingsWindow.setFixedSize(width, height);
    settingsWindow.adjustSize();

    autoapp::configuration::RecentAddressesList recentAddressesList(7);
    recentAddressesList.read();

    aasdk::tcp::TCPWrapper tcpWrapper;
    autoapp::ui::ConnectDialog connectdialog(ioService, tcpWrapper, recentAddressesList);
    //connectdialog.setWindowFlags(Qt::WindowStaysOnTopHint);
    connectdialog.move((width - 500)/2,(height-300)/2);

    autoapp::ui::WarningDialog warningdialog;
    //warningdialog.setWindowFlags(Qt::WindowStaysOnTopHint);
    warningdialog.move((width - 500)/2,(height-300)/2);

    autoapp::ui::UpdateDialog updatedialog;
    //updatedialog.setWindowFlags(Qt::WindowStaysOnTopHint);
    updatedialog.setFixedSize(500, 260);
    updatedialog.move((width - 500)/2,(height-260)/2);

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::exit, []() { system("touch /tmp/shutdown"); std::exit(0); });
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::reboot, []() { system("touch /tmp/reboot"); std::exit(0); });
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::showFullScreen);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::show_tab1);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openSettings, &settingsWindow, &autoapp::ui::SettingsWindow::loadSystemValues);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openConnectDialog, &connectdialog, &autoapp::ui::ConnectDialog::loadClientList);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openConnectDialog, &connectdialog, &autoapp::ui::ConnectDialog::exec);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openUpdateDialog, &updatedialog, &autoapp::ui::UpdateDialog::updateCheck);
    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::openUpdateDialog, &updatedialog, &autoapp::ui::UpdateDialog::exec);

    if (configuration->showCursor() == false) {
        qApplication.setOverrideCursor(Qt::BlankCursor);
    } else {
        qApplication.setOverrideCursor(Qt::ArrowCursor);
    }

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraHide, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py Background &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Background.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraShow, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py Foreground &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Foreground.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraPosYUp, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py PosYUp &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera PosY up.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraPosYDown, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py PosYDown &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera PosY down.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraZoomPlus, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py ZoomPlus &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Zoom plus.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraZoomMinus, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py ZoomMinus &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Zoom minus.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraRecord, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py Record &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Record.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraStop, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py Stop &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Stop.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::cameraSave, [&qApplication]() {
        system("/opt/crankshaft/cameracontrol.py Save &");
        OPENAUTO_LOG(debug) << "[AutoApp] Camera Save.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::TriggerScriptNight, [&qApplication]() {
        autoapp::mqtt::publishNightModeState(true);
        OPENAUTO_LOG(debug) << "[AutoApp] MainWindow Night.";
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::TriggerScriptDay, [&qApplication]() {
        autoapp::mqtt::publishNightModeState(false);
        OPENAUTO_LOG(debug) << "[AutoApp] MainWindow Day.";
    });

    const int windowedWidth = std::min(width, std::max(800, (width * 9) / 10));
    const int windowedHeight = std::min(height, std::max(480, (height * 9) / 10));
    mainWindow.resize(windowedWidth, windowedHeight);
    mainWindow.move(std::max(0, (width - windowedWidth) / 2), std::max(0, (height - windowedHeight) / 2));
    mainWindow.show();
    mainWindow.adjustSize();

    aasdk::usb::USBWrapper usbWrapper(usbContext);
    aasdk::usb::AccessoryModeQueryFactory queryFactory(usbWrapper, ioService);
    aasdk::usb::AccessoryModeQueryChainFactory queryChainFactory(usbWrapper, ioService, queryFactory);
    autoapp::service::ServiceFactory serviceFactory(ioService, configuration);
    autoapp::service::AndroidAutoEntityFactory androidAutoEntityFactory(ioService, configuration, serviceFactory);
    autoapp::mqtt::NightModeStateSubscriber nightModeSubscriber([&mainWindow](bool active) {
        QMetaObject::invokeMethod(&mainWindow, [&mainWindow, active]() {
            
        }, Qt::QueuedConnection);
    });
    nightModeSubscriber.start();

    auto usbHub(std::make_shared<aasdk::usb::USBHub>(usbWrapper, ioService, queryChainFactory));
    auto connectedAccessoriesEnumerator(std::make_shared<aasdk::usb::ConnectedAccessoriesEnumerator>(usbWrapper, ioService, queryChainFactory));
    auto app = std::make_shared<autoapp::App>(ioService, usbWrapper, tcpWrapper, androidAutoEntityFactory, std::move(usbHub), std::move(connectedAccessoriesEnumerator));

    autoapp::mqtt::MediaPlayerCommandSubscriber mediaPlayerCommandSubscriber([&app](const std::string &command) {
        if (command == "play") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY);
            return;
        }

        if (command == "stop") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PAUSE);
            return;
        }

        if (command == "pause") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PAUSE);
            return;
        }

        if (command == "next") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_NEXT);
            return;
        }

        if (command == "prev") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PREVIOUS);
            return;
        }

        if (command == "toggle") {
            app->sendAndroidMediaButton(aap_protobuf::service::media::sink::message::KeyCode::KEYCODE_MEDIA_PLAY_PAUSE);
            return;
        }

        OPENAUTO_LOG(warning) << "[AutoApp] Unknown media command received: " << command;
    });
    mediaPlayerCommandSubscriber.start();

#if 1
    app->setConnectionStateHandler([&mainWindow, width, height](bool connected) {
        autoapp::mqtt::publishConnectionState(connected);

        QMetaObject::invokeMethod(&mainWindow, [&mainWindow, width, height, connected]() {
            if (connected) {
                mainWindow.showFullScreen();
                OPENAUTO_LOG(info) << "[AutoApp] Switched main window to fullscreen after connection.";
            } else {
                const int windowedWidth = std::min(width, std::max(800, (width * 9) / 10));
                const int windowedHeight = std::min(height, std::max(480, (height * 9) / 10));
                mainWindow.showNormal();
                mainWindow.resize(windowedWidth, windowedHeight);
                mainWindow.move(std::max(0, (width - windowedWidth) / 2), std::max(0, (height - windowedHeight) / 2));
                OPENAUTO_LOG(info) << "[AutoApp] Restored main window to normal mode after disconnect.";
            }
            mainWindow.adjustSize();
        }, Qt::QueuedConnection);
    });
#endif
    QObject::connect(&connectdialog, &autoapp::ui::ConnectDialog::connectionSucceed, [&app](auto socket) {
        app->start(std::move(socket));
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::TriggerAppStart, [&app]() {
        OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStart: Manual start android auto.";
        try {
            app->disableAutostartEntity = false;
            app->resume();
            app->waitForUSBDevice();
        } catch (...) {
            OPENAUTO_LOG(error) << "[AutoApp] TriggerAppStart: app->waitForUSBDevice();";
        }
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::TriggerAppStop, [&app]() {
        try {
             OPENAUTO_LOG(debug) << "QObject::connect TriggerAppStop";
            if (std::ifstream("/tmp/android_device")) {
                OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop usb android auto.";
                app->disableAutostartEntity = true;
                system("/usr/local/bin/autoapp_helper usbreset");
                usleep(500000);
                try {
                    app->stop();
                    //app->pause();
                } catch (...) {
                    OPENAUTO_LOG(error) << "[AutoApp] TriggerAppStop: stop();";
                }

            } else {
                OPENAUTO_LOG(debug) << "[AutoApp] TriggerAppStop: Manual stop wifi android auto.";
                try {
                    app->onAndroidAutoQuit();
                    //app->pause();
                } catch (...) {
                    OPENAUTO_LOG(error) << "[Autoapp] TriggerAppStop: stop();";
                }

            }
        } catch (...) {
            OPENAUTO_LOG(error) << "[AutoApp] Exception in manual stop android auto.";
        }
    });

    QObject::connect(&mainWindow, &autoapp::ui::MainWindow::CloseAllDialogs, [&settingsWindow, &connectdialog, &updatedialog, &warningdialog]() {
        settingsWindow.close();
        connectdialog.close();
        warningdialog.close();
        updatedialog.close();
        OPENAUTO_LOG(debug) << "[AutoApp] Close all possible open dialogs.";
    });

    if (configuration->hideWarning() == false) {
        warningdialog.show();
    }

    app->waitForUSBDevice();

    auto result = qApplication.exec();
    mediaPlayerCommandSubscriber.stop();
    nightModeSubscriber.stop();

    std::for_each(threadPool.begin(), threadPool.end(), std::bind(&std::thread::join, std::placeholders::_1));

    libusb_exit(usbContext);
    return result;
}

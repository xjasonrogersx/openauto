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

#include <fstream>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <typeinfo>

#include <f1x/openauto/autoapp/MQTT/MQTTPublisher.hpp>
#include <f1x/openauto/autoapp/Projection/QtVideoOutput.hpp>
#ifdef USE_OMX
#include <f1x/openauto/autoapp/Projection/OMXVideoOutput.hpp>
#endif
#include <f1x/openauto/autoapp/Service/MediaSink/VideoMediaSinkService.hpp>

namespace {
std::string envValueOrUnset(const char *name) {
  const char *value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return "<unset>";
  }

  return std::string(value);
}

void publishVideoDebug(aasdk::messenger::ChannelId channelId,
                       const std::string &event,
                       const std::string &details = {}) {
  std::ostringstream message;
  message << "channel=" << aasdk::messenger::channelIdToString(channelId);
  if (!details.empty()) {
    message << ", " << details;
  }

  ::f1x::openauto::autoapp::mqtt::publishDebugMessage("video", event, message.str());
}
}  // namespace

namespace f1x {
  namespace openauto {
    namespace autoapp {
      namespace service {
        namespace mediasink {
          VideoMediaSinkService::VideoMediaSinkService(boost::asio::io_service &ioService,
                                                       aasdk::channel::mediasink::video::IVideoMediaSinkService::Pointer channel,
                                                       projection::IVideoOutput::Pointer videoOutput)
              : strand_(ioService), channel_(std::move(channel)), videoOutput_(std::move(videoOutput)), session_(-1),
                decodeBackend_("unknown"), decodeBackendDetails_("n/a"),
                decoderElementName_("unknown"), decoderDetectionMethod_("none"),
                backendBuffersPackets_(false), backendLikelyHardwareDecode_(false),
                telemetryWindowStart_(std::chrono::steady_clock::now()), telemetryPackets_(0), telemetryBytes_(0),
                telemetryTimestampedPackets_(0), firstTimestampInWindow_(0), lastTimestampInWindow_(0) {
            this->initializeTelemetryMetadata();

          }

          void VideoMediaSinkService::initializeTelemetryMetadata() {
            if (dynamic_cast<projection::QtVideoOutput *>(videoOutput_.get()) != nullptr) {
              decodeBackend_ = "qt_qmediaplayer";
              std::ostringstream details;
              details << "playback=QMediaPlayer(StreamPlayback)"
                      << ", gst_debug=" << envValueOrUnset("GST_DEBUG")
                      << ", gst_plugin_path=" << envValueOrUnset("GST_PLUGIN_PATH")
                      << ", qt_gst_videosink=" << envValueOrUnset("QT_GSTREAMER_WIDGET_VIDEOSINK");
              decodeBackendDetails_ = details.str();
              decoderElementName_ = "unavailable";
              decoderDetectionMethod_ = "qt5_qmediaplayer_api_does_not_expose_decoder_element";
              // Qt backend writes incoming bytes to SequentialBuffer before decode.
              backendBuffersPackets_ = true;
              // Qt/GStreamer may use HW decode, but this path cannot strictly confirm it.
              backendLikelyHardwareDecode_ = false;
              return;
            }

#ifdef USE_OMX
            if (dynamic_cast<projection::OMXVideoOutput *>(videoOutput_.get()) != nullptr) {
              decodeBackend_ = "omx_il";
              decodeBackendDetails_ = "playback=OpenMAX IL (video_decode -> video_render)";
              decoderElementName_ = "video_decode";
              decoderDetectionMethod_ = "known_omx_pipeline";
              backendBuffersPackets_ = false;
              backendLikelyHardwareDecode_ = true;
              return;
            }
#endif

            decodeBackend_ = typeid(*videoOutput_).name();
            decodeBackendDetails_ = "playback=unknown backend type";
            decoderElementName_ = "unknown";
            decoderDetectionMethod_ = "unknown_backend_type";
          }

          void VideoMediaSinkService::maybePublishVideoTelemetry(aasdk::messenger::Timestamp::ValueType timestamp,
                                                                 std::size_t packetSize) {
            ++telemetryPackets_;
            telemetryBytes_ += static_cast<uint64_t>(packetSize);

            if (timestamp != 0) {
              ++telemetryTimestampedPackets_;
              if (firstTimestampInWindow_ == 0) {
                firstTimestampInWindow_ = timestamp;
              }
              if (timestamp > lastTimestampInWindow_) {
                lastTimestampInWindow_ = timestamp;
              }
            }

            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = now - telemetryWindowStart_;
            if (elapsed < std::chrono::seconds(1)) {
              return;
            }

            const double elapsedSeconds = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
            const double packetRate = elapsedSeconds > 0.0 ? static_cast<double>(telemetryPackets_) / elapsedSeconds : 0.0;
            const double ingressMbps = elapsedSeconds > 0.0
                ? (static_cast<double>(telemetryBytes_) * 8.0) / (elapsedSeconds * 1000.0 * 1000.0)
                : 0.0;

            std::ostringstream details;
            details << std::fixed << std::setprecision(2)
                    << "backend=" << decodeBackend_
                    << ", likely_hw_decode=" << (backendLikelyHardwareDecode_ ? "true" : "false")
                    << ", packet_buffering=" << (backendBuffersPackets_ ? "true" : "false")
                    << ", ingress_pps=" << packetRate
                    << ", ingress_mbps=" << ingressMbps
                    << ", packets=" << telemetryPackets_
                    << ", bytes=" << telemetryBytes_;

            if (telemetryTimestampedPackets_ >= 2 && lastTimestampInWindow_ > firstTimestampInWindow_) {
              const double timestampSpanSeconds =
                  static_cast<double>(lastTimestampInWindow_ - firstTimestampInWindow_) / 1000000.0;
              if (timestampSpanSeconds > 0.0) {
                const double sourceFpsEstimate =
                    static_cast<double>(telemetryTimestampedPackets_ - 1) / timestampSpanSeconds;
                details << ", source_fps_est=" << sourceFpsEstimate;
              }
            } else {
              details << ", source_fps_est=n/a";
            }

            publishVideoDebug(channel_->getId(), "stream_telemetry", details.str());

            telemetryWindowStart_ = now;
            telemetryPackets_ = 0;
            telemetryBytes_ = 0;
            telemetryTimestampedPackets_ = 0;
            firstTimestampInWindow_ = 0;
            lastTimestampInWindow_ = 0;
          }

          void VideoMediaSinkService::start() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] start()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              {
                std::ostringstream details;
                details << "backend=" << decodeBackend_
                  << ", backend_details=" << decodeBackendDetails_
                        << ", likely_hw_decode=" << (backendLikelyHardwareDecode_ ? "true" : "false")
                        << ", packet_buffering=" << (backendBuffersPackets_ ? "true" : "false");
                publishVideoDebug(channel_->getId(), "service_started", details.str());
              }
              {
                std::ostringstream details;
                details << "backend=" << decodeBackend_
                        << ", decoder_element=" << decoderElementName_
                        << ", detection_method=" << decoderDetectionMethod_;
                publishVideoDebug(channel_->getId(), "decoder_probe", details.str());
              }
              channel_->receive(this->shared_from_this());
            });
          }

          void VideoMediaSinkService::stop() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] stop()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              publishVideoDebug(channel_->getId(), "service_stopped");
              videoOutput_->stop();
            });
          }

          void VideoMediaSinkService::pause() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] pause()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              publishVideoDebug(channel_->getId(), "paused");
            });
          }

          void VideoMediaSinkService::resume() {
            strand_.dispatch([this, self = this->shared_from_this()]() {
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] resume()";
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                                 << aasdk::messenger::channelIdToString(channel_->getId());
              publishVideoDebug(channel_->getId(), "resumed");

            });
          }

          void VideoMediaSinkService::fillFeatures(
              aap_protobuf::service::control::message::ServiceDiscoveryResponse &response) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] fillFeatures()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel "
                               << aasdk::messenger::channelIdToString(channel_->getId());

            auto *service = response.add_channels();
            service->set_id(static_cast<uint32_t>(channel_->getId()));

            auto *videoChannel = service->mutable_media_sink_service();

            videoChannel->set_available_type(
                aap_protobuf::service::media::shared::message::MediaCodecType::MEDIA_CODEC_VIDEO_H264_BP);
            videoChannel->set_available_while_in_call(true);


            auto *videoConfig1 = videoChannel->add_video_configs();
            videoConfig1->set_codec_resolution(videoOutput_->getVideoResolution());
            videoConfig1->set_frame_rate(videoOutput_->getVideoFPS());

            const auto &videoMargins = videoOutput_->getVideoMargins();
            videoConfig1->set_height_margin(videoMargins.height());
            videoConfig1->set_width_margin(videoMargins.width());
            videoConfig1->set_density(videoOutput_->getScreenDPI());

            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getVideoResolution " << VideoCodecResolutionType_Name(videoOutput_->getVideoResolution());
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getVideoFPS " << VideoFrameRateType_Name(videoOutput_->getVideoFPS());
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] width " << videoMargins.width();
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] height " << videoMargins.height();
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] getScreenDPI " << videoOutput_->getScreenDPI();
          }

          void
          VideoMediaSinkService::onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onMediaChannelSetupRequest()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", Codec: "
                               << MediaCodecType_Name(request.type());


            auto status = videoOutput_->init()
                          ? aap_protobuf::service::media::shared::message::Config::STATUS_READY
                          : aap_protobuf::service::media::shared::message::Config::STATUS_WAIT;

            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] setup status: " << Config_Status_Name(status);
            {
              std::ostringstream details;
              details << "codec=" << MediaCodecType_Name(request.type())
                      << ", status=" << Config_Status_Name(status);
              publishVideoDebug(channel_->getId(), "setup", details.str());
            }

            aap_protobuf::service::media::shared::message::Config response;
            response.set_status(status);
            response.set_max_unacked(1);
            response.add_configuration_indices(0);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then(std::bind(&VideoMediaSinkService::sendVideoFocusIndication, this->shared_from_this()),
                          std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                    std::placeholders::_1));

            channel_->sendChannelSetupResponse(response, std::move(promise));
            {
              std::ostringstream telemetryDetails;
              telemetryDetails << "max_unacked=" << response.max_unacked()
                               << ", backend=" << decodeBackend_
                               << ", likely_hw_decode=" << (backendLikelyHardwareDecode_ ? "true" : "false")
                               << ", packet_buffering=" << (backendBuffersPackets_ ? "true" : "false");
              publishVideoDebug(channel_->getId(), "channel_setup_flow", telemetryDetails.str());
            }
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onChannelOpenRequest()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: " << request.service_id() << ", Priority: "
                               << request.priority();

            const aap_protobuf::shared::MessageStatus status = videoOutput_->open()
                                                               ? aap_protobuf::shared::MessageStatus::STATUS_SUCCESS
                                                               : aap_protobuf::shared::MessageStatus::STATUS_INTERNAL_ERROR;

            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Status determined: "
                               << aap_protobuf::shared::MessageStatus_Name(status);
            {
              std::ostringstream details;
              details << "status=" << aap_protobuf::shared::MessageStatus_Name(status)
                      << ", priority=" << request.priority();
              publishVideoDebug(channel_->getId(), "channel_opened", details.str());
            }

            aap_protobuf::service::control::message::ChannelOpenResponse response;
            response.set_status(status);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendChannelOpenResponse(response, std::move(promise));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaChannelStartIndication(
              const aap_protobuf::service::media::shared::message::Start &indication) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onMediaChannelStartIndication()";
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: "
                               << indication.session_id();

            session_ = indication.session_id();
            publishVideoDebug(channel_->getId(), "stream_started", std::string("session=") + std::to_string(session_));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaChannelStopIndication(
              const aap_protobuf::service::media::shared::message::Stop &indication) {
            OPENAUTO_LOG(info) << "[onMediaChannelStopIndication] onMediaChannelStopIndication()";
            OPENAUTO_LOG(info) << "[onMediaChannelStopIndication] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: " << session_;

            publishVideoDebug(channel_->getId(), "stream_stopped", std::string("session=") + std::to_string(session_));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp,
                                                                     const aasdk::common::DataConstBuffer &buffer) {
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onMediaWithTimestampIndication()";
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] Channel Id: "
                               << aasdk::messenger::channelIdToString(channel_->getId()) << ", session: " << session_;

            // this->maybePublishVideoTelemetry(timestamp, buffer.size);

            videoOutput_->write(timestamp, buffer);

            aap_protobuf::service::media::source::message::Ack indication;
            indication.set_session_id(session_);
            indication.set_ack(1);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() {}, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendMediaAckIndication(indication, std::move(promise));
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::onMediaIndication(const aasdk::common::DataConstBuffer &buffer) {
            OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onMediaIndication()";
            this->onMediaWithTimestampIndication(0, buffer);
          }

          void VideoMediaSinkService::onChannelError(const aasdk::error::Error &e) {
            // OPERATION_ABORTED is expected during shutdown when messenger stops
            if (e.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) {
              OPENAUTO_LOG(debug) << "[VideoMediaSinkService] onChannelError(): " << e.what()
                                  << ", channel: " << aasdk::messenger::channelIdToString(channel_->getId())
                                  << " (expected during stop)";
            } else {
              OPENAUTO_LOG(error) << "[VideoMediaSinkService] onChannelError(): " << e.what()
                                  << ", channel: " << aasdk::messenger::channelIdToString(channel_->getId());
            }
            publishVideoDebug(channel_->getId(), "channel_error", e.what());
          }

          void VideoMediaSinkService::onVideoFocusRequest(
              const aap_protobuf::service::media::video::message::VideoFocusRequestNotification &request) {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] onVideoFocusRequest()";
            // Note: disp_channel_id() is deprecated but still used for logging compatibility
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] Display index: " << request.disp_channel_id() << ", focus mode: " << VideoFocusMode_Name(request.mode()) << ", focus reason: " << VideoFocusReason_Name(request.reason());
            #pragma GCC diagnostic pop
            {
              std::ostringstream details;
              details << "mode=" << VideoFocusMode_Name(request.mode())
                      << ", reason=" << VideoFocusReason_Name(request.reason());
              publishVideoDebug(channel_->getId(), "focus_request", details.str());
            }

            if (request.mode() ==
                aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_NATIVE) {
              // Return to OS
              OPENAUTO_LOG(info) << "[VideoMediaSinkService] Returning to OS.";
              try {
                if (!std::ifstream("/tmp/entityexit")) {
                  std::ofstream("/tmp/entityexit");
                }
              } catch (...) {
                OPENAUTO_LOG(error) << "[VideoMediaSinkService] Error in creating /tmp/entityexit";
              }
            }

            this->sendVideoFocusIndication();
            channel_->receive(this->shared_from_this());
          }

          void VideoMediaSinkService::sendVideoFocusIndication() {
            OPENAUTO_LOG(info) << "[VideoMediaSinkService] sendVideoFocusIndication()";

            aap_protobuf::service::media::video::message::VideoFocusNotification videoFocusIndication;
            videoFocusIndication.set_focus(
                aap_protobuf::service::media::video::message::VideoFocusMode::VIDEO_FOCUS_PROJECTED);
            videoFocusIndication.set_unsolicited(false);

            auto promise = aasdk::channel::SendPromise::defer(strand_);
            promise->then([]() { }, std::bind(&VideoMediaSinkService::onChannelError, this->shared_from_this(),
                                             std::placeholders::_1));
            channel_->sendVideoFocusIndication(videoFocusIndication, std::move(promise));
          }
        }
      }
    }
  }
}
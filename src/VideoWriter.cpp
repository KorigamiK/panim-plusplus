#include "panim/VideoWriter.hpp"
#include "panim/Log.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace panim {

    struct VideoWriter::Impl {
        AVFormatContext *fmt = nullptr;
        AVCodecContext *codec_ctx = nullptr;
        AVStream *stream = nullptr;
        SwsContext *sws = nullptr;
        AVFrame *frame_yuv = nullptr;
        AVFrame *frame_rgba = nullptr; // wraps incoming data
        int frame_index = 0;
        int input_width = 0;
        int input_height = 0;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        bool finished = false;

        ~Impl() {
            if (codec_ctx) {
                avcodec_free_context(&codec_ctx);
            }
            if (fmt) {
                if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
                    avio_closep(&fmt->pb);
                }
                avformat_free_context(fmt);
            }
            if (sws)
                sws_freeContext(sws);
            if (frame_yuv)
                av_frame_free(&frame_yuv);
            if (frame_rgba)
                av_frame_free(&frame_rgba);
        }
    };

    static std::string lowercase_extension(const std::filesystem::path &path) {
        std::string extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        return extension;
    }

    static const AVCodec *choose_codec(bool png_output) {
        if (png_output)
            return avcodec_find_encoder(AV_CODEC_ID_PNG);
        const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
        if (!codec)
            codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        }
        return codec;
    }

    static bool supports_pixel_format(const AVCodec *codec, AVPixelFormat format) {
#if LIBAVCODEC_VERSION_MAJOR >= 61
        const void *supported = nullptr;
        int count = 0;
        int result = avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &supported, &count);
        if (result < 0 || !supported)
            return true;
        const auto *formats = static_cast<const AVPixelFormat *>(supported);
        for (int index = 0; index < count; ++index) {
            if (formats[index] == format)
                return true;
        }
#else
        if (!codec->pix_fmts)
            return true;
        for (const AVPixelFormat *candidate = codec->pix_fmts; *candidate != AV_PIX_FMT_NONE; ++candidate) {
            if (*candidate == format)
                return true;
        }
#endif
        return false;
    }

    static std::string ffmpeg_error(int code) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(code, message, sizeof(message));
        return message;
    }

    static Status write_packets(AVCodecContext *ctx, AVFormatContext *fmt, AVStream *stream) {
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
            return Status::failure("Failed to allocate an encoded packet");

        while (true) {
            int ret = avcodec_receive_packet(ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                av_packet_free(&pkt);
                return Status::failure("Failed to receive encoded packet: " + ffmpeg_error(ret));
            }

            av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
            if (pkt->duration <= 0) {
                pkt->duration = av_rescale_q(1, ctx->time_base, stream->time_base);
            }
            pkt->stream_index = stream->index;

            ret = av_interleaved_write_frame(fmt, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                return Status::failure("Failed to write encoded packet: " + ffmpeg_error(ret));
            }
        }
        av_packet_free(&pkt);
        return Status::success();
    }

    VideoWriter::VideoWriter(const std::filesystem::path &path, int width, int height, double fps, int bitrate)
        : VideoWriter(path, width, height, fps, VideoWriterOptions{VideoProfile::Share, bitrate}) {}

    VideoWriter::VideoWriter(const std::filesystem::path &path, int width, int height, double fps, const VideoWriterOptions &options)
        : impl_(std::make_unique<Impl>()) {
        const std::string extension = lowercase_extension(path);
        const bool png_output = extension == ".png";
        const bool mp4_output = extension == ".mp4";
        impl_->input_width = options.input_width > 0 ? options.input_width : width;
        impl_->input_height = options.input_height > 0 ? options.input_height : height;
        impl_->width = width;
        impl_->height = height;
        impl_->fps = fps;

        const AVCodec *codec = choose_codec(png_output);
        if (!codec) {
            status_ = Status::failure(png_output ? "PNG encoder is unavailable" : "No suitable encoder (H.264/MPEG4) found");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        avformat_alloc_output_context2(&impl_->fmt, nullptr, nullptr, path.string().c_str());
        if (!impl_->fmt) {
            status_ = Status::failure("Failed to allocate output format context");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        if (png_output && impl_->fmt->priv_data) {
            av_opt_set_int(impl_->fmt->priv_data, "update", 1, 0);
            av_opt_set_int(impl_->fmt->priv_data, "atomic_writing", 1, 0);
        }
        if (mp4_output && impl_->fmt->priv_data) {
            av_opt_set(impl_->fmt->priv_data, "movflags", "+faststart", 0);
        }

        impl_->stream = avformat_new_stream(impl_->fmt, codec);
        if (!impl_->stream) {
            status_ = Status::failure("Failed to create stream");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        impl_->stream->id = impl_->fmt->nb_streams - 1;

        impl_->codec_ctx = avcodec_alloc_context3(codec);
        if (!impl_->codec_ctx) {
            status_ = Status::failure("Failed to allocate codec context");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        const bool quality_mode = codec->id == AV_CODEC_ID_H264 && options.bitrate <= 0;
        int64_t selected_bitrate = options.bitrate;
        if (codec->id != AV_CODEC_ID_H264 && selected_bitrate <= 0) {
            const double bits_per_pixel = 0.10;
            selected_bitrate =
                std::max<int64_t>(2'000'000, static_cast<int64_t>(std::llround(static_cast<double>(width) * height * fps * bits_per_pixel)));
        }

        impl_->codec_ctx->codec_id = codec->id;
        impl_->codec_ctx->bit_rate = quality_mode ? 0 : selected_bitrate;
        impl_->codec_ctx->width = width;
        impl_->codec_ctx->height = height;
        AVRational frame_rate = av_d2q(fps, 1'000'000);
        impl_->stream->time_base = av_inv_q(frame_rate);
        impl_->codec_ctx->time_base = impl_->stream->time_base;
        impl_->codec_ctx->framerate = frame_rate;
        impl_->codec_ctx->gop_size = std::max(12, static_cast<int>(std::lround(fps * 2.0)));
        impl_->codec_ctx->max_b_frames = png_output ? 0 : 2;
        AVPixelFormat desired_format = AV_PIX_FMT_YUV420P;
        if (png_output) {
            desired_format = AV_PIX_FMT_RGBA;
        } else if (options.profile == VideoProfile::Master && codec->id == AV_CODEC_ID_H264) {
            desired_format = AV_PIX_FMT_YUV444P;
        }
        if (!supports_pixel_format(codec, desired_format)) {
            if (desired_format == AV_PIX_FMT_YUV444P && supports_pixel_format(codec, AV_PIX_FMT_YUV420P)) {
                PANIM_LOG_WARN("Encoder {} has no 4:4:4 mode; master output will use 4:2:0", codec->name);
                desired_format = AV_PIX_FMT_YUV420P;
            } else {
                status_ = Status::failure("Encoder does not support the required pixel format");
                PANIM_LOG_ERROR(status_.message);
                return;
            }
        }
        impl_->codec_ctx->pix_fmt = desired_format;

        if (!png_output) {
            impl_->codec_ctx->color_primaries = AVCOL_PRI_BT709;
            impl_->codec_ctx->color_trc = AVCOL_TRC_BT709;
            impl_->codec_ctx->colorspace = AVCOL_SPC_BT709;
            impl_->codec_ctx->color_range = AVCOL_RANGE_MPEG;
        }

        if (impl_->fmt->oformat->flags & AVFMT_GLOBALHEADER) {
            impl_->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (codec->id == AV_CODEC_ID_H264) {
            const char *preset = options.profile == VideoProfile::Draft ? "veryfast" : options.profile == VideoProfile::Master ? "slow" : "medium";
            av_opt_set(impl_->codec_ctx->priv_data, "preset", preset, 0);
            if (quality_mode) {
                const char *crf = options.profile == VideoProfile::Draft ? "23" : options.profile == VideoProfile::Master ? "10" : "16";
                av_opt_set(impl_->codec_ctx->priv_data, "crf", crf, 0);
                av_opt_set(impl_->codec_ctx->priv_data, "tune", "animation", 0);
                if (options.profile == VideoProfile::Master && impl_->codec_ctx->pix_fmt == AV_PIX_FMT_YUV444P) {
                    av_opt_set(impl_->codec_ctx->priv_data, "profile", "high444", 0);
                }
            }
        }

        if (png_output) {
            PANIM_LOG_INFO("FFmpeg encoder: {} (lossless {}x{} PNG)", codec->name, width, height);
        } else if (quality_mode) {
            const char *profile_name = options.profile == VideoProfile::Draft    ? "draft"
                                       : options.profile == VideoProfile::Master ? "master"
                                                                                 : "share";
            const int crf = options.profile == VideoProfile::Draft ? 23 : options.profile == VideoProfile::Master ? 10 : 16;
            const char *pixel_format = av_get_pix_fmt_name(impl_->codec_ctx->pix_fmt);
            PANIM_LOG_INFO("FFmpeg encoder: {} ({}x{}, {} fps, {} profile, CRF {}, {})", codec->name, width, height, fps, profile_name, crf,
                           pixel_format ? pixel_format : "unknown pixel format");
        } else {
            PANIM_LOG_INFO("FFmpeg encoder: {} ({}x{}, {} fps, {} bps)", codec->name, width, height, fps, selected_bitrate);
        }

        int ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
        if (ret < 0) {
            status_ = Status::failure("Failed to open codec: " + ffmpeg_error(ret));
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        ret = avcodec_parameters_from_context(impl_->stream->codecpar, impl_->codec_ctx);
        if (ret < 0) {
            status_ = Status::failure("Failed to copy codec parameters to stream");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        if (!(impl_->fmt->oformat->flags & AVFMT_NOFILE)) {
            ret = avio_open(&impl_->fmt->pb, path.string().c_str(), AVIO_FLAG_WRITE);
            if (ret < 0) {
                status_ = Status::failure("Could not open output file");
                PANIM_LOG_ERROR(status_.message);
                return;
            }
        }

        ret = avformat_write_header(impl_->fmt, nullptr);
        if (ret < 0) {
            status_ = Status::failure("Error occurred when writing header");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        impl_->frame_yuv = av_frame_alloc();
        if (!impl_->frame_yuv) {
            status_ = Status::failure("Could not allocate YUV frame");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        impl_->frame_yuv->format = impl_->codec_ctx->pix_fmt;
        impl_->frame_yuv->width = width;
        impl_->frame_yuv->height = height;
        ret = av_frame_get_buffer(impl_->frame_yuv, 32);
        if (ret < 0) {
            status_ = Status::failure("Could not allocate YUV frame buffer");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        impl_->frame_rgba = av_frame_alloc();
        if (!impl_->frame_rgba) {
            status_ = Status::failure("Could not allocate RGBA frame wrapper");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        impl_->frame_rgba->format = AV_PIX_FMT_RGBA;
        impl_->frame_rgba->width = impl_->input_width;
        impl_->frame_rgba->height = impl_->input_height;

        impl_->sws = sws_getContext(impl_->input_width, impl_->input_height, AV_PIX_FMT_RGBA, width, height, impl_->codec_ctx->pix_fmt,
                                    SWS_LANCZOS | SWS_ACCURATE_RND | SWS_FULL_CHR_H_INP | SWS_FULL_CHR_H_INT, nullptr, nullptr, nullptr);
        if (!impl_->sws) {
            status_ = Status::failure("Failed to create swscale context");
            PANIM_LOG_ERROR(status_.message);
            return;
        }
        if (!png_output) {
            const int *coefficients = sws_getCoefficients(SWS_CS_ITU709);
            int colorspace_result = sws_setColorspaceDetails(impl_->sws, coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
            if (colorspace_result < 0) {
                status_ = Status::failure("Failed to configure BT.709 color conversion");
                PANIM_LOG_ERROR(status_.message);
                return;
            }
        }

        status_ = Status::success();
    }

    VideoWriter::~VideoWriter() {
        if (!impl_ || impl_->finished || !status_.ok)
            return;
        Status finish_status = finish();
        if (!finish_status.ok) {
            PANIM_LOG_ERROR("Failed to finalize output: {}", finish_status.message);
        }
    }

    Status VideoWriter::finish() {
        if (!impl_ || !status_.ok)
            return status_;
        if (impl_->finished)
            return status_;
        impl_->finished = true;

        int send_result = avcodec_send_frame(impl_->codec_ctx, nullptr);
        if (send_result < 0 && send_result != AVERROR_EOF) {
            status_ = Status::failure("Failed to flush encoder: " + ffmpeg_error(send_result));
        } else {
            Status flush_status = write_packets(impl_->codec_ctx, impl_->fmt, impl_->stream);
            if (!flush_status.ok)
                status_ = flush_status;
        }

        if (impl_->fmt) {
            int trailer_result = av_write_trailer(impl_->fmt);
            if (trailer_result < 0 && status_.ok) {
                status_ = Status::failure("Failed to write video trailer: " + ffmpeg_error(trailer_result));
            }
        }
        return status_;
    }

    Status VideoWriter::write_frame(const Frame &frame) {
        if (!status_.ok)
            return status_;
        if (impl_->finished)
            return Status::failure("Cannot write after output is finalized");
        if (frame.width != impl_->input_width || frame.height != impl_->input_height) {
            return Status::failure("Frame size mismatch");
        }

        // Wrap input buffer without copying by pointing linesize/data to const data.
        impl_->frame_rgba->data[0] = const_cast<uint8_t *>(frame.pixels.data());
        impl_->frame_rgba->linesize[0] = frame.width * 4;

        // Ensure YUV frame buffer is writable
        int ret = av_frame_make_writable(impl_->frame_yuv);
        if (ret < 0) {
            return Status::failure("YUV frame not writable");
        }

        sws_scale(impl_->sws, impl_->frame_rgba->data, impl_->frame_rgba->linesize, 0, impl_->input_height, impl_->frame_yuv->data,
                  impl_->frame_yuv->linesize);

        impl_->frame_yuv->pts = impl_->frame_index++;

        ret = avcodec_send_frame(impl_->codec_ctx, impl_->frame_yuv);
        if (ret < 0) {
            return Status::failure("Error sending frame to encoder");
        }

        return write_packets(impl_->codec_ctx, impl_->fmt, impl_->stream);
    }

} // namespace panim

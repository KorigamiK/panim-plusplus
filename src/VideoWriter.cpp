#include "panim/VideoWriter.hpp"
#include "panim/Log.hpp"

#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
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
        int width = 0;
        int height = 0;
        double fps = 0.0;

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

    static const AVCodec *choose_codec() {
        const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            codec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
        }
        return codec;
    }

    static void write_packets(AVCodecContext *ctx, AVFormatContext *fmt, AVStream *stream) {
        AVPacket *pkt = av_packet_alloc();
        if (!pkt)
            return;

        while (true) {
            int ret = avcodec_receive_packet(ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0) {
                av_packet_free(&pkt);
                return;
            }

            av_packet_rescale_ts(pkt, ctx->time_base, stream->time_base);
            pkt->stream_index = stream->index;

            ret = av_interleaved_write_frame(fmt, pkt);
            if (ret < 0) {
                av_packet_free(&pkt);
                return;
            }
        }
        av_packet_free(&pkt);
    }

    VideoWriter::VideoWriter(const std::filesystem::path &path, int width, int height, double fps, int bitrate)
        : impl_(std::make_unique<Impl>()) {
        impl_->width = width;
        impl_->height = height;
        impl_->fps = fps;

        const AVCodec *codec = choose_codec();
        if (!codec) {
            status_ = Status::failure("No suitable encoder (H.264/MPEG4) found");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        avformat_alloc_output_context2(&impl_->fmt, nullptr, nullptr, path.string().c_str());
        if (!impl_->fmt) {
            status_ = Status::failure("Failed to allocate output format context");
            PANIM_LOG_ERROR(status_.message);
            return;
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

        impl_->codec_ctx->codec_id = codec->id;
        impl_->codec_ctx->bit_rate = bitrate;
        impl_->codec_ctx->width = width;
        impl_->codec_ctx->height = height;
        impl_->stream->time_base = {1, static_cast<int>(fps)};
        impl_->codec_ctx->time_base = impl_->stream->time_base;
        impl_->codec_ctx->framerate = {static_cast<int>(fps), 1};
        impl_->codec_ctx->gop_size = 12;
        impl_->codec_ctx->max_b_frames = 2;
        impl_->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

        if (impl_->fmt->oformat->flags & AVFMT_GLOBALHEADER) {
            impl_->codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }

        if (codec->id == AV_CODEC_ID_H264) {
            av_opt_set(impl_->codec_ctx->priv_data, "preset", "veryfast", 0);
        }

        PANIM_LOG_INFO("FFmpeg encoder: {} ({}x{}, {} fps)", codec->name, width, height, fps);

        int ret = avcodec_open2(impl_->codec_ctx, codec, nullptr);
        if (ret < 0) {
            status_ = Status::failure("Failed to open codec");
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
        impl_->frame_rgba->format = AV_PIX_FMT_RGBA;
        impl_->frame_rgba->width = width;
        impl_->frame_rgba->height = height;
        ret = av_frame_get_buffer(impl_->frame_rgba, 32);
        if (ret < 0) {
            status_ = Status::failure("Could not allocate RGBA frame buffer");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        impl_->sws = sws_getContext(
            width, height, AV_PIX_FMT_RGBA,
            width, height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!impl_->sws) {
            status_ = Status::failure("Failed to create swscale context");
            PANIM_LOG_ERROR(status_.message);
            return;
        }

        status_ = Status::success();
    }

    VideoWriter::~VideoWriter() {
        if (!impl_)
            return;
        if (!status_.ok)
            return;

        // Flush encoder
        avcodec_send_frame(impl_->codec_ctx, nullptr);
        write_packets(impl_->codec_ctx, impl_->fmt, impl_->stream);

        if (impl_->fmt) {
            av_write_trailer(impl_->fmt);
        }
    }

    Status VideoWriter::write_frame(const Frame &frame) {
        if (!status_.ok)
            return status_;
        if (frame.width != impl_->width || frame.height != impl_->height) {
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

        sws_scale(
            impl_->sws,
            impl_->frame_rgba->data,
            impl_->frame_rgba->linesize,
            0,
            impl_->height,
            impl_->frame_yuv->data,
            impl_->frame_yuv->linesize);

        impl_->frame_yuv->pts = impl_->frame_index++;

        ret = avcodec_send_frame(impl_->codec_ctx, impl_->frame_yuv);
        if (ret < 0) {
            return Status::failure("Error sending frame to encoder");
        }

        write_packets(impl_->codec_ctx, impl_->fmt, impl_->stream);
        return Status::success();
    }

} // namespace panim

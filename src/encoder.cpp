#include <learnopengl/encoder.h>
// #include <learnopengl/globals.h>
#include <iostream>                 // for std::cin/cout/cerr

// FFmpeg
// ------
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/log.h>
}

namespace Encoder {

    namespace {     // anonymous namespace (encapsulation)

        // FFmpeg
        // ------
        AVFormatContext* formatCtx = nullptr;
        AVCodecContext* codecCtx = nullptr;
        AVStream* videoStream = nullptr;
        SwsContext* swsCtx = nullptr;
        AVFrame* frameX = nullptr;
        AVPacket pkt = {};  // Zero-initialize the struct
        // std::ofstream logFile;
        // std::mutex logMutex;
    }

    // Function to initialize FFmpeg encoder
    bool initializeEncoder(const char* filename) {
        // Set up ffmpeg's log callback
        // av_log_set_callback(custom_ffmpeg_log_callback);

        // Optional: Set log level (AV_LOG_DEBUG=full logs, AV_LOG_INFO=default, AV_LOG_WARNING=only warnings, AV_LOG_ERROR=only errors)
        // av_log_set_level(AV_LOG_INFO);

        avformat_alloc_output_context2(&formatCtx, nullptr, "mp4", filename);
        if (!formatCtx) {
            printf("Could not create output context\n");
            return false;
        }

        // Find the H.264 encoder
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        if (!codec) {
            printf("H.264 encoder not found\n");
            return false;
        }

        // Create a new stream
        videoStream = avformat_new_stream(formatCtx, codec);
        if (!videoStream) {
            printf("Could not create video stream\n");
            return false;
        }

        // Set up the codec context
        int FPS = static_cast<int>(framerate);
        codecCtx = avcodec_alloc_context3(codec);
        codecCtx->width = SCR_WIDTH;
        codecCtx->height = SCR_HEIGHT;
        codecCtx->time_base = (AVRational){1, FPS*1000};  // Frame rate
        codecCtx->framerate = (AVRational){FPS, 1};
        codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        codecCtx->bit_rate = 60000000;  // 60 Mbps
        codecCtx->gop_size = 10;
        codecCtx->max_b_frames = 1;
        // codecCtx->rc_max_rate = codecCtx->bit_rate;  // Set maximum bitrate limit
        // codecCtx->rc_buffer_size = codecCtx->bit_rate;  // Set buffer size to match bitrate

        // Set H.264 options
        av_opt_set(codecCtx->priv_data, "preset", "slow", 0);
        av_opt_set(codecCtx->priv_data, "crf", "18", 0);
        // av_opt_set(codecCtx->priv_data, "bitrate", "30000k", 0);   // Set bitrate to 5000 kbps
        // av_opt_set(codecCtx->priv_data, "maxrate", "30000k", 0);   // Max bitrate
        // av_opt_set(codecCtx->priv_data, "bufsize", "30000k", 0);   // Buffer size
        // av_opt_set(codecCtx->priv_data, "profile", "high", 0);     // Set profile to "high" for better quality
        av_opt_set(codecCtx->priv_data, "pix_fmt", "yuv420p", 0);  // Try using yuv422p or yuv444p for better quality


        // Open the encoder
        if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
            printf("Could not open codec\n");
            return false;
        }

        // Assign codec to stream
        videoStream->codecpar->codec_id = AV_CODEC_ID_H264;
        avcodec_parameters_from_context(videoStream->codecpar, codecCtx);

        // Make sure that your AVStream has the same time_base
        videoStream->time_base = codecCtx->time_base;
        // Force the output stream to interpret it as CFR (Constant Frame Rate)
        // videoStream->avg_frame_rate = (AVRational){60, 1};  // Force the output to exactly 60 FPS
        // av_dict_set(&formatCtx->metadata, "video-cfr", "1", 0);

        // Open output file
        if (!(formatCtx->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&formatCtx->pb, filename, AVIO_FLAG_WRITE) < 0) {
                printf("Could not open output file\n");
                return false;
            }
        }

        // Write header
        if (avformat_write_header(formatCtx, nullptr) < 0) {
            std::cerr << "Error writing header to output file!" << std::endl;
            return false;  // Handle failure
        }

        // Allocate frame and conversion context
        frameX = av_frame_alloc();
        frameX->format = AV_PIX_FMT_YUV420P;
        frameX->width = SCR_WIDTH;
        frameX->height = SCR_HEIGHT;
        av_frame_get_buffer(frameX, 32);

        swsCtx = sws_getContext(SCR_WIDTH, SCR_HEIGHT, AV_PIX_FMT_RGB24,
                                SCR_WIDTH, SCR_HEIGHT, AV_PIX_FMT_YUV420P,
                                SWS_BILINEAR, nullptr, nullptr, nullptr);
        return true;
    }

    // Encode frame using FFmpeg
    bool encodeFrame(const uint8_t* rgbData, float crntTime) {
        // Convert RGB to YUV420P
        uint8_t* inData[1] = {(uint8_t*)rgbData};  // Input RGB data
        int inLinesize[1] = {3 * SCR_WIDTH};          // Input stride
        sws_scale(swsCtx, inData, inLinesize, 0, SCR_HEIGHT, frameX->data, frameX->linesize);

        frameX->pts = static_cast<int64_t>(crntTime * 60 * 1000); //* AV_TIME_BASE);
        // frameX->pts = 1000 * frameIndex; // Simple frame count to force 60 FPS
        // frameIndex++;
        
        // Encode the frame
        if (avcodec_send_frame(codecCtx, frameX) < 0) {
            printf("Error sending frame to encoder\n");
            return false;
        }

        // Retrieve the encoded packet
        pkt.data = nullptr;
        pkt.size = 0;
        // pkt.dts = (frameX->pts < pkt.dts) ? pkt.dts + 1 : frameX->pts;
        pkt.dts = frameX->pts;

        while (avcodec_receive_packet(codecCtx, &pkt) == 0) {
            pkt.stream_index = videoStream->index;
            av_interleaved_write_frame(formatCtx, &pkt);
            av_packet_unref(&pkt);
        }

        return true;
    }

    // Finalize encoding and close files
    void finalizeEncoder() {
        avcodec_send_frame(codecCtx, nullptr);
        while (avcodec_receive_packet(codecCtx, &pkt) == 0) {
            pkt.stream_index = videoStream->index;
            av_interleaved_write_frame(formatCtx, &pkt);
            av_packet_unref(&pkt);
        }

        // Close log file at the end
        // if (logFile.is_open())
        // {
        //     logFile.close();
        // }

        av_write_trailer(formatCtx);
        avcodec_free_context(&codecCtx);
        avformat_free_context(formatCtx);
        sws_freeContext(swsCtx);
        av_frame_free(&frameX);
    }

}
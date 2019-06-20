#include "common.h"
#include "CDecode.h"

CDecode::~CDecode()
{
    std::string err;
    stopdecode(err);
}

bool CDecode::set_input(const std::string& input, std::string& err)
{
    LOCK();
    CHECKSTOP(err);
    if (input.empty())
    {
        err = "input is empty.";
        return false;
    }
    else
    {
        input_ = input;
        err = "opt succeed.";
        return true;
    }
}

const std::string& CDecode::get_input(std::string& err)
{
    LOCK();
    err = "opt succeed.";
    return input_;
}

bool CDecode::set_dec_callback(DecFrameCallback cb, void* param, std::string& err)
{
    LOCK();
    CHECKSTOP(err);
    decframecb_ = cb;
    decframecbparam_ = param;
    err = "opt succeed.";
    return true;
}

bool CDecode::set_dec_status_callback(DecStatusCallback cb, void* param, std::string& err)
{
    LOCK();
    CHECKSTOP(err);
    decstatuscb_ = cb;
    decstatuscbparam_ = param;
    err = "opt succeed.";
    return true;
}

bool CDecode::set_hwdec_type(AVHWDeviceType hwtype, bool trans, std::string& err)
{
    LOCK();
    CHECKSTOP(err);
    hwtype_ = hwtype;
    trans_ = trans;
    return true;
}

bool CDecode::begindecode(std::string& err)
{
    LOCK();
    CHECKSTOP(err);

    int ret;

    if (!stopdecode(err))
    {
        return false;
    }

    fmtctx_ = avformat_alloc_context();
    if (fmtctx_ == nullptr)
    {
        err = "avformat_alloc_context() return nullptr.";
        return false;
    }

    ret = avformat_open_input(&fmtctx_, input_.c_str(), nullptr, nullptr);
    CHECKFFRET(ret);

    ret = avformat_find_stream_info(fmtctx_, nullptr);
    CHECKFFRET(ret);

    // ������
    AVCodec* vcodec = nullptr;
    ret = av_find_best_stream(fmtctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &vcodec, 0);
    vindex_ = ret;
    AVCodec* acodec = nullptr;
    ret = av_find_best_stream(fmtctx_, AVMEDIA_TYPE_AUDIO, -1, -1, &acodec, 0);
    aindex_ = ret;

    if (vindex_ < 0 && aindex_ < 0)
    {
        err = "cant find stream.";
        return false;
    }
    if (vindex_ >= 0)
    {
        vcodectx_ = avcodec_alloc_context3(vcodec);
        if (vcodectx_ == nullptr)
        {
            err = "avcodec_alloc_context3(vcodec) return nullptr.";
            return false;
        }
        ret = avcodec_parameters_to_context(vcodectx_, fmtctx_->streams[vindex_]->codecpar);
        CHECKFFRETANDCTX(ret, vcodectx_);
        if (hwtype_ != AV_HWDEVICE_TYPE_NONE)
        {
            // ��ѯӲ����֧��
            for (int i = 0;; i++)
            {
                const AVCodecHWConfig* config = avcodec_get_hw_config(vcodec, i);
                if (config == nullptr)
                {
                    err = vcodec->name + std::string(" not support ") + av_hwdevice_get_type_name(hwtype_);
                    break;
                }
                if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
                    config->device_type == hwtype_)
                {
                    // Ӳ��������
                    AVBufferRef* hwbufref = nullptr;
                    ret = av_hwdevice_ctx_create(&hwbufref, hwtype_, nullptr, nullptr, 0);
                    if (ret < 0)
                    {
                        err = av_err2str(ret);
                    }
                    else
                    {
                        vcodectx_->hw_device_ctx = av_buffer_ref(hwbufref);
                        if (vcodectx_->hw_device_ctx == nullptr)
                        {
                            err = "av_buffer_ref(hwbufref) return nullptr.";
                        }
                        av_buffer_unref(&hwbufref);
                        hwfmt_ = config->pix_fmt;
                    }
                    break;
                }
            }
        }
        ret = avcodec_open2(vcodectx_, vcodec, nullptr);
        CHECKFFRETANDCTX(ret, vcodectx_);
    }
    if (aindex_ >= 0)
    {
        acodectx_ = avcodec_alloc_context3(acodec);
        if (acodectx_ == nullptr)
        {
            err = "avcodec_alloc_context3(acodec) return nullptr.";
            return false;
        }
        ret = avcodec_parameters_to_context(acodectx_, fmtctx_->streams[aindex_]->codecpar);
        CHECKFFRETANDCTX2(ret, vcodectx_, acodectx_);
        ret = avcodec_open2(acodectx_, acodec, nullptr);
        CHECKFFRETANDCTX2(ret, vcodectx_, acodectx_);
    }

    av_dump_format(fmtctx_, 0, input_.c_str(), 0);

    status_ = DECODING;
    std::thread th(&CDecode::decodethread, this);
    decodeth_.swap(th);

    return true;
}

bool CDecode::stopdecode(std::string& err)
{
    LOCK();

    status_ = STOP;
    if (decodeth_.joinable())
    {
        decodeth_.join();
    }
    if (vcodectx_ != nullptr)
    {
        avcodec_free_context(&vcodectx_);
    }
    if (acodectx_ != nullptr)
    {
        avcodec_free_context(&acodectx_);
    }
    avformat_close_input(&fmtctx_);

    vindex_ = aindex_ = -1;
    err = "opt succeed.";

    return true;
}

bool CDecode::seek(int64_t timestamp, int flags, std::string& err)
{
    LOCK();
    AVRational timebase = { 0 };
    if (vindex_ != -1)
    {
        timebase = fmtctx_->streams[vindex_]->time_base;
    }
    else if (aindex_ != -1)
    {
        timebase = fmtctx_->streams[aindex_]->time_base;
    }
    CHECKFFRET(av_seek_frame(fmtctx_, -1, av_rescale_q_rnd(timestamp, { 1, 1 }, timebase, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)), flags));
    return true;
}

bool CDecode::decodethread()
{
    int ret;
    std::string err;
    FRAMETYPE decodingtype = ERR; // ��¼��ǰ����֡����
    AVCodecContext* codectx = nullptr;
    // ����AVPacket��AVFrame
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    AVFrame* traframe = av_frame_alloc();
    if (packet == nullptr || frame == nullptr)
    {
        if (decstatuscb_ != nullptr)
        {
            status_ = STOP;
            decstatuscb_(STOP, "av_packet_alloc() or av_frame_alloc() return nullptr.", decstatuscbparam_);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_frame_free(&traframe);
        return false;
    }
    // ��ʼ��packet
    av_init_packet(packet);

    // ѭ�������ݽ�������
    while (true)
    {
        if (status_ != DECODING)
            break;

        // ������
        ret = av_read_frame(fmtctx_, packet);
        if (ret < 0)
        {
            if (decstatuscb_ != nullptr)
            {
                status_ = STOP;
                decstatuscb_(STOP, av_err2str(ret), decstatuscbparam_);
            }
            break; //������Ϊ��Ƶ��ȡ����
        }

        if (packet->stream_index == vindex_)
        {
            // ������Ƶ֡
            decodingtype = VIDEO;
            codectx = vcodectx_;
        }
        else if (packet->stream_index == aindex_)
        {
            // ������Ƶ֡
            decodingtype = AUDIO;
            codectx = acodectx_;
        }
        else
        {
            decodingtype = ERR;
            codectx = nullptr;
        }

        // ���codectx
        if (codectx == nullptr)
        {
            if (decstatuscb_ != nullptr)
            {
                decstatuscb_(DECODING, "codectx is nullptr.", decstatuscbparam_);
            }
            continue;
        }

        // ���ͽ�Ҫ���������
        ret = avcodec_send_packet(codectx, packet);
        if (ret < 0)
        {
            status_ = STOP;
            if (decstatuscb_ != nullptr)
            {
                decstatuscb_(STOP, av_err2str(ret), decstatuscbparam_);
            }
            break;
        }
        while (ret >= 0)
        {
            // ���ս�������
            ret = avcodec_receive_frame(codectx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                // ����������EOF
                break;
            }
            else if (ret < 0)
            {
                // ��������
                if (decstatuscb_ != nullptr)
                {
                    decstatuscb_(DECODING, av_err2str(ret), decstatuscbparam_);
                }
                break;
            }
            else
            {
                // �õ���������
                if (decframecb_ != nullptr)
                {
                    if (packet->stream_index == vindex_ // ��Ƶ֡ 
                        && hwtype_ != AV_HWDEVICE_TYPE_NONE // ʹ��Ӳ��
                        && frame->format == hwfmt_ // Ӳ���ʽ
                        && trans_ // ת��
                        )
                    {
                        ret = av_hwframe_transfer_data(traframe, frame, 0);
                        if (ret < 0)
                        {
                            if (decstatuscb_ != nullptr)
                            {
                                decstatuscb_(DECODING, av_err2str(ret), decstatuscbparam_);
                            }
                        }
                        else
                        {
                            traframe->pts = frame->pts;
                            traframe->pkt_dts = frame->pkt_dts;
                            traframe->pkt_duration = frame->pkt_duration;
                            decframecb_(traframe, decodingtype,
                                av_rescale_q_rnd(traframe->pts, fmtctx_->streams[packet->stream_index]->time_base, { 1, 1 }, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)),
                                decframecbparam_);
                        }
                    }
                    else
                    {
                        decframecb_(frame, decodingtype,
                            av_rescale_q_rnd(frame->pts, fmtctx_->streams[packet->stream_index]->time_base, { 1, 1 }, static_cast<AVRounding>(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX)),
                            decframecbparam_);
                    }
                }
                // ����û��ֱ��break������Ϊ�����ٴε���avcodec_receive_frame���õ������ݵĿ���
            }
        }

        // ��������ָ��Ļ�����
        av_packet_unref(packet);
    }

    // ����packet��frame
    av_packet_free(&packet);
    av_frame_free(&frame);
    av_frame_free(&traframe);

    return true;
}
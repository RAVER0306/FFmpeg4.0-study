#include "CDecode.h"

// C++��ʹ��av_err2str��
char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };
#define av_err2str(errnum) \
    av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

// �ݹ���
#define LOCK() std::lock_guard<std::recursive_mutex> _lock(this->mutex_)

// ���ֹͣ״̬
#define CHECKSTOP(err) \
if(this->status_ != STOP)\
{\
    err = "status is not stop.";\
    return false;\
}

// ���ffmpeg����ֵ
#define CHECKFFRET(ret) \
if (ret < 0)\
{\
    err = av_err2str(ret);\
    return false;\
}
#define CHECKFFRETANDCTX(ret, codectx) \
if (ret < 0)\
{\
    avcodec_free_context(&codectx);\
    err = av_err2str(ret);\
    return false;\
}
#define CHECKFFRETANDCTX2(ret, codectx1, codectx2) \
if (ret < 0)\
{\
    avcodec_free_context(&codectx1);\
    avcodec_free_context(&codectx2);\
    err = av_err2str(ret);\
    return false;\
}

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

bool CDecode::decodethread()
{
    int ret;
    std::string err;

    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    if (packet == nullptr || frame == nullptr)
    {
        if (decstatuscb_ != nullptr)
        {
            status_ = STOP;
            decstatuscb_(STOP, "av_packet_alloc() or av_frame_alloc() return nullptr.", decstatuscbparam_);
        }
        av_packet_free(&packet);
        av_frame_free(&frame);
        return false;
    }
    av_init_packet(packet);
    
    while (true)
    {
        if (status_ != DECODING)
            break;

        if (av_read_frame(fmtctx_, packet) < 0)
        {
            if (decstatuscb_ != nullptr)
            {
                status_ = STOP;
                decstatuscb_(STOP, "end of file.", decstatuscbparam_);
            }
            break; //������Ϊ��Ƶ��ȡ����
        }

        if (packet->stream_index == vindex_)
        {
            // ������Ƶ֡
            ret = avcodec_send_packet(vcodectx_, packet);
            if (ret < 0)
            {
                if (decstatuscb_ != nullptr)
                {
                    status_ = STOP;
                    decstatuscb_(STOP, av_err2str(ret), decstatuscbparam_);
                }
                break;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(vcodectx_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
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
                        decframecb_(frame, VIDEO, decframecbparam_);
                    }
                }
            }
        }
        else if (packet->stream_index == aindex_)
        {
            // ������Ƶ֡
            ret = avcodec_send_packet(acodectx_, packet);
            if (ret < 0)
            {
                if (decstatuscb_ != nullptr)
                {
                    status_ = STOP;
                    decstatuscb_(STOP, av_err2str(ret), decstatuscbparam_);
                }
                break;
            }
            while (ret >= 0)
            {
                ret = avcodec_receive_frame(acodectx_, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                {
                    break;
                }
                else if (ret < 0)
                {
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
                        decframecb_(frame, AUDIO, decframecbparam_);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);

    return true;
}
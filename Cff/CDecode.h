#ifndef __CDECODE_H__
#define __CDECODE_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libavformat/avformat.h>

#ifdef __cplusplus
}
#endif

#include <string>
#include <mutex>
#include <thread>

class CDecode
{
public:
    ~CDecode();

    // ״̬
    enum STATUS{STOP, DECODING};
    // ֡����
    enum FRAMETYPE {ERR, VIDEO, AUDIO};
    // ״̬֪ͨ�ص�����
    typedef void (*DecStatusCallback)(STATUS status, std::string err, void* param);
    // ����֡�ص�����
    typedef void (*DecFrameCallback)(const AVFrame* frame, FRAMETYPE frametype, void* param);

    // ��������
    bool set_input(const std::string& input, std::string& err);
    // ��ȡ����
    const std::string& get_input(std::string& err);

    // ���ý���֡�ص� 
    bool set_dec_callback(DecFrameCallback cb, void* param, std::string& err);
    // ���ý���״̬�仯�ص�
    bool set_dec_status_callback(DecStatusCallback cb, void* param, std::string& err);

    // ��ʼ����
    bool begindecode(std::string& err);
    // ֹͣ����
    bool stopdecode(std::string& err);

private:
    // �����߳�
    bool decodethread();

private:
    STATUS status_ = STOP;
    std::recursive_mutex mutex_;

    std::string input_;
    int vindex_ = -1;
    int aindex_ = -1;
    std::thread decodeth_;

    DecStatusCallback decstatuscb_ = nullptr;
    void* decstatuscbparam_ = nullptr;
    DecFrameCallback decframecb_ = nullptr;
    void* decframecbparam_ = nullptr;

    //ffmpeg
    AVFormatContext* fmtctx_ = nullptr;
    AVCodecContext* vcodectx_ = nullptr;
    AVCodecContext* acodectx_ = nullptr;
};

#endif __CDECODE_H__
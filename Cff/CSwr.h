#ifndef __CSWR_H__
#define __CSWR_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libswresample/swresample.h>


#ifdef __cplusplus
}
#endif

#include <string>
#include <mutex>

class CSwr
{
public:
    ~CSwr();

    // ״̬
    enum STATUS { STOP, LOCKED };
    // ����Դ����
    bool set_src_opt(int64_t layout, int rate, enum AVSampleFormat fmt, std::string& err);
    // ����Ŀ�����
    bool set_dst_opt(int64_t layout, int rate, enum AVSampleFormat fmt, std::string& err);
    // ��������
    bool lock_opt(std::string& err);
    // �������
    bool unlock_opt(std::string& err);
    // ת��
    int convert(uint8_t** out, int out_count, const uint8_t** in, int in_count, std::string& err);

private:
    STATUS status_ = STOP;
    std::recursive_mutex mutex_;

    SwrContext* swrctx_ = nullptr;

    AVSampleFormat src_sam_fmt_ = AV_SAMPLE_FMT_NONE;
    AVSampleFormat dst_sam_fmt_ = AV_SAMPLE_FMT_NONE;
    int64_t src_layout_ = AV_CH_LAYOUT_MONO;
    int64_t dst_layout_ = AV_CH_LAYOUT_MONO;
    int src_rate_ = 0;
    int dst_rate_ = 0;
};

#endif//__CSWR_H__
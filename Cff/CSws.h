#ifndef __CSWS_H__
#define __CSWS_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <libswscale/swscale.h>
#include <libavutil/imgutils.h> // av_image_alloc


#ifdef __cplusplus
}
#endif

#include <string>
#include <mutex>

class CSws
{
public:
    ~CSws();

    // ״̬
    enum STATUS { STOP, LOCKED };
    // ����Դ����
    bool set_src_opt(AVPixelFormat pixfmt, int w, int h, std::string& err);
    // ����Ŀ�����
    bool set_dst_opt(AVPixelFormat pixfmt, int w, int h, std::string& err);
    // ��������
    bool lock_opt(std::string& err);
    // �������
    bool unlock_opt(std::string& err);
    // ת��
    int scale(const uint8_t* const srcSlice[], const int srcStride[], int srcSliceY, int srcSliceH, uint8_t* const dst[], const int dstStride[], std::string& err);

private:
    STATUS status_ = STOP;
    std::recursive_mutex mutex_;

    SwsContext* swsctx_ = nullptr;

    AVPixelFormat src_pix_fmt_ = AV_PIX_FMT_NONE;
    AVPixelFormat dst_pix_fmt_ = AV_PIX_FMT_NONE;
    int src_w_ = 0;
    int src_h_ = 0;
    int dst_w_ = 0;
    int dst_h_ = 0;
};

#endif//__CSWS_H__
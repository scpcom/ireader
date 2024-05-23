#include "mp4-writer.h"
#include "mov-format.h"
#include "mpeg4-hevc.h"
#include "mpeg4-avc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


static uint8_t s_buffer[512 * 1024];
static uint8_t s_extra_data[64 * 1024];

struct mov_h265_test_t
{
	struct mp4_writer_t* mov;
	struct mpeg4_hevc_t hevc;

	int track;
	int width;
	int height;
	uint32_t pts, dts;
	const uint8_t* ptr;

	// uint8_t buf[1024 * 64];
	int bytes;

	int vcl;

    ptrdiff_t n;
    const uint8_t* p, *next, *end;
    uint8_t *data;
    int data_size;

    uint8_t *buf;
    int buf_seek;
    int buf_max_size;
};

static const uint8_t* file_read(const char* file, long* size)
{
	FILE* fp = fopen(file, "rb");
	if (fp)
	{
		fseek(fp, 0, SEEK_END);
		*size = ftell(fp);
		fseek(fp, 0, SEEK_SET);

		uint8_t* ptr = (uint8_t*)malloc(*size);
		fread(ptr, 1, *size, fp);
		fclose(fp);

		return (const uint8_t*)ptr;
	}

	return NULL;
}

static int h265_write(struct mov_h265_test_t* ctx, const void* data, int bytes)
{
	int vcl = 0;
	int update = 0;
	int n = h265_annexbtomp4(&ctx->hevc, data, bytes, s_buffer, sizeof(s_buffer), &vcl, &update);

	if (ctx->track < 0)
	{
		if (ctx->hevc.numOfArrays < 1)
		{
			//ctx->ptr = end;
			return -2; // waiting for vps/sps/pps
		}

		int extra_data_size = mpeg4_hevc_decoder_configuration_record_save(&ctx->hevc, s_extra_data, sizeof(s_extra_data));
		if (extra_data_size <= 0)
		{
			// invalid HVCC
			assert(0);
			return -1;
		}

		// TODO: waiting for key frame ???
		ctx->track = mp4_writer_add_video(ctx->mov, MOV_OBJECT_HEVC, ctx->width, ctx->height, s_extra_data, extra_data_size);
		if (ctx->track < 0)
			return -1;
		mp4_writer_init_segment(ctx->mov);
	}

	mp4_writer_write(ctx->mov, ctx->track, s_buffer, n, ctx->pts, ctx->pts, 1 == vcl ? MOV_AV_FLAG_KEYFREAME : 0);
	ctx->pts += 40;
	ctx->dts += 40;
	return 0;
}

static int memory_read(void* param, void* data, uint64_t bytes)
{
    struct mov_h265_test_t* ctx = (struct mov_h265_test_t*)param;
    if (ctx->buf_seek + bytes > ctx->buf_max_size) {
        bytes = ctx->buf_max_size - ctx->buf_seek;
    }
    uint8_t *src = (uint8_t *)ctx->buf + ctx->buf_seek;
    memcpy(data, src, bytes);
	return 0;
}

static int memory_write(void* param, const void* data, uint64_t bytes)
{
    struct mov_h265_test_t* ctx = (struct mov_h265_test_t*)param;
    if (ctx->buf_seek + bytes > ctx->buf_max_size) {
        return -1;
    }
    uint8_t *src = (uint8_t *)ctx->buf + ctx->buf_seek;
    memcpy(src, data, bytes);
    ctx->buf_seek += bytes;
	return 0;
}

static int memory_seek(void* param, int64_t offset)
{
    struct mov_h265_test_t* ctx = (struct mov_h265_test_t*)param;
    if (offset >= 0 && offset < ctx->buf_max_size) {
        ctx->buf_seek = offset;
    } else {
        ctx->buf_seek = ctx->buf_max_size;
    }
	return 0;
}

static int64_t memory_tell(void* param)
{
    struct mov_h265_test_t* ctx = (struct mov_h265_test_t*)param;
	return ctx->buf_seek;
}

static int mp4_buffer_init(struct mov_h265_test_t *ctx, int max_size)
{
    ctx->buf = malloc(max_size);
    if (ctx->buf == NULL) return -1;

    ctx->buf_max_size = max_size;
    ctx->buf_seek = 0;
    return 0;
}

static int mp4_buffer_deinit(struct mov_h265_test_t *ctx)
{
    ctx->buf_seek = 0;
    ctx->buf_max_size = 0;
    if (ctx->buf) {
        free(ctx->buf);
        ctx->buf = NULL;
    }
    return 0;
}

static struct mov_buffer_t *buffer_ops(void)
{
    static struct mov_buffer_t memory_ops = {
        memory_read,
        memory_write,
        memory_seek,
        memory_tell,
    };
    return &memory_ops;
}

static int mp4_buffer_seek(struct mov_h265_test_t *ctx, int offset)
{
    if (offset >= 0 && offset < ctx->buf_max_size) {
        ctx->buf_seek = offset;
    } else {
        ctx->buf_seek = ctx->buf_max_size;
    }
    return 0;
}

static const uint8_t* h264_startcode(const uint8_t *data, size_t bytes)
{
	size_t i;
	for (i = 2; i + 1 < bytes; i++)
	{
		if (0x01 == data[i] && 0x00 == data[i - 1] && 0x00 == data[i - 2])
			return data + i + 1;
	}

	return NULL;
}

int mp4_prepare_iterate_data(void *param, void *data, int size)
{
    struct mov_h265_test_t *ctx = (struct mov_h265_test_t *)param;
    ctx->ptr = data;
    ctx->data = data;
    ctx->data_size = size;
    ctx->end = ctx->data + ctx->data_size;
    ctx->p = h264_startcode(ctx->data, ctx->data_size);
    int avcc = mpeg4_h264_bitstream_format(ctx->data, ctx->data_size);
    if (avcc > 0) {
        printf("not support avcc format!\r\n");
        return -1;
    }

    return 0;
}

int mp4_h265_iterate(void *param, void **data, int *size)
{
    struct mov_h265_test_t *ctx = (struct mov_h265_test_t *)param;
    if (!ctx->p) return -1;

    ctx->next = h264_startcode(ctx->p, (int)(ctx->end - ctx->p));
    if (ctx->next)
    {
        ctx->n = ctx->next - ctx->p - 3;
    }
    else
    {
        ctx->n = ctx->end - ctx->p;
    }
printf("nalu type:%d\r\n", ((uint8_t *)ctx->p)[0] & 0x1f);
    while (ctx->n > 0 && 0 == ctx->p[ctx->n - 1]) ctx->n--; // filter tailing zero

    if (ctx->n > 0)
    {
        if (data) *data = (uint8_t *)ctx->p;
        if (size) *size = ctx->n;
    }

    ctx->p = ctx->next;

    return 0;
}


static void mp4_write_h265(void* param, uint8_t* nalu, size_t bytes)
{
	struct mov_h265_test_t* ctx = (struct mov_h265_test_t*)param;
	assert(ctx->ptr < nalu);

    uint8_t* ptr = nalu - 3;
//	const uint8_t* end = (const uint8_t*)nalu + bytes;
	uint8_t nalutype = (nalu[0] >> 1) & 0x3f;
    if (ctx->vcl > 0 && h265_is_new_access_unit((const uint8_t*)nalu, bytes))
    {
        int r = h265_write(ctx, ctx->ptr, ptr - ctx->ptr);
		if (-1 == r)
            return; // wait for more data

        ctx->ptr = ptr;
        ctx->vcl = 0;
    }

	if (nalutype <= 31) {
        ++ctx->vcl;
    }
}

static inline int h264_nal_type(const uint8_t *data, size_t bytes)
{
	uint8_t naltype;
	const uint8_t *p;

	do
	{printf("=========[%d]\r\n", __LINE__);
		p = h264_startcode(data, bytes);
		if (p)
		{printf("=========[%d]\r\n", __LINE__);
			naltype = p[0] & 0x1f;
			// 1: no-IDR slice
			// 2: A-slice
			// 3: B-slice
			// 4: C-slice
			// 5: IDR frame
			if (naltype > 0 && naltype < 6)
			{printf("=========[%d]\r\n", __LINE__);
				return naltype;
			}

			bytes -= p - data;
			data = p;
		}
	} while (p);

	return 0;
}

void mov_writer_h265(const char* h265, int width, int height, const char* mp4)
{
	long bytes = 0;
	uint8_t* ptr = (uint8_t *)file_read(h265, &bytes);
	if (NULL == ptr) return;
	FILE* fp = fopen(mp4, "wb+");

	struct mov_h265_test_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	ctx.track = -1;
	ctx.width = width;
	ctx.height = height;
	ctx.bytes = 0;

#if 0
static int cnt = 0;

    mp4_buffer_init(&ctx, 5 * 1024 * 1024);
	ctx.mov = mp4_writer_create(0, buffer_ops(), &ctx, MOV_FLAG_FASTSTART | MOV_FLAG_SEGMENT);
printf("buf_seek:%d\r\n", ctx.buf_seek);
    fwrite(ctx.buf, 1, ctx.buf_seek, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
    mp4_buffer_seek(&ctx, 0);

    mp4_prepare_iterate_data(&ctx, ptr, bytes);

    void *frame;
    int frame_size;
    while (0 == mp4_h265_iterate(&ctx, &frame, &frame_size)) {
        mp4_write_h265(&ctx, frame, (int)frame_size);
printf("frame_size:%d buf_seek:%d frame_type:%d(%#x\r\n", frame_size, ctx.buf_seek, ((uint8_t *)frame)[5] & 0x1F, ((uint8_t *)frame)[5] );
        uint8_t *nalu = ((uint8_t *)frame);
        // printf("nalu:");
        // for (int i = 0; i < 10; i ++) {
        //     printf("%#x ", nalu[i]);
        // }printf("nalu type:%d\r\n", h264_nal_type(frame, frame_size));
        fwrite(ctx.buf, 1, ctx.buf_seek, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
        mp4_buffer_seek(&ctx, 0);
    }
printf("buf_seek:%d\r\n", ctx.buf_seek);printf("===============[%s][%d]\r\n", __func__, __LINE__);
    mp4_buffer_seek(&ctx, 1672498);
	mp4_writer_destroy(ctx.mov);printf("buf_seek:%d\r\n", ctx.buf_seek - 1672498);
    fwrite(ctx.buf, 1, ctx.buf_seek - 1672498, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);

	fclose(fp);
	free(ptr);
    mp4_buffer_deinit(&ctx);
#else
static int cnt = 0;
    mp4_buffer_init(&ctx, 5 * 1024 * 1024);
	ctx.mov = mp4_writer_create(0, buffer_ops(), &ctx, MOV_FLAG_FASTSTART | MOV_FLAG_SEGMENT);
printf("buf_seek:%d\r\n", ctx.buf_seek);
    // fwrite(ctx.buf, 1, ctx.buf_seek, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
    // mp4_buffer_reset(&ctx);

    mp4_prepare_iterate_data(&ctx, ptr, bytes);

    void *frame;
    int frame_size;
    while (0 == mp4_h265_iterate(&ctx, &frame, &frame_size)) {
        mp4_write_h265(&ctx, frame, (int)frame_size);
printf("frame_size:%d buf_seek:%d\r\n", frame_size, ctx.buf_seek);
        // fwrite(ctx.buf, 1, ctx.buf_seek, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
        // mp4_buffer_reset(&ctx);
    }cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
printf("buf_seek:%d\r\n", ctx.buf_seek);printf("===============[%s][%d]\r\n", __func__, __LINE__);
	mp4_writer_destroy(ctx.mov);printf("buf_seek:%d\r\n", ctx.buf_seek);
    fwrite(ctx.buf, 1, ctx.buf_seek, fp);cnt += ctx.buf_seek;printf("cnt=%d\r\n", cnt);
	fclose(fp);
	free(ptr);
    mp4_buffer_deinit(&ctx);
#endif


}

int main(int argc, char **argv)
{
    if (argc < 5) {
        printf("try: ./test output.h264 640 480 output_h264.mp4");
    }

    mov_writer_h265(argv[1], argv[2], argv[3], argv[4]);
}
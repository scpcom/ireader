#include "flv-writer.h"
#include "flv-muxer.h"
#include "flv-header.h"
#include "flv-proto.h"
#include "mpeg4-avc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

struct flv_writer_t
{
	FILE* fp;
	flv_writer_onwrite write;
	void* param;
};

typedef struct
{
    uint8_t *p;
    size_t max_size;
    size_t offset;
} flv_writer_mem_t;

typedef struct {
    ptrdiff_t n;
    uint8_t *p, *next, *end;
    uint8_t *data;
    size_t data_size;
} h264_iterator_t;

struct h264_raw_t
{
	flv_muxer_t* flv;
	uint32_t pts, dts;
	uint8_t* ptr;
    int vcl;

    h264_iterator_t iterator;
    struct flv_writer_t *flv_writer;

    uint8_t is_inited;
};

static struct h264_raw_t ctx;

static uint8_t* h264_startcode(uint8_t* data, size_t bytes)
{
	size_t i;
	for (i = 2; i + 1 < bytes; i++)
	{
		if (0x01 == data[i] && 0x00 == data[i - 1] && 0x00 == data[i - 2])
			return data + i + 1;
	}

	return NULL;
}

static int h264_prepare_iterator_data(struct h264_raw_t *ctx, void *param, void *data, int size)
{
    h264_iterator_t *iter = (h264_iterator_t *)param;
    iter->data = data;
    iter->data_size = size;
    iter->end = iter->data + iter->data_size;
    iter->p = h264_startcode((uint8_t*)iter->data, iter->data_size);
    if (!iter->p) {
        printf("find not nalu!\r\n");
        return -1;
    }
    int avcc = mpeg4_h264_bitstream_format(iter->data, iter->data_size);
    if (avcc > 0) {
        printf("not support avcc format!\r\n");
        return -1;
    }

    ctx->ptr = data;
    ctx->vcl = 0;
    return 0;
}

static int h264_iterator(void *param, void **data, int *size)
{
    h264_iterator_t *ctx = (h264_iterator_t *)param;
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

    while (ctx->n > 0 && 0 == ctx->p[ctx->n - 1]) ctx->n--; // filter tailing zero

    if (ctx->n > 0)
    {
        if (data) *data = ctx->p;
        if (size) *size = ctx->n;
    } else {
        return -1;
    }

    ctx->p = ctx->next;

    return 0;
}

static void avc2flv(void* param, uint8_t* nalu, size_t bytes, uint32_t pts, uint32_t dts)
{
	struct h264_raw_t* ctx = (struct h264_raw_t*)param;
	assert(ctx->ptr < nalu);

#if 0
    uint8_t* ptr = nalu - 3;
    uint8_t nalutype = nalu[0] & 0x1f;
    printf("=========bytes:%d nalutype:%d\r\n ", bytes, nalutype);
    if (ctx->vcl > 0 && h264_is_new_access_unit((uint8_t*)nalu, bytes))
    {
        printf("!!!!!!==ctx->ptr:%p(size:%d) nalu:%p bytes:%d nalutype:%d\r\n ", ctx->ptr, ptr - ctx->ptr, nalu, bytes, nalutype);
        flv_muxer_avc(ctx->flv, ctx->ptr, ptr - ctx->ptr, pts, dts);
        ctx->ptr = ptr;
        ctx->vcl = 0;
    }

    if (1 <= nalutype && nalutype <= 5)
        ++ctx->vcl;
#else
    flv_muxer_avc(ctx->flv, nalu, bytes, pts, dts);
#endif
}

static int mem_write(void* param, const struct flv_vec_t* vec, int n)
{
    flv_writer_mem_t *mem = (flv_writer_mem_t *)param;
	int i;
	for(i = 0; i < n; i++)
	{
        if (vec[i].len + mem->offset > mem->max_size) {
            printf("memory write overflow! max:%ld curr:%ld\r\n", mem->max_size, mem->offset);
            return -1;
        }
        memcpy(mem->p + mem->offset, vec[i].ptr, vec[i].len);
        mem->offset += vec[i].len;
	}
	return 0;
}

static int mem_set_offset(void* param, size_t offset) {
    flv_writer_mem_t *mem = (flv_writer_mem_t *)param;
    if (!mem) return -1;

    mem->offset = offset;
    return 0;
}

static void* flv_mem_writer_create(int max_size)
{
    flv_writer_mem_t *mem;
    struct flv_writer_t* flv;

    mem = (flv_writer_mem_t *)malloc(sizeof(flv_writer_mem_t));
    if (!mem)
        return NULL;

    mem->max_size = max_size;
    mem->p = (uint8_t *)malloc(max_size);
    if (!mem->p) {
        free(mem);
        mem = NULL;
        return NULL;
    }

	flv = (struct flv_writer_t*)calloc(1, sizeof(*flv));
	if (!flv) {
        free(mem->p);
        mem->p = NULL;
        free(mem);
        mem = NULL;
        return NULL;
    }

	flv->write = mem_write;
	flv->param = mem;

	return flv;
}

static void flv_mem_writer_destroy(void *p)
{
	struct flv_writer_t* flv;
	flv = (struct flv_writer_t*)p;
    flv_writer_mem_t *mem = (flv_writer_mem_t *)flv->param;

	if (NULL != flv)
	{
		// flv_write_eos(flv);      //TODO
        if (mem) {
            if (mem->p) {
                free(mem->p);
                mem->p = NULL;
            }
            free(mem);
            mem = NULL;
        }
		free(flv);
	}
}

static int flv_write_eos(struct flv_writer_t* flv)
{
	int n;
	uint8_t header[16];
	struct flv_video_tag_header_t video;
	memset(&video, 0, sizeof(video));
	video.codecid = FLV_VIDEO_H264;
	video.keyframe = FLV_VIDEO_KEY_FRAME;
	video.avpacket = FLV_END_OF_SEQUENCE;
	video.cts = 0;

	n = flv_video_tag_header_write(&video, header, sizeof(header));
	return n > 0 ? flv_writer_input(flv, FLV_TYPE_VIDEO, header, n, 0) : -1;
}

int maix_avc2flv_init(int max_buff_size)
{
    if (ctx.is_inited) return 0;
    memset(&ctx, 0, sizeof(ctx));

    ctx.flv_writer = flv_mem_writer_create(max_buff_size);
    if (!ctx.flv_writer) {
        printf("init flv memory writer failed!\r\n");
        return -1;
    }

    ctx.flv = flv_muxer_create(flv_writer_input, ctx.flv_writer);
    if (!ctx.flv) {
        printf("init flv muxer failed!\r\n");
        flv_mem_writer_destroy(ctx.flv_writer);
        return -1;
    }

    ctx.is_inited = 1;
    return 0;
}

int maix_avc2flv_deinit()
{
    if (!ctx.is_inited) return 0;

    flv_muxer_destroy(ctx.flv);
    flv_mem_writer_destroy(ctx.flv_writer);

    ctx.is_inited = 0;
    return 0;
}

int maix_avc2flv_prepare(uint8_t *data, int data_size)
{
    return h264_prepare_iterator_data(&ctx, &ctx.iterator, data, data_size);
}

// 0, find nalu; other, find not nalu
int maix_avc2flv_iterate(void **nalu, int *size)
{
    return h264_iterator(&ctx.iterator, nalu, size);
}

int maix_avc2flv(void *nalu, int nalu_size, uint32_t pts, uint32_t dts, uint8_t **flv, int *flv_size)
{
    flv_writer_mem_t *mem = (flv_writer_mem_t *)ctx.flv_writer->param;
    mem_set_offset(mem, 0);

    avc2flv(&ctx, nalu, nalu_size, pts, dts);

    if (flv) *flv = mem->p;
    if (flv_size) *flv_size = mem->offset;

    return 0;
}

// need free data after used
int maix_flv_get_header(int audio, int video, uint8_t **data, int *size)
{
    #define FLV_HEADER_SIZE		9 // DataOffset included
    uint8_t *header = (uint8_t *)malloc(FLV_HEADER_SIZE + 4);
    if (!header) {
        printf("malloc failed!\r\n");
        return -1;
    }

    flv_header_write(audio, video, header, FLV_HEADER_SIZE);
    flv_tag_size_write(header + FLV_HEADER_SIZE, 4, 0); // PreviousTagSize0(Always 0)

    if (data) {
        *data = header;
    } else {
        free(header);
        header = NULL;
    }
    if (size) *size = FLV_HEADER_SIZE + 4;

    return 0;
}

// need free data after used
int maix_flv_get_tail(uint8_t **data, int *size)
{
    if (!ctx.is_inited) return -1;
    flv_writer_mem_t *mem = (flv_writer_mem_t *)ctx.flv_writer->param;
    mem_set_offset(mem, 0);
    flv_write_eos(ctx.flv_writer);

    uint8_t *tail = (uint8_t *)malloc(mem->offset);
    if (!tail) {
        printf("malloc failed!\r\n");
        return -1;
    }
    memcpy(tail, mem->p, mem->offset);

    if (data) {
        *data = tail;
    } else {
        free(tail);
    }
    if (size) *size = mem->offset;
    return 0;
}

#include "flv-writer.h"
#include "flv-muxer.h"
#include "flv-header.h"
#include "mpeg4-avc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct {
    ptrdiff_t n;
    uint8_t *p, *next, *end;
    uint8_t *data;
    int data_size;
} h264_iterator_t;

struct h264_raw_t
{
	flv_muxer_t* flv;
	uint32_t pts, dts;
	uint8_t* ptr;
    int vcl;
};

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

static int h264_prepare_iterator_data(void *param, void *data, int size)
{
    h264_iterator_t *ctx = (h264_iterator_t *)param;
    ctx->data = data;
    ctx->data_size = size;
    ctx->end = ctx->data + ctx->data_size;
    ctx->p = h264_startcode((uint8_t*)ctx->data, ctx->data_size);
    if (!ctx->p) {
        printf("find not nalu!\r\n");
        return -1;
    }
    int avcc = mpeg4_h264_bitstream_format(ctx->data, ctx->data_size);
    if (avcc > 0) {
        printf("not support avcc format!\r\n");
        return -1;
    }printf("=[%s][%d]\r\n", __func__, __LINE__);
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

    // while (ctx->n > 0 && 0 == ctx->p[ctx->n - 1]) ctx->n--; // filter tailing zero

    if (ctx->n > 0)
    {
        if (data) *data = ctx->p - 3;
        if (size) *size = ctx->n + 3;
    } else {
        return -1;
    }

    ctx->p = ctx->next;

    return 0;
}

static int on_flv_packet(void* flv, int type, const void* data, size_t bytes, uint32_t timestamp)
{
	return flv_writer_input(flv, type, data, bytes, timestamp);
}

static void h264_handler(void* param, uint8_t* nalu, size_t bytes)
{
	struct h264_raw_t* ctx = (struct h264_raw_t*)param;
	assert(ctx->ptr < nalu);
static int cnt = 0, cnt2 = 0;
    uint8_t* ptr = nalu - 3;
//    uint8_t* end = (uint8_t*)nalu + bytes;
    uint8_t nalutype = nalu[0] & 0x1f;
    if (ctx->vcl > 0 && h264_is_new_access_unit((uint8_t*)nalu, bytes))
    {
        flv_muxer_avc(ctx->flv, ctx->ptr, ptr - ctx->ptr, ctx->pts, ctx->dts);
        ctx->pts += 40;
        ctx->dts += 40;

        ctx->ptr = ptr;
        ctx->vcl = 0;

        cnt2 ++;
    }

    if (1 <= nalutype && nalutype <= 5)
        ++ctx->vcl;

printf("[%d/%d] nalutype:%d frame:%p bytes:%d  \
        ctx.pts:%d ctx.vcl:%d \r\n",
        cnt2, ++cnt, nalutype, nalu, bytes, ctx->pts, ctx->vcl);
}


// need free data after used
int flv_get_header(int audio, int video, uint8_t **data, int *size)
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

static int mem_write(void* param, const struct flv_vec_t* vec, int n)
{printf("===write:%d", n);
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

void avc2flv_test(const char* inputH264, const char* outputFLV)
{
#if 1
	struct h264_raw_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	void* f = flv_writer_create(outputFLV);
	ctx.flv = flv_muxer_create(on_flv_packet, f);
	FILE* fp = fopen(inputH264, "rb");

	static uint8_t buffer[32 * 1024 * 1024];
	size_t n = fread(buffer, 1, sizeof(buffer), fp);
	ctx.ptr = buffer;
	mpeg4_h264_annexb_nalu(buffer, n, h264_handler, &ctx);
	fclose(fp);

	flv_muxer_destroy(ctx.flv);
	flv_writer_destroy(f);
#else
    FILE *file = fopen("outputFLV", "wb");

	struct h264_raw_t ctx;
	memset(&ctx, 0, sizeof(ctx));
	// void* f = flv_writer_create(outputFLV);

    uint8_t *header;
    int header_size;
    if (0 != flv_get_header(1, 1, &header, &header_size)) {
        printf("flv get header failed!\r\n");
    }
    fwrite(header, header_size, 1, file);
    free(header);

    void * f = flv_mem_writer_create(128 * 1024);
    ctx.flv = flv_muxer_create(flv_writer_input, f);

	// ctx.flv = flv_muxer_create(on_flv_packet, f);
	FILE* fp = fopen(inputH264, "rb");

	static uint8_t buffer[32 * 1024 * 1024];
	size_t n = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
	ctx.ptr = buffer;
    ctx.vcl = 0;
    // mpeg4_h264_annexb_nalu(buffer, n, h264_handler, &ctx);

    struct flv_writer_t *flv = (struct flv_writer_t *)f;
    flv_writer_mem_t *mem = (flv_writer_mem_t *)flv->param;

    printf("buffer:%p bytes:%d\r\n", buffer, n);

    h264_iterator_t iter;
    h264_prepare_iterator_data(&iter, buffer, n);
    printf("buffer:%p n:%d\r\n", buffer, n);
    void *frame;
    int bytes = 0;

    static int total = 0, cnt = 0, cnt2 = 0;
    ctx.vcl = 0;
    while (0 == h264_iterator(&iter, &frame, &bytes)) {
        mem_set_offset(mem, 0);
        uint8_t *nalu = (uint8_t *)frame + 3;
        uint8_t* ptr = (uint8_t *)frame;
        uint8_t nalutype = nalu[0] & 0x1f;
        if (ctx.vcl > 0 && h264_is_new_access_unit((uint8_t*)nalu, bytes - 3))
        {printf("===[%s][%d] frame:%p bytes:%d\r\n", __func__, __LINE__,frame, bytes);
        //     // flv_muxer_avc(ctx.flv, ctx.ptr, ptr - ctx.ptr, ctx.pts, ctx.dts);
            int res = flv_muxer_avc(ctx.flv, frame, bytes, ctx.pts, ctx.dts);
            if (res < 0) {
                printf("unknow error :%d\r\n", res);
            }
            ctx.pts += 40;
            ctx.dts += 40;
            ctx.vcl = 0;

            cnt2 += 1;
        }

        if (1 <= nalutype && nalutype <= 5)
            ++ctx.vcl;

        total += bytes;
        printf("[%d/%d] nalutype:%d frame:%p bytes:%d mem size:%d \
        ctx.pts:%d ctx.vcl:%d total:%d\r\n",
        cnt2, ++cnt, nalutype, frame, bytes, mem->offset, ctx.pts, ctx.vcl, total);
    }

	flv_muxer_destroy(ctx.flv);
	// flv_writer_destroy(f);
    flv_mem_writer_destroy(f);
#endif
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("try: ./test output.h264 output.flv");
    }

    avc2flv_test(argv[1], argv[2]);
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "maix_avc2flv.h"
#include "sys/system.h"

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

static void avc2flv_test(const char* inputH264, const char* outputFLV)
{
	static uint8_t buffer[32 * 1024 * 1024];
    FILE* fp = fopen(inputH264, "rb");
	size_t n = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);
    
    FILE *file = fopen(outputFLV, "wb");

    assert(0 == maix_avc2flv_init(256 * 1024));

    uint8_t *header;
    int header_size;
    assert(0 == maix_flv_get_header(1, 1, &header, &header_size));
    fwrite(header, header_size, 1, file);
    free(header);

    // assert(0 == maix_avc2flv_prepare(buffer, n));


#if 1
	int r, type;
	// int avcrecord = 0;
    int aacconfig = 0;
	size_t taglen;
	uint32_t timestamp;
	uint32_t s_timestamp = 0;
	uint32_t diff = 0;
	uint64_t clock;
    size_t buffer_size = n;
    // while (1)

    uint8_t *tmp = malloc(512 * 1024);
    int tmp_offset = 0;
    int is_ready = 0;
	{
		uint8_t *p, *next, *end;
		size_t n;
		clock = system_clock(); // timestamp start from 0

		p = h264_startcode(buffer, buffer_size);
		end = buffer + buffer_size;

		while (1)
		{
			int pts = 0, dts = 0;
			uint64_t t = system_clock();
			if (clock + timestamp > t && clock + timestamp < t + 3 * 1000) // dts skip
				system_sleep(clock + timestamp - t);
			else if (clock + timestamp > t + 3 * 1000)
				clock = t - timestamp;
			
			timestamp += diff;
			s_timestamp = timestamp > s_timestamp ? timestamp : s_timestamp;

			next = h264_startcode(p, (int)(end - p));
			if (next)
			{
				n = next - p - 3;
			}
			else
			{
				n = end - p;
				break;
			}

			while (n > 0 && 0 == p[n - 1]) n--; // filter tailing zero

			if (n > 0)
			{
				// enum { NAL_NIDR = 1, NAL_PARTITION_A = 2,
				//  NAL_IDR = 5, NAL_SEI = 6, 
				// NAL_SPS = 7, NAL_PPS = 8, NAL_AUD = 9, };
				uint8_t nalutype = p[0] & 0x1f;
                {
                    void *nalu = p;
                    int size = n;
                    static uint32_t pts = 0, dts = 0;

                    if (!is_ready) {
                        if (nalutype == 0x7 || nalutype == 0x8 || nalutype == 6) {
                            tmp[tmp_offset ++] = 0x00;
                            tmp[tmp_offset ++] = 0x00;
                            tmp[tmp_offset ++] = 0x01;
                            memcpy(tmp + tmp_offset, p, n);
                            tmp_offset += n;
                        } else if (nalutype == 0x5 || nalutype == 0x1) {
                            tmp[tmp_offset ++] = 0x00;
                            tmp[tmp_offset ++] = 0x00;
                            tmp[tmp_offset ++] = 0x01;
                            memcpy(tmp + tmp_offset, p, n);
                            tmp_offset += n;
                            is_ready = 1;
                        }
                    }

                    if (is_ready) {
                        uint8_t *flv;
                        int flv_size;
                        // assert(0 == maix_avc2flv_prepare(nalu, size));
                        assert(0 == maix_avc2flv(tmp, tmp_offset, pts, dts, &flv, &flv_size));
                        pts += 40;
                        dts += 40;

                        printf("get out size %d\r\n", flv_size);
                        fwrite(flv, flv_size, 1, file);

                        is_ready = 0;
                        tmp_offset = 0;
                    }
                }
				printf("buffer:%p data: %p, size:%d type:%d(%#x)\r\n", buffer, p, n, nalutype, p[0]);
			} else {
				return -1;
			}

			p = next;

		}

		diff = s_timestamp + 30;
	}
#else
    void *nalu;
    int size;
    uint32_t pts = 0, dts = 0;
    while (0 == maix_avc2flv_iterate(&nalu, &size)) {
        uint8_t *flv;
        int flv_size;
        assert(0 == maix_avc2flv(nalu, size, pts, dts, &flv, &flv_size));
        pts += 40;
        dts += 40;

        fwrite(flv, flv_size, 1, file);
    }
#endif
    uint8_t *tail;
    int tail_size;
    assert(0 == maix_flv_get_tail(&tail, &tail_size));
    fwrite(tail, tail_size, 1, file);
    free(tail);

    assert(0 == maix_avc2flv_deinit());

    fclose(file);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("try: ./test output.h264 output.flv");
    }

    avc2flv_test(argv[1], argv[2]);
}
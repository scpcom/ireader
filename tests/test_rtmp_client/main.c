#include<stdio.h>

#include "sockutil.h"
#include "sys/system.h"
#include "rtmp-client.h"
#include "flv-reader.h"
#include "flv-header.h"
#include "flv-proto.h"
#include "flv-writer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "maix_avc2flv.h"

#define AAC_ADTS_HEADER_SIZE 7
#define FLV_TAG_HEAD_LEN 11
#define FLV_PRE_TAG_LEN 4

//#define CORRUPT_RTMP_CHUNK_DATA

#if defined(CORRUPT_RTMP_CHUNK_DATA)
static void rtmp_corrupt_data(const void* data, size_t bytes)
{
    static unsigned int seed;
    if (0 == seed)
    {
        seed = (unsigned int)time(NULL);
        srand(seed);
    }

    if (bytes < 1)
        return;

    //size_t i = bytes > 20 ? 20 : bytes;
    //i = rand() % i;

    //uint8_t v = ((uint8_t*)data)[i];
    //((uint8_t*)data)[i] = rand() % 255;
    //printf("rtmp_corrupt_data[%d] %d == %d\n", i, (int)v, (int)((uint8_t*)data)[i]);
 
    if (5 == rand() % 10)
    {
        size_t i = rand() % bytes;
        uint8_t v = ((uint8_t*)data)[i];
        ((uint8_t*)data)[i] = rand() % 255;
        printf("rtmp_corrupt_data[%d] %d == %d\n", i, (int)v, (int)((uint8_t*)data)[i]);
    }
}

static uint8_t s_buffer[4 * 1024 * 1024];
static size_t s_offset;
static FILE* s_fp;
static void fwritepacket(uint32_t timestamp)
{
    assert(4 == fwrite(&s_offset, 1, 4, s_fp));
    assert(4 == fwrite(&timestamp, 1, 4, s_fp));
    assert(s_offset == fwrite(s_buffer, 1, s_offset, s_fp));
    s_offset = 0;
}
#endif

static int rtmp_client_send(void* param, const void* header, size_t len, const void* data, size_t bytes)
{
	socket_t* socket = (socket_t*)param;
	socket_bufvec_t vec[2];
	socket_setbufvec(vec, 0, (void*)header, len);
	socket_setbufvec(vec, 1, (void*)data, bytes);

#if defined(CORRUPT_RTMP_CHUNK_DATA)
	//if (len > 0)
	//{
	//    assert(s_offset + len < sizeof(s_buffer));
	//    memcpy(s_buffer + s_offset, header, len);
	//    s_offset += len;
	//}
	//if (bytes > 0)
	//{
	//    assert(s_offset + bytes < sizeof(s_buffer));
	//    memcpy(s_buffer + s_offset, data, bytes);
	//    s_offset += bytes;
	//}
	
	rtmp_corrupt_data(header, len);
    rtmp_corrupt_data(data, bytes);
#endif
	return socket_send_v_all_by_time(*socket, vec, bytes > 0 ? 2 : 1, 0, 5000);
}

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

int maix_rtmp_client_push_h264(rtmp_client_t *client, struct flv_vec_t *vec, int num, uint32_t timestamp)
{
	uint8_t need_prepare = 0;
	int body_len = 0;
	int output_len = 0;
	uint8_t *output = NULL;
	int offset = 0;
	uint8_t is_ready = 0;

	int tmp_offset = 0;
	int tmp_max_size = 256 * 1024;
	uint8_t *tmp = malloc(tmp_max_size);
	if (!tmp) return -1;

	for (int i = 0; i < num; i ++) {
		uint8_t *nalu = vec[i].ptr;
		int nalu_size = vec[i].len;
		uint8_t nalutype = nalu[0] & 0x1f;

		if (nalutype == 0x7 || nalutype == 0x8 || nalutype == 6) {
			if (tmp_offset + 3 + nalu_size > tmp_max_size) return -1;

			tmp[tmp_offset ++] = 0x00;
			tmp[tmp_offset ++] = 0x00;
			tmp[tmp_offset ++] = 0x01;
			memcpy(tmp + tmp_offset, nalu, nalu_size);
			tmp_offset += nalu_size;
		}  else if (nalutype == 0x5 || nalutype == 0x1) {
			if (tmp_offset + 3 + nalu_size > tmp_max_size) return -1;

			tmp[tmp_offset ++] = 0x00;
			tmp[tmp_offset ++] = 0x00;
			tmp[tmp_offset ++] = 0x01;
			memcpy(tmp + tmp_offset, nalu, nalu_size);
			tmp_offset += nalu_size;


			uint8_t *flv;
			int flv_size;
			int flv_offset = 0;
			uint8_t *flv_next = flv;
			if (0 != maix_avc2flv(tmp, tmp_offset, timestamp, timestamp, &flv, &flv_size)) {
				printf("maix_avc2flv failed!\r\n");
				return -1;
			}

			while (1) {
				if (flv_next >= (flv + flv_size)) break;
				struct flv_tag_header_t tag_header = {0};
				flv_tag_header_read(&tag_header, flv_next, FLV_TAG_HEAD_LEN);


				printf("flv:%p flv_next:%p tag len:%d timestamp:%d\r\n", flv, flv_next, tag_header.size, timestamp);

				rtmp_client_push_video(client, flv + FLV_TAG_HEAD_LEN, tag_header.size, timestamp);
				flv_next += FLV_PRE_TAG_LEN + FLV_TAG_HEAD_LEN + tag_header.size;
			}

			tmp_offset = 0;
			is_ready = 0;
		}
	}

	return 0;
}

static void rtmp_client_push_h264(const char* flv, rtmp_client_t* rtmp)
{
	int r, type;
	// int avcrecord = 0;
    int aacconfig = 0;
	size_t taglen;
	uint32_t timestamp;
	uint32_t s_timestamp = 0;
	uint32_t diff = 0;
	uint64_t clock;
	
	static char packet[2 * 1024 * 1024];

	static uint8_t buffer[32 * 1024 * 1024];
    FILE* fp = fopen(flv, "rb");
	size_t buffer_size = fread(buffer, 1, sizeof(buffer), fp);
    fclose(fp);

	maix_avc2flv_init(512 * 1024);
	uint8_t *tmp = malloc(512 * 1024);
	int tmp_offset = 0;
	int is_ready = 0;
	timestamp = 0;
	while (1)
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
#if 0
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

						int flv_offset = 0;
						uint8_t *flv_next = flv;
						while (1) {
							if (flv_next >= (flv + flv_size)) break;
							struct flv_tag_header_t tag_header = {0};
							flv_tag_header_read(&tag_header, flv_next, FLV_TAG_HEAD_LEN);


							printf("flv:%p flv_next:%p tag len:%d timestamp:%d\r\n", flv, flv_next, tag_header.size, timestamp);
							// for(int i = 0; i < 16; i ++) {
							// 	printf("%#x ", flv_next[i]);
							// }printf("\r\n");
							rtmp_client_push_video(rtmp, flv + 11, tag_header.size, timestamp);
							flv_next += FLV_PRE_TAG_LEN + FLV_TAG_HEAD_LEN + tag_header.size;
						}
						system_sleep(40);
						timestamp += 40;
                        // fwrite(flv, flv_size, 1, file);
						struct flv_tag_header_t tag_header = {0};
						flv_tag_header_read(&tag_header, flv, flv_size);

						struct flv_video_tag_header_t video_tag_header = {0};
						int header_size = flv_video_tag_header_read(&video_tag_header, flv + 11, flv_size - 11);
                        is_ready = 0;
                        tmp_offset = 0;
                    }
                }
#else
				// flv_ve
				// maix_rtmp_client_push_h264(rtmp, )
#endif
			} else {
				return -1;
			}

			p = next;
			// diff = s_timestamp + 30;
		}

		
	}

// EXIT:
	return;
}

static void rtmp_client_push(const char* flv, rtmp_client_t* rtmp)
{
	int r, type;
	// int avcrecord = 0;
    int aacconfig = 0;
	size_t taglen;
	uint32_t timestamp;
	uint32_t s_timestamp = 0;
	uint32_t diff = 0;
	uint64_t clock;
	
	static char packet[2 * 1024 * 1024];
	while (1)
	{
		void* f = flv_reader_create(flv);

		clock = system_clock(); // timestamp start from 0
		size_t total = 0;
		while (1 == flv_reader_read(f, &type, &timestamp, &taglen, packet, sizeof(packet)))
		{
			uint64_t t = system_clock();
			if (clock + timestamp > t && clock + timestamp < t + 3 * 1000) // dts skip
				system_sleep(clock + timestamp - t);
			else if (clock + timestamp > t + 3 * 1000)
				clock = t - timestamp;
			
			timestamp += diff;
			s_timestamp = timestamp > s_timestamp ? timestamp : s_timestamp;

			if (FLV_TYPE_AUDIO == type)
			{
                if (0 == packet[1])
                {
                    if(0 != aacconfig)
                        continue;
                    aacconfig = 1;
                }
				r = rtmp_client_push_audio(rtmp, packet, taglen, timestamp);
			}
			else if (FLV_TYPE_VIDEO == type)
			{
				r = rtmp_client_push_video(rtmp, packet, taglen, timestamp);
			}
			else if (FLV_TYPE_SCRIPT == type)
			{
				r = rtmp_client_push_script(rtmp, packet, taglen, timestamp);
			}
			else
			{
				assert(0);
				r = 0; // ignore
			}

			if (0 != r)
			{
				assert(0);
				break; // TODO: handle send failed
			}
		}

		flv_reader_destroy(f);

		diff = s_timestamp + 30;
	}

// EXIT:
	return;
}

// rtmp://video-center.alivecdn.com/live/hello?vhost=your.domain
// rtmp_publish_test("video-center.alivecdn.com", "live", "hello?vhost=your.domain", local-flv-file-name)
void rtmp_publish_test(const char* host, const char* app, const char* stream, const char* flv)
{
	static char packet[2 * 1024 * 1024];
	snprintf(packet, sizeof(packet), "rtmp://%s/%s", host, app); // tcurl

	struct rtmp_client_handler_t handler;
	memset(&handler, 0, sizeof(handler));
	handler.send = rtmp_client_send;

	socket_init();
	socket_t socket = socket_connect_host(host, 1935, 2000);
	socket_setnonblock(socket, 0);

	rtmp_client_t* rtmp = rtmp_client_create(app, stream, packet/*tcurl*/, &socket, &handler);
	int r = rtmp_client_start(rtmp, 0);

	while (4 != rtmp_client_getstate(rtmp) && (r = socket_recv(socket, packet, sizeof(packet), 0)) > 0)
	{
		assert(0 == rtmp_client_input(rtmp, packet, r));
	}

	// rtmp_client_push(flv, rtmp);
	rtmp_client_push_h264(flv, rtmp);

	rtmp_client_destroy(rtmp);
	socket_close(socket);
	socket_cleanup();
}


int main(int argc, char **argv)
{
    if (argc < 5) {
        printf("try: ./test 10.167.53.100 myapp test /root/test.flv\r\n");
        exit(0);
    }

    // rtmp_publish_test("10.167.53.100", "myapp", "test", "/root/test.flv");
    rtmp_publish_test(argv[1], argv[2], argv[3], argv[4]);
    return 0;
}

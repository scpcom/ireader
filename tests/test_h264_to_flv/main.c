#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "maix_avc2flv.h"

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

    assert(0 == maix_avc2flv_prepare(buffer, n));

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
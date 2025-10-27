/****************************************************************************
*   Merged Voice Processing: Voiceprint + KWS
*   Sequential processing in single binary
****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <math.h>

#include "vsi_nn_pub.h"
#include "vnn_global.h"
#include "vnn_pre_process.h"
#include "vnn_post_process.h"
#include "vnn_voiceprintuint8.h"
#include "vnn_kwsmodel.h"
#include "my_struct.h"

#define SAMPLE_RATE 16000
#define VOICEPRINT_SECONDS 3
#define KWS_SECONDS 1
#define MAX_SAMPLES_VOICEPRINT (SAMPLE_RATE * VOICEPRINT_SECONDS)
#define MAX_SAMPLES_KWS (SAMPLE_RATE * KWS_SECONDS)

#define RTSP_URL "rtsp://adminOnvif:Vision123@10.60.1.136/Streaming/Channels/102?transportmode=unicast&profile=Profile_1"
#define IP_CAM_SRC_RATE 48000
#define IP_CAM_CHANNELS 1
#define BYTES_PER_SAMPLE 2

#ifdef __linux__
#define VSI_UINT64_SPECIFIER PRIu64
#elif defined(_WIN32)
#define VSI_UINT64_SPECIFIER "I64u"
#endif

static float voiceprint_buffer[MAX_SAMPLES_VOICEPRINT];
static float kws_buffer[MAX_SAMPLES_KWS];
static int quiet_mode = 0;

// Function declarations
int record_from_ipcam_and_resample(float* buffer, int max_samples, const char* rtsp_url);
vsi_status vnn_VerifyGraph(vsi_nn_graph_t* graph);

int read_media_to_mono16k_with_ffmpeg(const char* path, float* buffer, int max_samples)
{
    if (!path || !buffer || max_samples <= 0)
        return -1;

    // We will read at most max_samples in s16le
    const size_t dst_bytes_total = (size_t)max_samples * BYTES_PER_SAMPLE;
    int16_t* tmp = (int16_t*)malloc(dst_bytes_total);
    if (!tmp)
    {
        fprintf(stderr, "[FFMPEG] Unable to allocate %zu bytes\n", dst_bytes_total);
        return -1;
    }

    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "ffmpeg -nostdin -i \"%s\" -vn -ac 1 -ar %d -f s16le -hide_banner -loglevel warning - 2>/dev/null",
             path, SAMPLE_RATE);

    FILE* fp = popen(cmd, "r");
    if (!fp)
    {
        fprintf(stderr, "[FFMPEG] Failed to start ffmpeg. Is it installed?\n");
        free(tmp);
        return -1;
    }

    size_t got = 0;
    while (got < dst_bytes_total)
    {
        size_t need = dst_bytes_total - got;
        size_t n = fread((uint8_t*)tmp + got, 1, need, fp);
        if (n > 0)
        {
            got += n;
            continue;
        }
        if (feof(fp) || ferror(fp))
            break;
    }
    pclose(fp);

    int samples = (int)(got / BYTES_PER_SAMPLE);
    // Convert to float and zero-pad remainder
    for (int i = 0; i < samples; ++i)
        buffer[i] = (float)tmp[i] / 32768.0f;
    for (int i = samples; i < max_samples; ++i)
        buffer[i] = 0.0f;

    free(tmp);
    return samples;
}

static void print_embedding_json(const Voiceprint_feature* feat)
{
    // Minimal JSON to stdout, single line
    printf("{\"feature_num\":%d,\"vector\":[", feat->feature_num);
    for (int i = 0; i < feat->feature_num; ++i)
    {
        printf("%s%f", (i == 0 ? "" : ","), feat->vals[i].value);
    }
    printf("]}\n");
}

static void print_usage(const char* prog_name) {
    printf("Usage:\n");
    printf("  %s stream <rtsp_url>    - Continuous merged processing (voiceprint + KWS)\n", prog_name);
    printf("  %s embed_wav <wav_file> <user_name> - Register user voice\n", prog_name);
}

/* ================= IPCAM audio capture + resampling =================
 * Equivalent to original "record_and_resample_microphone" interface
 * Internally calls ffmpeg: -vn -ac 1 -ar 48000 -f s16le - (reads raw PCM s16le from stdout)
 */
int record_from_ipcam_and_resample(float* buffer, int max_samples, const char* rtsp_url)
{
    const int dst_total = SAMPLE_RATE * VOICEPRINT_SECONDS; // 16k * 3s = 48000
    if (max_samples < dst_total) {
        fprintf(stderr, "[IPCAM] buffer too small, need at least %d samples\n", dst_total);
        return -1;
    }

    const int src_total = IP_CAM_SRC_RATE * VOICEPRINT_SECONDS;       // 48k * 3s = 144000
    const size_t src_bytes_total = (size_t)src_total * BYTES_PER_SAMPLE;

    int16_t* src = (int16_t*)malloc(src_bytes_total);
    if (!src) {
        fprintf(stderr, "[IPCAM] Unable to allocate buffer (%zu bytes)\n", src_bytes_total);
        return -1;
    }

    // Add -t 3.2 (3s + 0.2s buffer), -stimeout 3000000 (3s connection timeout)
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -nostdin -rtsp_transport tcp -stimeout 3000000 -t 3.2 -i \"%s\" "
        "-vn -ac %d -ar %d -f s16le -hide_banner -loglevel warning - 2>/dev/null",
        rtsp_url, IP_CAM_CHANNELS, IP_CAM_SRC_RATE);

    FILE* fp = popen(cmd, "r");
    if (!fp) {
        fprintf(stderr, "[IPCAM] Unable to start ffmpeg, please ensure it is installed and in PATH\n");
        free(src);
        return -1;
    }

    // Max 5 second wall timeout to avoid infinite waiting
    const int MAX_WAIT_MS = 5000;
    int waited_ms = 0;

    size_t got = 0;
    int result_status = -1;  // Track success/failure for cleanup
    
    while (got < src_bytes_total) {
        size_t need = src_bytes_total - got;
        size_t n = fread((uint8_t*)src + got, 1, need, fp);

        if (n > 0) {
            got += n;
            continue;
        }

        // No data read, check if EOF or temporarily unavailable
        if (feof(fp)) {
            // ffmpeg finished (e.g., -t timeout reached), break early
            break;
        }
        if (ferror(fp)) {
            perror("[IPCAM] fread error");
            goto cleanup;
        }

        // No data but not EOF/ERROR: may be waiting for data => sleep to avoid busy-wait
        usleep(10000); // 10ms
        waited_ms += 10;
        if (waited_ms >= MAX_WAIT_MS) {
            fprintf(stderr, "[IPCAM] Read timeout\n");
            goto cleanup;
        }
    }

    int src_samples = (int)(got / BYTES_PER_SAMPLE);
    if (src_samples <= 0) {
        fprintf(stderr, "[IPCAM] No valid audio data received. Check network connection and RTSP URL.\n");
        goto cleanup;
    }

    // Linear resampling to fixed length dst_total
    double step = (double)src_samples / (double)dst_total;
    for (int i = 0; i < dst_total; ++i) {
        double pos = i * step;
        int idx = (int)pos;
        double frac = pos - idx;

        int16_t s1 = (idx < src_samples) ? src[idx] : 0;
        int16_t s2 = (idx + 1 < src_samples) ? src[idx + 1] : s1;

        float v1 = (float)s1 / 32768.0f;
        float v2 = (float)s2 / 32768.0f;

        buffer[i] = (float)((1.0 - frac) * v1 + frac * v2);
    }
    
    result_status = dst_total;  // Success

cleanup:
    if (fp) {
        pclose(fp);
    }
    free(src);
    
    return result_status;
}

#define BILLION                                 1000000000
static uint64_t get_perf_count()
{
#if defined(__linux__) || defined(__ANDROID__) || defined(__QNX__) || defined(__CYGWIN__)
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)((uint64_t)ts.tv_nsec + (uint64_t)ts.tv_sec * BILLION);
#elif defined(_WIN32) || defined(UNDER_CE)
    LARGE_INTEGER freq;
    LARGE_INTEGER ln;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ln);

    return (uint64_t)(ln.QuadPart * BILLION / freq.QuadPart);
#endif
}

vsi_status vnn_VerifyGraph
    (
    vsi_nn_graph_t *graph
    )
{
    vsi_status status = VSI_FAILURE;
    uint64_t tmsStart, tmsEnd, msVal, usVal;

    /* Verify graph */
    printf("Verify...\n");
    tmsStart = get_perf_count();
    status = vsi_nn_VerifyGraph( graph );
    tmsEnd = get_perf_count();
    msVal = (tmsEnd - tmsStart)/1000000;
    usVal = (tmsEnd - tmsStart)/1000;
    printf("Verify Graph: %"VSI_UINT64_SPECIFIER"ms or %"VSI_UINT64_SPECIFIER"us\n", msVal, usVal);

    return status;
}


int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    const char* command = argv[1];

    if (strcmp(command, "stream") == 0) {
        // Merged streaming mode
        const char* rtsp_url = (argc >= 3) ? argv[2] : RTSP_URL;

        fprintf(stderr, "[SuperSet] Starting merged voice processing...\n");
        fprintf(stderr, "[SuperSet] RTSP URL: %s\n", rtsp_url);

        // Load both models
        vsi_nn_graph_t* kws_graph = vnn_CreateKwsModel("keyword_spotting.export.data", NULL,
                      vnn_GetPreProcessMap(), vnn_GetPreProcessMapCount(),
                      vnn_GetPostProcessMap(), vnn_GetPostProcessMapCount());
        vsi_nn_graph_t* voiceprint_graph = vnn_CreateVoiceprintUint8("network_binary.nb", NULL,
                      vnn_GetPreProcessMap(), vnn_GetPreProcessMapCount(),
                      vnn_GetPostProcessMap(), vnn_GetPostProcessMapCount());

        if (!voiceprint_graph || !kws_graph) {
            fprintf(stderr, "Failed to create neural network graphs\n");
            return 1;
        }

        // Verify graphs
        if (vnn_VerifyGraph(voiceprint_graph) != VSI_SUCCESS ||
            vnn_VerifyGraph(kws_graph) != VSI_SUCCESS) {
            fprintf(stderr, "Failed to verify graphs\n");
            return 1;
        }

        printf("{\"event\":\"debug\",\"message\":\"SuperSet merged processing started\"}\n");
        fflush(stdout);

        // Main processing loop
        while (1) {
            // Phase 1: Voiceprint (3 seconds)
            printf("{\"event\":\"debug\",\"message\":\"Voiceprint phase starting\"}\n");
            fflush(stdout);

            int voiceprint_samples = record_from_ipcam_and_resample(
                voiceprint_buffer, MAX_SAMPLES_VOICEPRINT, rtsp_url);

            if (voiceprint_samples > 0) {
                // Preprocess and run voiceprint
                vnn_PreProcessVoiceprintUint8FromBuffer(voiceprint_graph, voiceprint_buffer, voiceprint_samples);
                vsi_nn_RunGraph(voiceprint_graph);
                
                Voiceprint_feature result;
                vnn_PostProcessVoiceprintUint8(voiceprint_graph, &result);
                // Output embedding JSON for STB processing
                print_embedding_json(&result);
            }

            // Phase 2: KWS (1 second)
            printf("{\"event\":\"debug\",\"message\":\"KWS phase starting\"}\n");
            fflush(stdout);

            int kws_samples = record_from_ipcam_and_resample(
                kws_buffer, MAX_SAMPLES_KWS, rtsp_url);

            if (kws_samples > 0) {
                // Preprocess and run KWS
                vnn_PreProcessKwsModelFromBuffer(kws_graph, kws_buffer, kws_samples);
                vsi_nn_RunGraph(kws_graph);
                vnn_PostProcessKwsModel(kws_graph);
            }

            // Small delay between cycles
            usleep(100000);  // 100ms
        }

        // Cleanup (never reached)
        vnn_ReleaseVoiceprintUint8(voiceprint_graph, TRUE);
        vnn_ReleaseKwsModel(kws_graph, TRUE);

    } else if (strcmp(command, "embed_wav") == 0) {
        // Registration mode - use voiceprint only
        if (argc < 4) {
            fprintf(stderr, "Usage: %s embed_wav <wav_file> <user_name>\n", argv[0]);
            return 1;
        }

        const char* wav_file = argv[2];
        const char* user_name = argv[3];

        // Load only voiceprint model for registration
        vsi_nn_graph_t* voiceprint_graph = vnn_CreateVoiceprintUint8("network_binary.nb", NULL,
                      vnn_GetPreProcessMap(), vnn_GetPreProcessMapCount(),
                      vnn_GetPostProcessMap(), vnn_GetPostProcessMapCount());
        if (!voiceprint_graph) {
            fprintf(stderr, "Failed to create voiceprint graph\n");
            return 1;
        }

        // Process WAV file for registration (same as voiceprint embed_wav)
        memset(voiceprint_buffer, 0, sizeof(voiceprint_buffer));
        int samples = read_media_to_mono16k_with_ffmpeg(wav_file, voiceprint_buffer, MAX_SAMPLES_VOICEPRINT);

        if (samples <= 0) {
            fprintf(stderr, "❌ Failed to read or process WAV file: %s\n", wav_file);
            vnn_ReleaseVoiceprintUint8(voiceprint_graph, TRUE);
            return 1;
        }

        vsi_status status = vnn_PreProcessVoiceprintUint8FromBuffer(voiceprint_graph, voiceprint_buffer, samples);
        status = vsi_nn_RunGraph(voiceprint_graph);

        Voiceprint_feature result;
        status = vnn_PostProcessVoiceprintUint8(voiceprint_graph, &result);

        if (status == VSI_SUCCESS) {
            // Output the embedding as JSON for STB to handle storage
            print_embedding_json(&result);
        } else {
            fprintf(stderr, "❌ Failed to process audio feature.\n");
            vnn_ReleaseVoiceprintUint8(voiceprint_graph, TRUE);
            return 1;
        }

        vnn_ReleaseVoiceprintUint8(voiceprint_graph, TRUE);

    } else {
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}


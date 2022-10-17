#include <ctype.h>
#include "erl_nif.h"
#include "naive.h"

//https://stackoverflow.com/questions/17639213/millisecond-precision-timing-of-functions-in-c-crossplatform
#if defined(_MSC_VER)
#include <windows.h>
#define TIME_TYPE LARGE_INTEGER
#define TIME_GET(X) QueryPerformanceCounter(&X);

long diff_micro(LARGE_INTEGER *start, LARGE_INTEGER *end)
{
    LARGE_INTEGER Frequency, elapsed;

    QueryPerformanceFrequency(&Frequency); 
    elapsed.QuadPart = end->QuadPart - start->QuadPart;

    elapsed.QuadPart *= 1000000;
    elapsed.QuadPart /= Frequency.QuadPart;

    return elapsed.QuadPart;
}


#else

#include <time.h>
#define TIME_TYPE struct timespec
#define TIME_GET(X) clock_gettime(CLOCK_MONOTONIC, &X);
long diff_micro(struct timespec *start, struct timespec *end)
{
    /* us */
    return ((end->tv_sec * (1000000)) + (end->tv_nsec / 1000)) -
        ((start->tv_sec * 1000000) + (start->tv_nsec / 1000));
}

#endif

/*
 * decode64_chunk is an "internal NIF" scheduled by decode64 below. It takes
 * the binary argument, same as the other functions here, but also
 * takes a count of the max number of bytes to process per timeslice, the
 * offset into the binary at which to start processing, the resource type
 * holding the resulting data, and it's size.
 */
static ERL_NIF_TERM
decode64_chunk(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    unsigned long offset, i, end, max_per_slice, res_size;
    TIME_TYPE start, stop;
    int pct, total = 0;
    ERL_NIF_TERM newargv[5];
    ErlNifBinary bin;
    void* res;
    int illegal_char_error;

    if (argc != 5 || !enif_inspect_binary(env, argv[0], &bin) ||
        !enif_get_ulong(env, argv[1], &max_per_slice) ||
        !enif_get_ulong(env, argv[2], &offset) ||
        !enif_get_resource(env, argv[3], res_type, &res) ||
        !enif_get_ulong(env, argv[4], &res_size))
        return enif_make_badarg(env);
    end = offset + max_per_slice;
    if (end > bin.size) end = bin.size;
    i = offset;
    while (i < bin.size) {
        TIME_GET(start)
        illegal_char_error = 0;
        unsigned char* arg_ascii = (unsigned char*)(bin.data)+i;
        unsigned char* arg_bin = (unsigned char*)(res)+i*3/4;
        unbase64(arg_ascii, end-i, arg_bin, res_size-i*3/4, &illegal_char_error);
        if (illegal_char_error){
            return enif_make_badarg(env);
        }
        i = end;
        if (i == bin.size) break;
        TIME_GET(stop)
        /* determine how much of the timeslice was used */
        pct = (int)(diff_micro(&start, &stop)/10);
        total += pct;
        if (pct > 100) pct = 100;
        else if (pct == 0) pct = 1;
        if (enif_consume_timeslice(env, pct)) {
            /* the timeslice has been used up, so adjust our max_per_slice byte count based on
             * the processing we've done, then reschedule to run again */
            max_per_slice = i - offset;
            if (total > 100) {
                int m = (int)(total/100);
                if (m == 1)
                    max_per_slice -= (unsigned long)(max_per_slice*(total-100)/100);
                else
                    max_per_slice = (unsigned long)(max_per_slice/m);
            }
            max_per_slice = max_per_slice / 4;
            max_per_slice = max_per_slice * 4;
            newargv[0] = argv[0];
            newargv[1] = enif_make_ulong(env, max_per_slice);
            newargv[2] = enif_make_ulong(env, i);
            newargv[3] = argv[3];
            newargv[4] = argv[4];
            return enif_schedule_nif(env, "decode64_chunk", 0, decode64_chunk, argc, newargv);
        }
        end += max_per_slice;
        if (end > bin.size) end = bin.size;
    }
    return enif_make_resource_binary(env, res, res, res_size);
}

/*
 * decode64 just schedules decode64_chunk for execution, providing an initial
 * guess of 30KB for the max number of bytes to process before yielding the
 * scheduler thread.
 */
static ERL_NIF_TERM
decode64(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    ERL_NIF_TERM newargv[5];
    ErlNifBinary bin;
    unsigned res_size;
    void* res;

    if (argc != 1 || !enif_inspect_binary(env, argv[0], &bin))
        return enif_make_badarg(env);
    if (bin.size == 0)
        return argv[0];

    // The code below removes space-like characters from the input
    // and pads the input with '=' chars. The padding is made based on
    // the original input size (possibly including space-like characters)
    // to make it backwards-compatible with base64url 1.0.1 (huh).
    ERL_NIF_TERM no_space_bin;
    unsigned spaces_count = 0;
    unsigned padding_size = 0;

    for (int i = 0; i < bin.size; i++) {
        if (isspace(bin.data[i])) {
            spaces_count += 1;
        }
    }

    if (bin.size % 4 == 3) {
        padding_size = 1;
    } else if (bin.size % 4 == 2) {
        padding_size = 2;
    }
    unsigned no_space_bin_size = bin.size - spaces_count + padding_size;

    unsigned char* no_space_binary = enif_make_new_binary(env, no_space_bin_size, &no_space_bin);
    if (spaces_count) {
        int j = 0;
        for (int i = 0; i < bin.size; i++) {
            if (likely(!isspace(bin.data[i]))) {
               no_space_binary[j++] = bin.data[i];
            }
        }
    } else {
        memcpy(no_space_binary, bin.data, bin.size);
    }
    memcpy(no_space_binary + bin.size - spaces_count, "===", padding_size);

    res_size = unbase64_size(no_space_binary, no_space_bin_size);
    newargv[0] = no_space_bin;
    newargv[1] = enif_make_ulong(env, 30720); // MOD4
    newargv[2] = enif_make_ulong(env, 0);
    res = enif_alloc_resource(res_type, res_size);
    newargv[3] = enif_make_resource(env, res);
    newargv[4] = enif_make_ulong(env, res_size);
    enif_release_resource(res);
    return enif_schedule_nif(env, "decode64_chunk", 0, decode64_chunk, 5, newargv);
}

/*
 * encode64_chunk is an "internal NIF" scheduled by encode64 below. It takes
 * the binary argument, same as the other functions here, but also
 * takes a count of the max number of bytes to process per timeslice, the
 * offset into the binary at which to start processing, the resource type
 * holding the resulting data, and it's size.
 */
static ERL_NIF_TERM
encode64_chunk(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    unsigned long offset, i, end, max_per_slice, res_size;
    TIME_TYPE start, stop, slice;
    int pct, total = 0;
    ERL_NIF_TERM newargv[5];
    ErlNifBinary bin;
    void* res;

    if (argc != 5 || !enif_inspect_binary(env, argv[0], &bin) ||
        !enif_get_ulong(env, argv[1], &max_per_slice) ||
        !enif_get_ulong(env, argv[2], &offset) ||
        !enif_get_resource(env, argv[3], res_type, &res) ||
        !enif_get_ulong(env, argv[4], &res_size))
        return enif_make_badarg(env);
    end = offset + max_per_slice;
    if (end > bin.size) end = bin.size;
    i = offset;
    while (i < bin.size) {
        TIME_GET(start)
        unsigned char* arg_bin = (unsigned char*)(bin.data)+i;
        unsigned char* arg_res = (unsigned char*)(res)+i*4/3;
        base64(arg_bin, end-i, arg_res, res_size-i*4/3);
        i = end;
        if (i == bin.size) break;
        TIME_GET(stop)
        /* determine how much of the timeslice was used */
        pct = (int)(diff_micro(&start, &stop)/10);
        total += pct;
        if (pct > 100) pct = 100;
        else if (pct == 0) pct = 1;
        if (enif_consume_timeslice(env, pct)) {
            /* the timeslice has been used up, so adjust our max_per_slice byte count based on
             * the processing we've done, then reschedule to run again */
            max_per_slice = i - offset;
            if (total > 100) {
                int m = (int)(total/100);
                if (m == 1)
                    max_per_slice -= (unsigned long)(max_per_slice*(total-100)/100);
                else
                    max_per_slice = (unsigned long)(max_per_slice/m);
            }
            max_per_slice = max_per_slice / 3;
            max_per_slice = max_per_slice * 3;
            newargv[0] = argv[0];
            newargv[1] = enif_make_ulong(env, max_per_slice);
            newargv[2] = enif_make_ulong(env, i);
            newargv[3] = argv[3];
            newargv[4] = argv[4];
            return enif_schedule_nif(env, "encode64_chunk", 0, encode64_chunk, argc, newargv);
        }
        end += max_per_slice;
        if (end > bin.size) end = bin.size;
    }
    return enif_make_resource_binary(env, res, res, res_size);
}

/*
 * encode64 just schedules encode64_chunk for execution, providing an initial
 * guess of 30KB for the max number of bytes to process before yielding the
 * scheduler thread.
 */
static ERL_NIF_TERM
encode64(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    ErlNifResourceType* res_type = (ErlNifResourceType*)enif_priv_data(env);
    ERL_NIF_TERM newargv[5];
    ErlNifBinary bin;
    unsigned res_size;
    void* res;

    if (argc != 1 || !enif_inspect_binary(env, argv[0], &bin))
        return enif_make_badarg(env);
    if (bin.size == 0)
        return argv[0];
    res_size = base64_size(bin.size);
    newargv[0] = argv[0];
    newargv[1] = enif_make_ulong(env, 30720); // MOD3
    newargv[2] = enif_make_ulong(env, 0);
    res = enif_alloc_resource(res_type, res_size);
    newargv[3] = enif_make_resource(env, res);
    newargv[4] = enif_make_ulong(env, res_size);
    enif_release_resource(res);
    return enif_schedule_nif(env, "encode64_chunk", 0, encode64_chunk, 5, newargv);
}

static int
nifload(ErlNifEnv* env, void** priv_data, ERL_NIF_TERM load_info)
{
    *priv_data = enif_open_resource_type(env,
                                         NULL,
                                         "b64fast",
                                         NULL,
                                         ERL_NIF_RT_CREATE|ERL_NIF_RT_TAKEOVER,
                                         NULL);
    return *priv_data == NULL;
}

static int
nifupgrade(ErlNifEnv* env, void** priv_data, void** old_priv_data, ERL_NIF_TERM load_info)
{
    *priv_data = enif_open_resource_type(env,
                                         NULL,
                                         "b64fast",
                                         NULL,
                                         ERL_NIF_RT_TAKEOVER,
                                         NULL);
    return *priv_data == NULL;
}

static ErlNifFunc funcs[] = {
    {"encode64", 1, encode64},
    {"decode64", 1, decode64},
};
ERL_NIF_INIT(b64fast, funcs, nifload, NULL, nifupgrade, NULL);


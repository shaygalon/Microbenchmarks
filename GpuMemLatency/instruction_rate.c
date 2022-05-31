#include "opencltest.h"

float fp64_instruction_rate_test(cl_context context,
    cl_command_queue command_queue,
    uint32_t thread_count,
    uint32_t local_size,
    uint32_t chase_iterations,
    int float4_element_count,
    cl_mem a_mem_obj,
    cl_mem result_obj,
    cl_double* A,
    cl_double* result);

float run_rate_test(cl_context context,
    cl_command_queue command_queue,
    cl_kernel kernel,
    uint32_t thread_count,
    uint32_t local_size,
    uint32_t chase_iterations,
    int float4_element_count,
    cl_mem a_mem_obj,
    cl_mem result_obj,
    cl_double* A,
    cl_double* result,
    float totalOps);

float global_totalOps;

float instruction_rate_test(cl_context context,
    cl_command_queue command_queue,
    uint32_t thread_count,
    uint32_t local_size,
    uint32_t chase_iterations)
{
    size_t global_item_size = thread_count;
    size_t local_item_size = local_size;
    float gOpsPerSec = 0, totalOps;
    cl_int ret;
    int64_t time_diff_ms;
    int float4_element_count = local_size * 4;

    cl_program program = build_program(context, "instruction_rate_kernel.cl");
    cl_kernel int32_add_rate_kernel = clCreateKernel(program, "int32_add_rate_test", &ret);
    cl_kernel fp32_add_rate_kernel = clCreateKernel(program, "fp32_add_rate_test", &ret);
    cl_kernel int64_add_rate_kernel = clCreateKernel(program, "int64_add_rate_test", &ret);
    cl_kernel mix_fp32_int32_add_rate_kernel = clCreateKernel(program, "mix_fp32_int32_add_rate_test", &ret);

    float* A = (float*)malloc(sizeof(float) * float4_element_count * 4);
    float* result = (float*)malloc(sizeof(float) * 4 * thread_count);

    if (!A || !result)
    {
        fprintf(stderr, "Failed to allocate memory instruction rate test\n");
    }

    cl_mem a_mem_obj = clCreateBuffer(context, CL_MEM_READ_ONLY, float4_element_count * sizeof(float), NULL, &ret);
    cl_mem result_obj = clCreateBuffer(context, CL_MEM_READ_WRITE, sizeof(float) * 4 * thread_count, NULL, &ret);

    // Integer test first
    uint32_t *int32_A = (uint32_t*)A;
    for (int i = 0; i < float4_element_count * 4; i++)
    {
        int32_A[i] = i + 1;
    }

    // 4x int4 * 8 per iteration, and count the loop increment too
    totalOps = (float)chase_iterations * (4.0f * 8.0f + 1.0f) * (float)thread_count;
    float int32_rate = run_rate_test(context, command_queue, fp32_add_rate_kernel, thread_count, local_size, chase_iterations, 
        float4_element_count, a_mem_obj, result_obj, A, result, totalOps);
    fprintf(stderr, "%f G INT32 Adds/sec\n", int32_rate);

    // FP32 add test
    cl_float* fp32_A = (cl_float*)A;
    for (int i = 0; i < float4_element_count * 4; i++)
    {
        fp32_A[i] = 0.5f * i;
    }

    totalOps = (float)chase_iterations * (4.0f * 8.0f) * (float)thread_count;
    float fp32_rate = run_rate_test(context, command_queue, fp32_add_rate_kernel, thread_count, local_size, chase_iterations, 
        float4_element_count, a_mem_obj, result_obj, A, result, totalOps);
    fprintf(stderr, "%f G FP32 Adds/sec\n", fp32_rate);

    // Mixed INT32 and FP32 - 4 FP32, 4 INT32, and the loop increment
    totalOps = (float)chase_iterations * (4.0f * 8.0f + 1.0f) * (float)thread_count;
    float mix_fp32_int32_rate = run_rate_test(context, command_queue, mix_fp32_int32_add_rate_kernel, thread_count, local_size, chase_iterations,
        float4_element_count, a_mem_obj, result_obj, A, result, totalOps);
    fprintf(stderr, "%f G mixed INT32 and FP32 Adds/sec\n", mix_fp32_int32_rate);

    // INT64 add test
    cl_ulong* int64_A = (cl_ulong*)A;
    for (int i = 0; i < float4_element_count * 2; i++)
    {
        int64_A[i] = i * 2;
    }

    totalOps = (float)(chase_iterations / 2) * (2.0f * 8.0f) * (float)thread_count;
    float int64_rate = run_rate_test(context, command_queue, int64_add_rate_kernel, thread_count, local_size, chase_iterations / 2,
        float4_element_count, a_mem_obj, result_obj, A, result, totalOps);
    fprintf(stderr, "%f G INT64 Adds/sec\n", int64_rate);

    if (checkExtensionSupport("cl_khr_fp64")) {
        float fp64_rate = fp64_instruction_rate_test(context, command_queue, thread_count, local_size, chase_iterations, float4_element_count,
            a_mem_obj, result_obj, A, result);
        fprintf(stderr, "%f G FP64 Adds/sec\n", fp64_rate);
    }
    else {
        fprintf(stderr, "FP64 not supported\n");
    }

cleanup:
    clFlush(command_queue);
    clFinish(command_queue);
    clReleaseMemObject(a_mem_obj);
    clReleaseMemObject(result_obj);
    free(A);
    free(result);
    return gOpsPerSec;
}

float run_rate_test(cl_context context,
    cl_command_queue command_queue,
    cl_kernel kernel,
    uint32_t thread_count,
    uint32_t local_size,
    uint32_t chase_iterations,
    int float4_element_count,
    cl_mem a_mem_obj,
    cl_mem result_obj,
    cl_double* A,
    cl_double* result,
    float totalOps)
{
    size_t global_item_size = thread_count;
    size_t local_item_size = local_size;
    cl_int ret;
    float gOpsPerSec;
    uint64_t time_diff_ms;

    memset(result, 0, sizeof(float) * 4 * thread_count);

    ret = clEnqueueWriteBuffer(command_queue, a_mem_obj, CL_TRUE, 0, float4_element_count * sizeof(float), A, 0, NULL, NULL);
    ret = clEnqueueWriteBuffer(command_queue, result_obj, CL_TRUE, 0, sizeof(float) * 4 * thread_count, result, 0, NULL, NULL);
    clSetKernelArg(kernel, 0, sizeof(cl_mem), (void*)&a_mem_obj);
    clSetKernelArg(kernel, 1, sizeof(cl_int), (void*)&chase_iterations);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), (void*)&result_obj);
    clFinish(command_queue);

    //fprintf(stderr, "Submitting fp32 add kernel to command queue\n");
    start_timing();
    ret = clEnqueueNDRangeKernel(command_queue, kernel, 1, NULL, &global_item_size, &local_item_size, 0, NULL, NULL);
    if (ret != CL_SUCCESS)
    {
        fprintf(stderr, "Failed to submit fp32 add kernel to command queue. clEnqueueNDRangeKernel returned %d\n", ret);
        gOpsPerSec = 0;
        return 0;
    }

    ret = clFinish(command_queue);
    if (ret != CL_SUCCESS)
    {
        printf("Failed to finish command queue. clFinish returned %d\n", ret);
        gOpsPerSec = 0;
        return 0;
    }

    time_diff_ms = end_timing();

    gOpsPerSec = ((float)totalOps / 1e9) / ((float)time_diff_ms / 1000);
    //fprintf(stderr, "chase iterations: %d, thread count: %d\n", chase_iterations, thread_count);
    //fprintf(stderr, "total ops: %f (%.2f G)\ntotal time: %llu ms\n", totalOps, totalOps / 1e9, time_diff_ms);
    return gOpsPerSec;
}

// taking out FP64 because some implementations don't support it
float fp64_instruction_rate_test(cl_context context,
    cl_command_queue command_queue,
    uint32_t thread_count,
    uint32_t local_size,
    uint32_t chase_iterations,
    int float4_element_count,
    cl_mem a_mem_obj,
    cl_mem result_obj,
    cl_double *A,
    cl_double *result)
{
    size_t global_item_size = thread_count;
    size_t local_item_size = local_size;
    float gOpsPerSec, totalOps;
    cl_int ret;
    int64_t time_diff_ms;

    // FP64 add test
    uint32_t low_chase_iterations = chase_iterations / 4;
    cl_double* fp64_A = (cl_float*)A;
    for (int i = 0; i < float4_element_count * 2; i++)
    {
        fp64_A[i] = 0.5f * i;
    }

    memset(result, 0, sizeof(float) * 4 * thread_count);

    cl_program program = build_program(context, "instruction_rate_fp64_kernel.cl");
    cl_kernel fp64_add_rate_kernel = clCreateKernel(program, "fp64_add_rate_test", &ret);
    totalOps = (float)low_chase_iterations * (2.0f * 8.0f) * (float)thread_count;
    gOpsPerSec = run_rate_test(context, command_queue, fp64_add_rate_kernel, thread_count, local_size, low_chase_iterations, 
        float4_element_count, a_mem_obj, result_obj, A, result, totalOps);
    return gOpsPerSec;
}
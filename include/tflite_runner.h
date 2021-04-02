#ifndef __TFLITE_RUN_H__
#define __TFLITE_RUN_H__

#include <stddef.h>

#ifdef __cplusplus
#define EXTERNC extern "C"
#else
#define EXTERNC
#endif

typedef struct _tflite_runner_t tflite_runner_t;

EXTERNC tflite_runner_t *tflite_runner_create_from_file(const char *model_file_name);
EXTERNC tflite_runner_t *tflite_runner_create_from_mem(const void *model_data, size_t model_size);
EXTERNC int tflite_runner_run(tflite_runner_t *self,
                              const float *input, size_t input_size,
                              float *score);
EXTERNC const char *tflite_get_label(int id);           
EXTERNC void tflite_runner_destroy(tflite_runner_t **self_p);

#undef EXTERNC

#endif // __TFLITE_RUN_H__
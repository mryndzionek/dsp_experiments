#include "tflite_runner.h"

#include "logging.h"

#include "tensorflow/lite/c/c_api.h"

#define LABEL_COUNT (17)

static const char *labels[LABEL_COUNT] = {"zero", "one", "two", "three",
                                          "four", "five", "six", "seven",
                                          "eight", "nine", "yes", "no",
                                          "left", "right", "up", "down", "go"};

struct _tflite_runner_t
{
    TfLiteInterpreter* interpreter;
};

static tflite_runner_t *tflite_runner_create(TfLiteModel *model)
{
    TfLiteStatus s;

    tflite_runner_t *self = (tflite_runner_t *)malloc(sizeof(tflite_runner_t));
    log_assert(self);

    TfLiteInterpreterOptions *options = TfLiteInterpreterOptionsCreate();
    self->interpreter = TfLiteInterpreterCreate(model, options);
    log_assert(self->interpreter != nullptr);
    TfLiteInterpreterOptionsSetNumThreads(options, 2);

    s = TfLiteInterpreterAllocateTensors(self->interpreter);
    log_assert(s == kTfLiteOk);

    log_assert(TfLiteInterpreterGetInputTensorCount(self->interpreter) == 1);
    log_assert(TfLiteInterpreterGetOutputTensorCount(self->interpreter) == 1);

    TfLiteModelDelete(model);
    TfLiteInterpreterOptionsDelete(options);

    return self;
}

tflite_runner_t *tflite_runner_create_from_file(const char *model_file_name)
{
    TfLiteModel *model =
        TfLiteModelCreateFromFile(model_file_name);
    log_assert(model != nullptr);
    return tflite_runner_create(model);
}

tflite_runner_t *tflite_runner_create_from_mem(const void *model_data, size_t model_size)
{
    TfLiteModel *model =
        TfLiteModelCreate(model_data, model_size);
    log_assert(model != nullptr);
    return tflite_runner_create(model);
}

int tflite_runner_run(tflite_runner_t *self,
                      const float *input, size_t input_size,
                      float *score)
{
    TfLiteStatus s;
    int argmax = -1;
    float output[sizeof(labels)];

    TfLiteTensor* input_tensor = TfLiteInterpreterGetInputTensor(self->interpreter, 0);
    log_assert(input_tensor != nullptr);
    log_assert(TfLiteTensorType(input_tensor) == kTfLiteFloat32);
    log_assert(TfLiteTensorByteSize(input_tensor) == input_size * sizeof(float));
    s = TfLiteTensorCopyFromBuffer(input_tensor, input, input_size * sizeof(float));
    log_assert(s == kTfLiteOk);

    s = TfLiteInterpreterInvoke(self->interpreter);
    log_assert(s == kTfLiteOk);

    const TfLiteTensor *output_tensor =
        TfLiteInterpreterGetOutputTensor(self->interpreter, 0);
    log_assert(TfLiteTensorByteSize(output_tensor) == LABEL_COUNT * sizeof(float));

    s = TfLiteTensorCopyToBuffer(output_tensor, output,
                                 LABEL_COUNT * sizeof(float));
    log_assert(s == kTfLiteOk);

    *score = 0.001f;
    for (int i = 0; i < (int)LABEL_COUNT; ++i)
    {
        if (*score < output[i])
        {
            *score = output[i];
            argmax = i;
        }
    }
    return argmax;
}

const char *tflite_get_label(int id)
{
    if ((id < 0) || (id > LABEL_COUNT))
    {
        return NULL;
    }
    else
    {
        return labels[id];
    }
}

void tflite_runner_destroy(tflite_runner_t **self_p)
{
    LOG(DEBUG, "Destroying");
    log_assert(self_p);
    if (*self_p)
    {
        tflite_runner_t *self = *self_p;
        TfLiteInterpreterDelete(self->interpreter);
        free(self);
        *self_p = NULL;
    }
    LOG(DEBUG, "Destroyed");
}
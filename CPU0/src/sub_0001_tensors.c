#include "sub_0001_tensors.h"

const TensorInfo sub_0001_tensors[] = {
  { "_split_1_command_stream", 1, 1044, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 2, 35088, "MODEL", 0xffffffff },
  { "_split_1_scratch", 3, 62160, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 4, 62160, "FAST_SCRATCH", 0x0 },
  { "serving_default_input_layer_0_10028", 5, 3110, "INPUT_TENSOR", 0xc260 },
  { "StatefulPartitionedCall_1_0_70021_10062", 0, 1, "OUTPUT_TENSOR", 0x10 },
};

const size_t sub_0001_tensors_count = sizeof(sub_0001_tensors) / sizeof(sub_0001_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0001_address_serving_default_input_layer_0_10028 = 0xc260;
const uint32_t sub_0001_address_StatefulPartitionedCall_1_0_70021_10062 = 0x10;


#pragma once

#include <stdint.h>
#include <vector>

struct DeviceEndpointReading {
    uint16_t endpoint_id;
    uint32_t cluster_id;
    uint32_t attribute_id;
    int64_t  value;
};

using DeviceReadings = std::vector<DeviceEndpointReading>;

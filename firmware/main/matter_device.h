#pragma once

#include <stdint.h>

class IMatterDevice {
public:
    virtual ~IMatterDevice() = default;
    virtual uint16_t endpoint_id() const = 0;
    virtual void set_raw(uint32_t cluster_id, uint32_t attribute_id, uint16_t raw_value) = 0;
};

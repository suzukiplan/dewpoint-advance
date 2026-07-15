/**
 * Dewpoint Advance Runtime
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#pragma once

#include "mgbahelper.h"

#include <functional>
#include <memory>

class DewpointRuntime final : public DewpointBridge
{
  public:
    using Logger = std::function<void(const char*)>;

    explicit DewpointRuntime(mGBAHelper& gba, Logger logger = {});
    ~DewpointRuntime() override;

    DewpointRuntime(const DewpointRuntime&) = delete;
    DewpointRuntime& operator=(const DewpointRuntime&) = delete;

    bool initialize();
    void tick();

    uint32_t readRegister(uint32_t index) override;
    void writeRegister(uint32_t index, uint32_t value) override;
    void reset() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
